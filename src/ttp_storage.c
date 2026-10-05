#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
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
    int h = __builtin_abs(bmih->biHeight);
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

    BYTE* dst = (BYTE*)HeapAlloc(GetProcessHeap(), 0, dstTotalSize);
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
    __builtin_memcpy(dstBmih, bmih, sizeof(BITMAPINFOHEADER));
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

    HANDLE hFile = CreateFileA(filepath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    TTPHeader header;
    __builtin_memset(&header, 0, sizeof(header));
    __builtin_memcpy(header.magic, TTP_MAGIC, 4);
    header.version = TTP_VERSION;
    header.stepCount = stepCount;
    header.flags = 0;

    DWORD dwWritten = 0;
    if (!WriteFile(hFile, &header, sizeof(TTPHeader), &dwWritten, NULL) || dwWritten != sizeof(TTPHeader)) {
        CloseHandle(hFile);
        return FALSE;
    }

    TTPStep* stepsCopy = NULL;
    if (stepCount > 0) {
        stepsCopy = (TTPStep*)HeapAlloc(GetProcessHeap(), 0, sizeof(TTPStep) * stepCount);
        if (!stepsCopy) {
            CloseHandle(hFile);
            return FALSE;
        }
        __builtin_memcpy(stepsCopy, steps, sizeof(TTPStep) * stepCount);

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

        DWORD stepsBytes = sizeof(TTPStep) * stepCount;
        if (!WriteFile(hFile, stepsCopy, stepsBytes, &dwWritten, NULL) || dwWritten != stepsBytes) {
            if (stepsCopy) {
                HeapFree(GetProcessHeap(), 0, stepsCopy);
            }
            CloseHandle(hFile);
            return FALSE;
        }

        for (DWORD i = 0; i < stepCount; i++) {
            if (stepsCopy[i].imageSize > 0 && bmpBuffers && bmpBuffers[i] != NULL) {
                if (!WriteFile(hFile, bmpBuffers[i], stepsCopy[i].imageSize, &dwWritten, NULL) || dwWritten != stepsCopy[i].imageSize) {
                    if (stepsCopy) {
                        HeapFree(GetProcessHeap(), 0, stepsCopy);
                    }
                    CloseHandle(hFile);
                    return FALSE;
                }
            }
        }

        if (stepsCopy) {
            HeapFree(GetProcessHeap(), 0, stepsCopy);
        }
    }

    CloseHandle(hFile);
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

    HANDLE hFile = CreateFileA(filepath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    TTPHeader header;
    DWORD dwRead = 0;
    if (!ReadFile(hFile, &header, sizeof(TTPHeader), &dwRead, NULL) || dwRead != sizeof(TTPHeader)) {
        CloseHandle(hFile);
        return FALSE;
    }

    if (header.magic[0] != TTP_MAGIC[0] || header.magic[1] != TTP_MAGIC[1] ||
        header.magic[2] != TTP_MAGIC[2] || header.magic[3] != TTP_MAGIC[3]) {
        CloseHandle(hFile);
        return FALSE;
    }

    if (header.version < 1) {
        CloseHandle(hFile);
        return FALSE;
    }

    DWORD count = header.stepCount;
    TTPStep* steps = NULL;
    BYTE** bmpBuffers = NULL;
    DWORD* bmpSizes = NULL;

    if (count > 0) {
        if (header.version == TTP_VERSION_LEGACY) {
            /* Legacy version 1: read 172-byte TTPStep_v1 and map to TTPStep */
            DWORD legacyBytes = sizeof(TTPStep_v1) * count;
            TTPStep_v1* legacySteps = (TTPStep_v1*)HeapAlloc(GetProcessHeap(), 0, legacyBytes);
            if (!legacySteps) {
                CloseHandle(hFile);
                return FALSE;
            }

            if (!ReadFile(hFile, legacySteps, legacyBytes, &dwRead, NULL) || dwRead != legacyBytes) {
                if (legacySteps) {
                    HeapFree(GetProcessHeap(), 0, legacySteps);
                }
                CloseHandle(hFile);
                return FALSE;
            }

            steps = (TTPStep*)HeapAlloc(GetProcessHeap(), 0, sizeof(TTPStep) * count);
            if (!steps) {
                if (legacySteps) {
                    HeapFree(GetProcessHeap(), 0, legacySteps);
                }
                CloseHandle(hFile);
                return FALSE;
            }

            for (DWORD i = 0; i < count; i++) {
                __builtin_memset(&steps[i], 0, sizeof(TTPStep));
                steps[i].stepId = legacySteps[i].stepId;
                steps[i].actionType = legacySteps[i].actionType;
                steps[i].targetMode = legacySteps[i].targetMode;
                steps[i].origX = legacySteps[i].origX;
                steps[i].origY = legacySteps[i].origY;
                steps[i].destX = legacySteps[i].destX;
                steps[i].destY = legacySteps[i].destY;
                steps[i].timeoutMs = legacySteps[i].timeoutMs;
                steps[i].postDelayMs = legacySteps[i].postDelayMs;
                __builtin_memcpy(steps[i].textKey, legacySteps[i].textKey, sizeof(steps[i].textKey));
                steps[i].imageOffset = legacySteps[i].imageOffset;
                steps[i].imageSize = legacySteps[i].imageSize;
                steps[i].chromaTol = 0; /* Legacy default: disabled */
                steps[i].reserved[0] = 0;
                steps[i].reserved[1] = 0;
                steps[i].reserved[2] = 0;
            }
            if (legacySteps) {
                HeapFree(GetProcessHeap(), 0, legacySteps);
            }
        } else {
            /* Version 2 and later: read current 176-byte TTPStep */
            DWORD stepsBytes = sizeof(TTPStep) * count;
            steps = (TTPStep*)HeapAlloc(GetProcessHeap(), 0, stepsBytes);
            if (!steps) {
                CloseHandle(hFile);
                return FALSE;
            }

            if (!ReadFile(hFile, steps, stepsBytes, &dwRead, NULL) || dwRead != stepsBytes) {
                if (steps) {
                    HeapFree(GetProcessHeap(), 0, steps);
                }
                CloseHandle(hFile);
                return FALSE;
            }
        }

        bmpBuffers = (BYTE**)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)count * sizeof(BYTE*));
        bmpSizes = (DWORD*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)count * sizeof(DWORD));
        if (!bmpBuffers || !bmpSizes) {
            if (bmpBuffers) {
                HeapFree(GetProcessHeap(), 0, bmpBuffers);
            }
            if (bmpSizes) {
                HeapFree(GetProcessHeap(), 0, bmpSizes);
            }
            if (steps) {
                HeapFree(GetProcessHeap(), 0, steps);
            }
            CloseHandle(hFile);
            return FALSE;
        }

        for (DWORD i = 0; i < count; i++) {
            if (steps[i].imageSize > 0) {
                if (SetFilePointer(hFile, (LONG)steps[i].imageOffset, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
                    ttp_free_project(steps, bmpBuffers, bmpSizes, count);
                    CloseHandle(hFile);
                    return FALSE;
                }

                BYTE* rawBmp = (BYTE*)HeapAlloc(GetProcessHeap(), 0, steps[i].imageSize);
                if (!rawBmp) {
                    ttp_free_project(steps, bmpBuffers, bmpSizes, count);
                    CloseHandle(hFile);
                    return FALSE;
                }

                if (!ReadFile(hFile, rawBmp, steps[i].imageSize, &dwRead, NULL) || dwRead != steps[i].imageSize) {
                    if (rawBmp) {
                        HeapFree(GetProcessHeap(), 0, rawBmp);
                    }
                    ttp_free_project(steps, bmpBuffers, bmpSizes, count);
                    CloseHandle(hFile);
                    return FALSE;
                }

                /* Backward compatibility: auto-promote 24bpp BMP to 32bpp BGRA with A=255 */
                DWORD promotedSize = 0;
                BYTE* promotedBmp = convert_24bpp_to_32bpp(rawBmp, steps[i].imageSize, &promotedSize);
                if (promotedBmp) {
                    if (rawBmp) {
                        HeapFree(GetProcessHeap(), 0, rawBmp);
                    }
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

    CloseHandle(hFile);

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
                HeapFree(GetProcessHeap(), 0, bmpBuffers[i]);
                bmpBuffers[i] = NULL;
            }
        }
        HeapFree(GetProcessHeap(), 0, bmpBuffers);
    }
    if (bmpSizes) {
        HeapFree(GetProcessHeap(), 0, bmpSizes);
    }
    if (steps) {
        HeapFree(GetProcessHeap(), 0, steps);
    }
}
