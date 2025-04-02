#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <cstring>
#include <cmath>

#pragma pack(push, 1)
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BITMAPFileHeader;

typedef struct {
    uint32_t biSize;
    int32_t biWidth;
    int32_t biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t biXPelsPerMeter;
    int32_t biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BITMAPInfoHeader;

typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t reserved;
} RGBQUAD;
#pragma pack(pop)

void makeUnNearestNeighborTable(int* distTable, int distTableLen, int* outLenTable, int* outLenTableSize) {
    printf("distTable[%d]={", distTableLen);
    for (int x=0; x<distTableLen; x++) {
        printf("%d,", distTable[x]);
    }
    printf("}\n");

    // X方向の差分の位置の距離を蓄積
    // 「1,0,0,0,1,0,1,0」なら、「4,2,2」が出力される
    int idx = 0;
    {
        int len = 0;
        for (int x=1; x<distTableLen; x++) {
            if (distTable[x] == 0) {
                len++;
                continue;
            }
            outLenTable[idx++] = len+1;
            len = 0;
        }
        if (len > 0) {
            outLenTable[idx++] = len+1;
        }
    }

    printf("start outLenTable[%d]={", idx);
    for (int x=0; x<idx; x++) {
        printf("%d,", outLenTable[x]);
    }
    printf("}\n");

    // 距離が長すぎる所を分割する
    // 「4,2,2」なら、「2,2,2,2」に変換される
    bool loop = true;
    while (loop) {
        // 全部の距離の平均をとる
        int dist = 0;
        for (int i=0; i<idx; i++) {
            dist += outLenTable[i];
        }
        dist = (int)(float(dist) / idx);

        loop = false;
        for (int i=0; i<idx; i++) {
            // 「距離の平均+1」を超えたら分割する
            if (outLenTable[i] > dist+1) {
                for (int j=idx; j>i; j--) {
                    outLenTable[j] = outLenTable[j-1];
                }
                int len = outLenTable[i];
                outLenTable[i] = len/2;
                outLenTable[i+1] = len - (len/2);
                printf("expand outLenTable[%d]=%d => %d, %d\n", i, len, len/2, len-(len/2));
                idx++;
                loop = true;
            }
        }
    }

    printf("end outLenTable[%d]={", idx);
    for (int x=0; x<idx; x++) {
        printf("%d,", outLenTable[x]);
    }
    printf("}\n");

    *(outLenTableSize) = idx;
}

