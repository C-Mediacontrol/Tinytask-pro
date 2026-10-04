#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ttp_storage.h"

/* Helper: Promote 24bpp BMP to 32bpp BGRA with A=255 for seamless backward compatibility */
static BYTE* convert_24bpp_to_32bpp(const BYTE* src, DWORD srcSize, DWORD* outSize) {
    if (!src || srcSize < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) {
        return NULL;
    }

    const BITMAPFILEHEADER* bmfh = (const BITMAPFILEHEADER*)src;
    if (bmfh->bfType != 0x4D42) { /* 'BM' */
        return NULL;
    }

    const BITMAPINFOHEADER* bmih = (const BITMAPINFOHEADER*)(src + sizeof(BITMAPFILEHEADER));
    if (bmih->biSize < sizeof(BITMAPINFOHEADER) || bmih->biBitCount != 24) {
        return NULL;
    }

    if (bmih->biCompression != BI_RGB) {
        return NULL;
    }

    int w = bmih->biWidth;
    int h = abs(bmih->biHeight);
    if (w <= 0 || h <= 0) {
        return NULL;
    }

    int srcStride = ((w * 3 + 3) / 4) * 4;
    DWORD srcPixelDataSize = (DWORD)srcStride * h;
    if (bmfh->bfOffBits + srcPixelDataSize > srcSize) {
        return NULL;
    }

    int dstStride = w * 4;
    DWORD dstPixelDataSize = (DWORD)dstStride * h;
    DWORD dstTotalSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dstPixelDataSize;

    BYTE* dst = (BYTE*)malloc(dstTotalSize);
    if (!dst) {
        return NULL;
    }

    BITMAPFILEHEADER* dstBmfh = (BITMAPFILEHEADER*)dst;
    dstBmfh->bfType = 0x4D42;
    dstBmfh->bfSize = dstTotalSize;
    dstBmfh->bfReserved1 = 0;
    dstBmfh->bfReserved2 = 0;
    dstBmfh->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    BITMAPINFOHEADER* dstBmih = (BITMAPINFOHEADER*)(dst + sizeof(BITMAPFILEHEADER));
    memcpy(dstBmih, bmih, sizeof(BITMAPINFOHEADER));
    dstBmih->biSize = sizeof(BITMAPINFOHEADER);
    dstBmih->biBitCount = 32;
    dstBmih->biCompression = BI_RGB;
    dstBmih->biSizeImage = dstPixelDataSize;

    const BYTE* srcPixels = src + bmfh->bfOffBits;
    BYTE* dstPixels = dst + dstBmfh->bfOffBits;

    for (int y = 0; y < h; y++) {
        const BYTE* srcRow = srcPixels + y * srcStride;
        BYTE* dstRow = dstPixels + y * dstStride;
        for (int x = 0; x < w; x++) {
            dstRow[x * 4 + 0] = srcRow[x * 3 + 0]; /* B */
            dstRow[x * 4 + 1] = srcRow[x * 3 + 1]; /* G */
            dstRow[x * 4 + 2] = srcRow[x * 3 + 2]; /* R */
            dstRow[x * 4 + 3] = 255;               /* A = 255 (100% opaque foreground) */
        }
    }

    *outSize = dstTotalSize;
    return dst;
}

BOOL ttp_save_project(const char* filepath, const TTPStep* steps, DWORD stepCount, const BYTE** bmpBuffers, const DWORD* bmpSizes) {
    if (!filepath) {
        return FALSE;
    }
    if (stepCount > 0 && !steps) {
        return FALSE;
    }

    FILE* fp = fopen(filepath, "wb");
    if (!fp) {
        return FALSE;
    }

    TTPHeader header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, TTP_MAGIC, 4);
    header.version = TTP_VERSION;
    header.stepCount = stepCount;
    header.flags = 0;

    if (fwrite(&header, sizeof(TTPHeader), 1, fp) != 1) {
        fclose(fp);
        return FALSE;
    }

    TTPStep* stepsCopy = NULL;
    if (stepCount > 0) {
        stepsCopy = (TTPStep*)malloc(sizeof(TTPStep) * stepCount);
        if (!stepsCopy) {
            fclose(fp);
            return FALSE;
        }
        memcpy(stepsCopy, steps, sizeof(TTPStep) * stepCount);

        DWORD currentBlobOffset = (DWORD)(sizeof(TTPHeader) + stepCount * sizeof(TTPStep));
        for (DWORD i = 0; i < stepCount; i++) {
            if (bmpSizes && bmpBuffers && bmpSizes[i] > 0 && bmpBuffers[i] != NULL) {
                stepsCopy[i].imageOffset = currentBlobOffset;
                stepsCopy[i].imageSize = bmpSizes[i];
                currentBlobOffset += bmpSizes[i];
            } else {
                stepsCopy[i].imageOffset = 0;
                stepsCopy[i].imageSize = 0;
            }
        }

        if (fwrite(stepsCopy, sizeof(TTPStep), stepCount, fp) != stepCount) {
            free(stepsCopy);
            fclose(fp);
            return FALSE;
        }

        for (DWORD i = 0; i < stepCount; i++) {
            if (stepsCopy[i].imageSize > 0 && bmpBuffers && bmpBuffers[i] != NULL) {
                if (fwrite(bmpBuffers[i], 1, stepsCopy[i].imageSize, fp) != stepsCopy[i].imageSize) {
                    free(stepsCopy);
                    fclose(fp);
                    return FALSE;
                }
            }
        }

        free(stepsCopy);
    }

    fclose(fp);
    return TRUE;
}

BOOL ttp_load_project(const char* filepath, TTPStep** outSteps, DWORD* outStepCount, BYTE*** outBmpBuffers, DWORD** outBmpSizes) {
    if (!filepath || !outSteps || !outStepCount || !outBmpBuffers || !outBmpSizes) {
        return FALSE;
    }

    *outSteps = NULL;
    *outStepCount = 0;
    *outBmpBuffers = NULL;
    *outBmpSizes = NULL;

    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        return FALSE;
    }

    TTPHeader header;
    if (fread(&header, sizeof(TTPHeader), 1, fp) != 1) {
        fclose(fp);
        return FALSE;
    }

    if (memcmp(header.magic, TTP_MAGIC, 4) != 0) {
        fclose(fp);
        return FALSE;
    }

    if (header.version < 1) {
        fclose(fp);
        return FALSE;
    }

    DWORD count = header.stepCount;
    TTPStep* steps = NULL;
    BYTE** bmpBuffers = NULL;
    DWORD* bmpSizes = NULL;

    if (count > 0) {
        if (header.version == TTP_VERSION_LEGACY) {
            /* Legacy version 1: read 172-byte TTPStep_v1 and map to TTPStep */
            TTPStep_v1* legacySteps = (TTPStep_v1*)malloc(sizeof(TTPStep_v1) * count);
            if (!legacySteps) {
                fclose(fp);
                return FALSE;
            }

            if (fread(legacySteps, sizeof(TTPStep_v1), count, fp) != count) {
                free(legacySteps);
                fclose(fp);
                return FALSE;
            }

            steps = (TTPStep*)malloc(sizeof(TTPStep) * count);
            if (!steps) {
                free(legacySteps);
                fclose(fp);
                return FALSE;
            }

            for (DWORD i = 0; i < count; i++) {
                memset(&steps[i], 0, sizeof(TTPStep));
                steps[i].stepId = legacySteps[i].stepId;
                steps[i].actionType = legacySteps[i].actionType;
                steps[i].targetMode = legacySteps[i].targetMode;
                steps[i].origX = legacySteps[i].origX;
                steps[i].origY = legacySteps[i].origY;
                steps[i].destX = legacySteps[i].destX;
                steps[i].destY = legacySteps[i].destY;
                steps[i].timeoutMs = legacySteps[i].timeoutMs;
                steps[i].postDelayMs = legacySteps[i].postDelayMs;
                memcpy(steps[i].textKey, legacySteps[i].textKey, sizeof(steps[i].textKey));
                steps[i].imageOffset = legacySteps[i].imageOffset;
                steps[i].imageSize = legacySteps[i].imageSize;
                steps[i].chromaTol = 0; /* Legacy default: disabled */
                steps[i].reserved[0] = 0;
                steps[i].reserved[1] = 0;
                steps[i].reserved[2] = 0;
            }
            free(legacySteps);
        } else {
            /* Version 2 and later: read current 176-byte TTPStep */
            steps = (TTPStep*)malloc(sizeof(TTPStep) * count);
            if (!steps) {
                fclose(fp);
                return FALSE;
            }

            if (fread(steps, sizeof(TTPStep), count, fp) != count) {
                free(steps);
                fclose(fp);
                return FALSE;
            }
        }

        bmpBuffers = (BYTE**)calloc(count, sizeof(BYTE*));
        bmpSizes = (DWORD*)calloc(count, sizeof(DWORD));
        if (!bmpBuffers || !bmpSizes) {
            if (bmpBuffers) free(bmpBuffers);
            if (bmpSizes) free(bmpSizes);
            free(steps);
            fclose(fp);
            return FALSE;
        }

        for (DWORD i = 0; i < count; i++) {
            if (steps[i].imageSize > 0) {
                if (fseek(fp, (long)steps[i].imageOffset, SEEK_SET) != 0) {
                    ttp_free_project(steps, bmpBuffers, bmpSizes, count);
                    fclose(fp);
                    return FALSE;
                }

                BYTE* rawBmp = (BYTE*)malloc(steps[i].imageSize);
                if (!rawBmp) {
                    ttp_free_project(steps, bmpBuffers, bmpSizes, count);
                    fclose(fp);
                    return FALSE;
                }

                if (fread(rawBmp, 1, steps[i].imageSize, fp) != steps[i].imageSize) {
                    free(rawBmp);
                    ttp_free_project(steps, bmpBuffers, bmpSizes, count);
                    fclose(fp);
                    return FALSE;
                }

                /* Backward compatibility: auto-promote 24bpp BMP to 32bpp BGRA with A=255 */
                DWORD promotedSize = 0;
                BYTE* promotedBmp = convert_24bpp_to_32bpp(rawBmp, steps[i].imageSize, &promotedSize);
                if (promotedBmp) {
                    free(rawBmp);
                    bmpBuffers[i] = promotedBmp;
                    bmpSizes[i] = promotedSize;
                    steps[i].imageSize = promotedSize;
                } else {
                    bmpBuffers[i] = rawBmp;
                    bmpSizes[i] = steps[i].imageSize;
                }
            } else {
                bmpBuffers[i] = NULL;
                bmpSizes[i] = 0;
            }
        }
    }

    fclose(fp);

    *outSteps = steps;
    *outStepCount = count;
    *outBmpBuffers = bmpBuffers;
    *outBmpSizes = bmpSizes;

    return TRUE;
}

void ttp_free_project(TTPStep* steps, BYTE** bmpBuffers, DWORD* bmpSizes, DWORD stepCount) {
    if (bmpBuffers) {
        for (DWORD i = 0; i < stepCount; i++) {
            if (bmpBuffers[i]) {
                free(bmpBuffers[i]);
                bmpBuffers[i] = NULL;
            }
        }
        free(bmpBuffers);
    }
    if (bmpSizes) {
        free(bmpSizes);
    }
    if (steps) {
        free(steps);
    }
}