void unnearestNeighborBMP(const char *inFile, const char *outFile) {
    FILE *infile = fopen(inFile, "rb");
    if (!infile) {
        fprintf(stderr, "Failed to open BMP file [%s]", inFile);
        exit(EXIT_FAILURE);
    }

    // BMPのヘッダを読む
    BITMAPFileHeader bmpHeader;
    BITMAPInfoHeader bmpInfoHeader;
    fread(&bmpHeader, sizeof(BITMAPFileHeader), 1, infile);
    fread(&bmpInfoHeader, sizeof(BITMAPInfoHeader), 1, infile);

    // BMP判定と8bpp値確認
    if ((bmpHeader.bfType != 0x4D42) || (bmpInfoHeader.biBitCount != 8) || (bmpInfoHeader.biClrUsed != 0)) {
        fprintf(stderr, "Not a valid 8-bit BMP file.\n");
        fclose(infile);
        exit(EXIT_FAILURE);
    }

    // BMPのパレットを読む
    int paletteSize = 256;
    RGBQUAD *bmpPalette = (RGBQUAD *)malloc(paletteSize * sizeof(RGBQUAD));
    fread(bmpPalette, sizeof(RGBQUAD), paletteSize, infile);

    // BMPの情報取得
    int width = bmpInfoHeader.biWidth;
    int height = abs(bmpInfoHeader.biHeight);
    int rowSize = ((width + 1) & (-2));
    printf("BMP width=%d(rawsize=%d), height=%d\n", width, rowSize, height);

    // BMPのビットマップを読む
    unsigned char *bitmapData = (unsigned char *)malloc(rowSize * height);
    fseek(infile, bmpHeader.bfOffBits, SEEK_SET);
    fread(bitmapData, rowSize, height, infile);
    fclose(infile);

    // X方向のテーブル準備
    int *xDistTable = (int *)malloc(width * sizeof(int));
    memset(xDistTable, 0, width * sizeof(int));
    int *xLenTable = (int *)malloc(width * sizeof(int));
    memset(xLenTable, 0, width * sizeof(int));
    int xLenTableSize = 0;

    // X方向の差分が発生する位置を蓄積
    for (int y=1; y<height; y++) {
        xDistTable[0]++;
        for (int x=1; x<width; x++) {
            if (bitmapData[y*rowSize+(x-1)] != bitmapData[y*rowSize+x]) {
               // printf("diff (%d, %d) %d %d\n", x, y, bitmapData[y*rowSize+(x-1)], bitmapData[y*rowSize+x]);
               xDistTable[x]++;
            }
        }
    }

    // X方向の連長テーブルを作成
    makeUnNearestNeighborTable(xDistTable, width, xLenTable, &xLenTableSize);

    // Y方向のテーブル準備
    int *yDistTable = (int *)malloc(height * sizeof(int));
    memset(yDistTable, 0, height * sizeof(int));
    int *yLenTable = (int *)malloc(height * sizeof(int));
    memset(yLenTable, 0, height * sizeof(int));
    int yLenTableSize = 0;

    // Y方向の差分が発生する位置を蓄積
    for (int x=0; x<width; x++) {
        yDistTable[0]++;
        for (int y=1; y<height; y++) {
            if (bitmapData[(y-1)*rowSize+x] != bitmapData[y*rowSize+x]) {
               // printf("diff (%d, %d) %d %d\n", x, y, bitmapData[(y-1)*rowSize+x], bitmapData[y*rowSize+x]);
               yDistTable[y]++;
            }
        }
    }

    // Y方向の連長テーブルを作成
    makeUnNearestNeighborTable(yDistTable, height, yLenTable, &yLenTableSize);

    // 新しい画像の情報を作成
    int newWidth = xLenTableSize;
    int newHeight = yLenTableSize;
    int newRowSize = ((newWidth + 1) & (-2));
    printf("New BMP width=%d(rawsize=%d), height=%d\n", newWidth, newRowSize, newHeight);

    // 新しい画像のビットマップを作成
    unsigned char *newBitmapData = (unsigned char *)malloc(newRowSize * newHeight);
    memset(newBitmapData, 0, newRowSize * newHeight);

    // 新しい画像のビットマップを埋める
    {
        int oldY = 0;
        for (int y=0; y<newHeight; y++) {
            int oldX = 0;
            for (int x=0; x<newWidth; x++) {
                newBitmapData[y*newRowSize+x] = bitmapData[oldY*rowSize+oldX];
                oldX += xLenTable[x];
            }
            oldY+= yLenTable[y];
        }
    }

    // BMPヘッダーを更新
    bmpInfoHeader.biWidth = newWidth;
    bmpInfoHeader.biHeight = newHeight;

    // 新しいBMPヘッダーを書き出す
    FILE *fout = fopen(outFile, "wb");
    if (!fout) {
        fprintf(stderr, "Failed to open output file [%s]", outFile);
        exit(EXIT_FAILURE);
    }
    fwrite(&bmpHeader, sizeof(BITMAPFileHeader), 1, fout);
    fwrite(&bmpInfoHeader, sizeof(BITMAPInfoHeader), 1, fout);

    // 新しいBMPパレットを書き出す
    fwrite(bmpPalette, paletteSize, sizeof(RGBQUAD), fout);

    // 新しいBMPビットマップを書き出す
    fwrite(newBitmapData, newRowSize, newHeight, fout);
    fclose(fout);

    printf("Conversion complete.\n");
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "BMP Un-NearestNeighbor Converter: 20250402a Hiroaki GOTO as GORRY \n", argv[0]);
        fprintf(stderr, "Usage: %s <input BMP file> <output BMP file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    unnearestNeighborBMP(argv[1], argv[2]);
    return EXIT_SUCCESS;
}
