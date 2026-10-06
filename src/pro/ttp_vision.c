#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <oleacc.h>
#include "ttp_vision.h"

/* Fast 64-bit integer square root without floating point or CRT math.h */
unsigned long ttp_isqrt(unsigned long long n) {
    unsigned long long res = 0;
    unsigned long long bit = 1ULL << 62;
    while (bit > n) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (n >= res + bit) {
            n -= res + bit;
            res = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return (unsigned long)res;
}

/* Fast hardware-based Newton-Raphson double square root without CRT math.h */
double ttp_sqrt(double x) {
    if (x <= 0.0) {
        return 0.0;
    }
    union {
        double d;
        unsigned long long u;
    } conv;
    conv.d = x;
    conv.u = (conv.u >> 1) + 0x1ff0000000000000ULL;
    double y = conv.d;
    y = 0.5 * (y + x / y);
    y = 0.5 * (y + x / y);
    y = 0.5 * (y + x / y);
    y = 0.5 * (y + x / y);
    y = 0.5 * (y + x / y);
    return y;
}

/* =========================================================================
 * 1. Euclidean Distance & Spatial Disambiguation
 * ========================================================================= */

double ttp_calc_euclidean_dist(LONG x1, LONG y1, LONG x2, LONG y2) {
    double dx = (double)(x1 - x2);
    double dy = (double)(y1 - y2);
    return ttp_sqrt(dx * dx + dy * dy);
}

int ttp_pick_nearest_candidate(LONG origX, LONG origY, const POINT* candidates, int count) {
    if (!candidates || count <= 0) {
        return -1;
    }

    int bestIdx = 0;
    double bestDist = ttp_calc_euclidean_dist(origX, origY, candidates[0].x, candidates[0].y);

    for (int i = 1; i < count; i++) {
        double dist = ttp_calc_euclidean_dist(origX, origY, candidates[i].x, candidates[i].y);
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }

    return bestIdx;
}

void ttp_free_bmp_buffer(BYTE* bmpBuffer) {
    if (bmpBuffer) {
        HeapFree(GetProcessHeap(), 0, bmpBuffer);
    }
}

/* =========================================================================
 * 1.5 Automatic Border Chromakey Flood-Fill & Safety Fallback
 * ========================================================================= */

BOOL ttp_chromakey_mask(const BYTE* rgbPixels, int w, int h, int bytesPerPixel, BYTE tol, BYTE* outMask) {
    if (!rgbPixels || !outMask || w <= 0 || h <= 0) {
        return FALSE;
    }
    if (bytesPerPixel != 3 && bytesPerPixel != 4) {
        return FALSE;
    }
    if (tol == 0) {
        tol = 25;
    }

    int totalPixels = w * h;
    /* Initialize mask to all 1s (foreground) */
    __builtin_memset(outMask, 1, (size_t)totalPixels);

    /* 1. Sample 4-border pixels to compute base background color */
    int borderCount = 0;
    double sumB = 0.0, sumG = 0.0, sumR = 0.0;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (x == 0 || x == w - 1 || y == 0 || y == h - 1) {
                const BYTE* p = rgbPixels + (y * w + x) * bytesPerPixel;
                sumB += p[0];
                sumG += p[1];
                sumR += p[2];
                borderCount++;
            }
        }
    }

    if (borderCount == 0) {
        return FALSE;
    }

    double avgB = sumB / (double)borderCount;
    double avgG = sumG / (double)borderCount;
    double avgR = sumR / (double)borderCount;
    int baseB = (int)(avgB + 0.5);
    int baseG = (int)(avgG + 0.5);
    int baseR = (int)(avgR + 0.5);

    /* 2. Compute border color variance / dispersion sigmaB */
    double varSum = 0.0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (x == 0 || x == w - 1 || y == 0 || y == h - 1) {
                const BYTE* p = rgbPixels + (y * w + x) * bytesPerPixel;
                int db = (int)p[0] - baseB;
                int dg = (int)p[1] - baseG;
                int dr = (int)p[2] - baseR;
                varSum += (double)(dr * dr + dg * dg + db * db);
            }
        }
    }

    double sigmaB = ttp_sqrt(varSum / (double)borderCount);
    if (sigmaB > 60.0) {
        /* Safety fallback: border variance too high, revert mask to all 1s */
        return TRUE;
    }

    /* 3. BFS 4-neighbor flood fill starting from border pixels matching base color within tol */
    int* queue = (int*)HeapAlloc(GetProcessHeap(), 0, (size_t)totalPixels * sizeof(int));
    if (!queue) {
        return FALSE;
    }

    int head = 0;
    int tail = 0;
    int tolSq = (int)tol * (int)tol;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            if (x == 0 || x == w - 1 || y == 0 || y == h - 1) {
                int idx = y * w + x;
                const BYTE* p = rgbPixels + idx * bytesPerPixel;
                int db = (int)p[0] - baseB;
                int dg = (int)p[1] - baseG;
                int dr = (int)p[2] - baseR;
                int distSq = dr * dr + dg * dg + db * db;
                if (distSq <= tolSq) {
                    outMask[idx] = 0; /* Mark as transparent background */
                    queue[tail++] = idx;
                }
            }
        }
    }

    static const int dx[4] = { 1, -1, 0, 0 };
    static const int dy[4] = { 0, 0, 1, -1 };

    while (head < tail) {
        int curr = queue[head++];
        int cx = curr % w;
        int cy = curr / w;

        for (int dir = 0; dir < 4; dir++) {
            int nx = cx + dx[dir];
            int ny = cy + dy[dir];
            if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                int nidx = ny * w + nx;
                if (outMask[nidx] == 1) {
                    const BYTE* np = rgbPixels + nidx * bytesPerPixel;
                    int db = (int)np[0] - baseB;
                    int dg = (int)np[1] - baseG;
                    int dr = (int)np[2] - baseR;
                    int distSq = dr * dr + dg * dg + db * db;
                    if (distSq <= tolSq) {
                        outMask[nidx] = 0;
                        queue[tail++] = nidx;
                    }
                }
            }
        }
    }

    if (queue) {
        HeapFree(GetProcessHeap(), 0, queue);
    }

    /* 4. Safety fallback: if foreground ratio < 15%, revert mask to all 1s */
    int fgCount = 0;
    for (int i = 0; i < totalPixels; i++) {
        if (outMask[i] == 1) {
            fgCount++;
        }
    }

    double fgRatio = (double)fgCount / (double)totalPixels;
    if (fgRatio < 0.15) {
        __builtin_memset(outMask, 1, (size_t)totalPixels);
    }

    return TRUE;
}

/* =========================================================================
 * 2. Adaptive Edge Detection & Button Cropping
 * ========================================================================= */

BOOL ttp_adaptive_crop_button(HDC hdcSrc, LONG clickX, LONG clickY, RECT* outRect, BYTE** outBmp, DWORD* outBmpSize) {
    if (!outRect || !outBmp || !outBmpSize) {
        return FALSE;
    }

    HDC hdc = hdcSrc;
    BOOL releaseDC = FALSE;
    if (!hdc) {
        hdc = GetDC(NULL);
        releaseDC = TRUE;
        if (!hdc) return FALSE;
    }

    const int ROI_SIZE = 256;
    const int HALF_ROI = ROI_SIZE / 2; /* 128 */
    int roiLeft = (int)clickX - HALF_ROI;
    int roiTop = (int)clickY - HALF_ROI;

    /* Create compatible DC and 32bpp top-down DIB section for ROI */
    HDC hdcMem = CreateCompatibleDC(hdc);
    if (!hdcMem) {
        if (releaseDC) ReleaseDC(NULL, hdc);
        return FALSE;
    }

    BITMAPINFO bi;
    __builtin_memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = ROI_SIZE;
    bi.bmiHeader.biHeight = -ROI_SIZE; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBmp || !pBits) {
        DeleteDC(hdcMem);
        if (releaseDC) ReleaseDC(NULL, hdc);
        return FALSE;
    }

    HGDIOBJ hOld = SelectObject(hdcMem, hBmp);

    /* Fill background to white in case BitBlt is partially clipped */
    RECT fullRc = { 0, 0, ROI_SIZE, ROI_SIZE };
    FillRect(hdcMem, &fullRc, (HBRUSH)GetStockObject(WHITE_BRUSH));

    if (!BitBlt(hdcMem, 0, 0, ROI_SIZE, ROI_SIZE, hdc, roiLeft, roiTop, SRCCOPY)) {
        SelectObject(hdcMem, hOld);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        if (releaseDC) ReleaseDC(NULL, hdc);
        return FALSE;
    }
    GdiFlush();

    /* 1. Compute grayscale luminance & verify texture variance
     * Note: Allocate 256KB working buffer on process heap to eliminate stack guard-page breach under -mno-stack-arg-probe */
    BYTE* workBuf = (BYTE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)(ROI_SIZE * ROI_SIZE * 4));
    if (!workBuf) {
        SelectObject(hdcMem, hOld);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        if (releaseDC) ReleaseDC(NULL, hdc);
        return FALSE;
    }

    BYTE (*gray)[256]    = (BYTE (*)[256])(workBuf);
    BYTE (*edge)[256]    = (BYTE (*)[256])(workBuf + ROI_SIZE * ROI_SIZE * 1);
    BYTE (*dilated)[256] = (BYTE (*)[256])(workBuf + ROI_SIZE * ROI_SIZE * 2);
    BYTE (*closed)[256]  = (BYTE (*)[256])(workBuf + ROI_SIZE * ROI_SIZE * 3);

    const BYTE* srcPix = (const BYTE*)pBits;
    double sumG = 0.0, sumSqG = 0.0;
    int nG = ROI_SIZE * ROI_SIZE;

    for (int y = 0; y < ROI_SIZE; y++) {
        for (int x = 0; x < ROI_SIZE; x++) {
            const BYTE* px = srcPix + (y * ROI_SIZE + x) * 4;
            BYTE b = px[0];
            BYTE g = px[1];
            BYTE r = px[2];
            int lum = (int)(0.299 * r + 0.587 * g + 0.114 * b + 0.5);
            if (lum < 0) lum = 0;
            if (lum > 255) lum = 255;
            gray[y][x] = (BYTE)lum;
            double dLum = (double)lum;
            sumG += dLum;
            sumSqG += dLum * dLum;
        }
    }

    double varG = (sumSqG - (sumG * sumG) / nG) / nG;
    if (varG <= 1.0) {
        /* Flat uniform surface (pure white/black/solid color) without features */
        HeapFree(GetProcessHeap(), 0, workBuf);
        SelectObject(hdcMem, hOld);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        if (releaseDC) ReleaseDC(NULL, hdc);
        return FALSE;
    }

    /* 2. Compute 3x3 Sobel gradients and binary edge map */
    int cx = HALF_ROI;
    int cy = HALF_ROI;

    int* gradM = (int*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, (size_t)(ROI_SIZE * ROI_SIZE) * sizeof(int));
    if (!gradM) {
        HeapFree(GetProcessHeap(), 0, workBuf);
        SelectObject(hdcMem, hOld);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        if (releaseDC) ReleaseDC(NULL, hdc);
        return FALSE;
    }

    double localGradSum = 0.0;
    int localGradCount = 0;

    for (int y = 1; y < ROI_SIZE - 1; y++) {
        for (int x = 1; x < ROI_SIZE - 1; x++) {
            int gx = -gray[y-1][x-1] + gray[y-1][x+1]
                     - 2 * gray[y][x-1] + 2 * gray[y][x+1]
                     - gray[y+1][x-1] + gray[y+1][x+1];
            int gy = -gray[y-1][x-1] - 2 * gray[y-1][x] - gray[y-1][x+1]
                     + gray[y+1][x-1] + 2 * gray[y+1][x] + gray[y+1][x+1];
            int m = __builtin_abs(gx) + __builtin_abs(gy);
            gradM[y * ROI_SIZE + x] = m;
            if (x >= cx - 35 && x <= cx + 35 && y >= cy - 20 && y <= cy + 20) {
                localGradSum += m;
                localGradCount++;
            }
        }
    }

    int edgeThresh = 30;
    if (localGradCount > 0) {
        int avgLocal = (int)(localGradSum / localGradCount);
        edgeThresh = avgLocal / 2;
        if (edgeThresh < 20) edgeThresh = 20;
        if (edgeThresh > 70) edgeThresh = 70;
    }

    __builtin_memset(edge, 0, (size_t)(ROI_SIZE * ROI_SIZE));
    for (int y = 1; y < ROI_SIZE - 1; y++) {
        for (int x = 1; x < ROI_SIZE - 1; x++) {
            if (gradM[y * ROI_SIZE + x] >= edgeThresh) {
                edge[y][x] = 1;
            }
        }
    }

    /* 3. UIED Morphological Closing:
     * Dilation (3x3 max filter) followed by Erosion (3x3 min filter).
     * Connects discrete character strokes and fragmented button borders. */
    __builtin_memset(dilated, 0, (size_t)(ROI_SIZE * ROI_SIZE));
    for (int y = 1; y < ROI_SIZE - 1; y++) {
        for (int x = 1; x < ROI_SIZE - 1; x++) {
            BYTE v = 0;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (edge[y + dy][x + dx]) { v = 1; break; }
                }
                if (v) break;
            }
            dilated[y][x] = v;
        }
    }

    __builtin_memset(closed, 0, (size_t)(ROI_SIZE * ROI_SIZE));
    for (int y = 2; y < ROI_SIZE - 2; y++) {
        for (int x = 2; x < ROI_SIZE - 2; x++) {
            BYTE v = 1;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    if (!dilated[y + dy][x + dx]) { v = 0; break; }
                }
                if (!v) break;
            }
            closed[y][x] = v;
        }
    }

    /* 4. Click-Seeded Enclosing Barrier Scanning:
     * Scan outwards in 4 directions from center (cx, cy).
     * Distinguishes inner text/icon glyphs (distance < 12-18px) from true enclosing button borders.
     * Stops at the true outer button barrier, preventing bleed into adjacent stacked buttons! */
    int bestTop = cy - 15;
    int firstTop = -1;
    int outerTop = -1;
    int gapTopRows = 0;

    for (int y = cy - 3; y >= cy - 50 && y >= 4; y--) {
        int edgeCount = 0;
        int gradSum = 0;
        for (int x = cx - 25; x <= cx + 25; x++) {
            if (closed[y][x]) edgeCount++;
            gradSum += gradM[y * ROI_SIZE + x];
        }
        if (edgeCount >= 8 || gradSum >= 600) {
            if (firstTop == -1) {
                firstTop = y;
                if (cy - y >= 14) {
                    break; /* Already outer button border */
                }
            } else if (gapTopRows >= 3) {
                outerTop = y;
                break;
            }
        } else {
            if (firstTop != -1) {
                gapTopRows++;
            }
        }
    }
    if (outerTop != -1) bestTop = outerTop;
    else if (firstTop != -1) bestTop = firstTop;

    int bestBottom = cy + 15;
    int firstBottom = -1;
    int outerBottom = -1;
    int gapBottomRows = 0;

    for (int y = cy + 3; y <= cy + 50 && y < ROI_SIZE - 4; y++) {
        int edgeCount = 0;
        int gradSum = 0;
        for (int x = cx - 25; x <= cx + 25; x++) {
            if (closed[y][x]) edgeCount++;
            gradSum += gradM[y * ROI_SIZE + x];
        }
        if (edgeCount >= 8 || gradSum >= 600) {
            if (firstBottom == -1) {
                firstBottom = y;
                if (y - cy >= 14) {
                    break; /* Already outer button border */
                }
            } else if (gapBottomRows >= 3) {
                outerBottom = y;
                break;
            }
        } else {
            if (firstBottom != -1) {
                gapBottomRows++;
            }
        }
    }
    if (outerBottom != -1) bestBottom = outerBottom;
    else if (firstBottom != -1) bestBottom = firstBottom;

    int spanTop = min(bestTop, bestBottom);
    int spanBottom = max(bestTop, bestBottom);
    if (spanBottom - spanTop < 16) {
        spanTop = cy - 8;
        spanBottom = cy + 8;
    }

    int bestLeft = cx - 30;
    int firstLeft = -1;
    int outerLeft = -1;
    int gapLeftCols = 0;

    for (int x = cx - 3; x >= cx - 90 && x >= 4; x--) {
        int edgeCount = 0;
        int gradSum = 0;
        for (int y = spanTop; y <= spanBottom; y++) {
            if (closed[y][x]) edgeCount++;
            gradSum += gradM[y * ROI_SIZE + x];
        }
        int h = spanBottom - spanTop + 1;
        if (edgeCount >= max(4, h / 3) || gradSum >= h * 40) {
            if (firstLeft == -1) {
                firstLeft = x;
                if (cx - x >= 18) {
                    break; /* Already outer button border */
                }
            } else if (gapLeftCols >= 3) {
                outerLeft = x;
                break;
            }
        } else {
            if (firstLeft != -1) {
                gapLeftCols++;
            }
        }
    }
    if (outerLeft != -1) bestLeft = outerLeft;
    else if (firstLeft != -1) bestLeft = firstLeft;

    int bestRight = cx + 30;
    int firstRight = -1;
    int outerRight = -1;
    int gapRightCols = 0;

    for (int x = cx + 3; x <= cx + 90 && x < ROI_SIZE - 4; x++) {
        int edgeCount = 0;
        int gradSum = 0;
        for (int y = spanTop; y <= spanBottom; y++) {
            if (closed[y][x]) edgeCount++;
            gradSum += gradM[y * ROI_SIZE + x];
        }
        int h = spanBottom - spanTop + 1;
        if (edgeCount >= max(4, h / 3) || gradSum >= h * 40) {
            if (firstRight == -1) {
                firstRight = x;
                if (x - cx >= 18) {
                    break; /* Already outer button border */
                }
            } else if (gapRightCols >= 3) {
                outerRight = x;
                break;
            }
        } else {
            if (firstRight != -1) {
                gapRightCols++;
            }
        }
    }
    if (outerRight != -1) bestRight = outerRight;
    else if (firstRight != -1) bestRight = firstRight;

    if (gradM) {
        HeapFree(GetProcessHeap(), 0, gradM);
    }
    if (workBuf) {
        HeapFree(GetProcessHeap(), 0, workBuf);
        workBuf = NULL;
    }

    int L = (bestLeft < bestRight) ? bestLeft : bestRight;
    int R = (bestLeft < bestRight) ? bestRight : bestLeft;
    int T = (bestTop < bestBottom) ? bestTop : bestBottom;
    int B = (bestTop < bestBottom) ? bestBottom : bestTop;

    /* Clamp dimensions: width in [20, 180], height in [16, 56] */
    int width = R - L;
    if (width < 20) {
        int mid = (L + R) / 2;
        L = mid - 10;
        R = mid + 10;
    } else if (width > 180) {
        int mid = (L + R) / 2;
        L = mid - 90;
        R = mid + 90;
    }

    int height = B - T;
    if (height < 16) {
        int mid = (T + B) / 2;
        T = mid - 8;
        B = mid + 8;
    } else if (height > 56) {
        int mid = (T + B) / 2;
        T = mid - 28;
        B = mid + 28;
    }

    /* Prevent extreme aspect ratio (e.g. tall narrow sliver like 22x90) */
    width = R - L;
    height = B - T;
    if (height > width * 1.5) {
        int allowedH = (int)(width * 1.5);
        if (allowedH < 16) allowedH = 16;
        int mid = (T + B) / 2;
        T = mid - allowedH / 2;
        B = mid + allowedH / 2;
    }

    /* Clamp to ROI boundaries [0, 256] */
    if (L < 0) { R -= L; L = 0; }
    if (R > ROI_SIZE) { L -= (R - ROI_SIZE); R = ROI_SIZE; }
    if (L < 0) L = 0;

    if (T < 0) { B -= T; T = 0; }
    if (B > ROI_SIZE) { T -= (B - ROI_SIZE); B = ROI_SIZE; }
    if (T < 0) T = 0;

    int cropW = R - L;
    int cropH = B - T;

    /* Fill outRect in hdcSrc coordinate space */
    outRect->left = roiLeft + L;
    outRect->top = roiTop + T;
    outRect->right = roiLeft + R;
    outRect->bottom = roiTop + B;

    /* Format standard 32bpp uncompressed BMP */
    int rowStride = cropW * 4;
    DWORD imgSize = (DWORD)rowStride * cropH;
    DWORD totalBmpSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + imgSize;

    BYTE* bmpBuf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, totalBmpSize);
    if (!bmpBuf) {
        SelectObject(hdcMem, hOld);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        if (releaseDC) ReleaseDC(NULL, hdc);
        return FALSE;
    }
    __builtin_memset(bmpBuf, 0, totalBmpSize);

    BITMAPFILEHEADER* bmfh = (BITMAPFILEHEADER*)bmpBuf;
    bmfh->bfType = 0x4D42; /* 'BM' */
    bmfh->bfSize = totalBmpSize;
    bmfh->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    BITMAPINFOHEADER* bmih = (BITMAPINFOHEADER*)(bmpBuf + sizeof(BITMAPFILEHEADER));
    bmih->biSize = sizeof(BITMAPINFOHEADER);
    bmih->biWidth = cropW;
    bmih->biHeight = cropH; /* Standard bottom-up */
    bmih->biPlanes = 1;
    bmih->biBitCount = 32;
    bmih->biCompression = BI_RGB;
    bmih->biSizeImage = imgSize;

    BYTE* dstData = bmpBuf + bmfh->bfOffBits;
    for (int y = 0; y < cropH; y++) {
        /* Bottom-up: row 0 in BMP is bottom row (T + cropH - 1 - y) in ROI */
        int srcY = T + (cropH - 1 - y);
        BYTE* dstRow = dstData + y * rowStride;
        for (int x = 0; x < cropW; x++) {
            int srcX = L + x;
            const BYTE* px = srcPix + (srcY * ROI_SIZE + srcX) * 4;
            dstRow[x * 4 + 0] = px[0]; /* B */
            dstRow[x * 4 + 1] = px[1]; /* G */
            dstRow[x * 4 + 2] = px[2]; /* R */
            dstRow[x * 4 + 3] = 255;   /* Foreground default */
        }
    }

    BYTE* mask = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (size_t)cropW * cropH);
    if (mask) {
        if (ttp_chromakey_mask(dstData, cropW, cropH, 4, 25, mask)) {
            for (int i = 0; i < cropW * cropH; i++) {
                dstData[i * 4 + 3] = mask[i] ? 255 : 0;
            }
        }
        if (mask) {
            HeapFree(GetProcessHeap(), 0, mask);
        }
    }

    *outBmp = bmpBuf;
    *outBmpSize = totalBmpSize;

    SelectObject(hdcMem, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    if (releaseDC) ReleaseDC(NULL, hdc);

    return TRUE;
}

BOOL ttp_crop_rect_bmp(HDC hdcSrc, const RECT* cropRect, BYTE** outBmp, DWORD* outBmpSize) {
    if (!cropRect || !outBmp || !outBmpSize) return FALSE;
    *outBmp = NULL;
    *outBmpSize = 0;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int L = cropRect->left;
    int T = cropRect->top;
    int R = cropRect->right;
    int B = cropRect->bottom;

    if (L < 0) L = 0;
    if (T < 0) T = 0;
    if (R > screenW) R = screenW;
    if (B > screenH) B = screenH;

    int cropW = R - L;
    int cropH = B - T;
    if (cropW < 8 || cropH < 8) return FALSE;

    HDC hdc = hdcSrc;
    BOOL releaseDC = FALSE;
    if (!hdc) {
        hdc = GetDC(NULL);
        releaseDC = TRUE;
        if (!hdc) return FALSE;
    }

    HDC hdcMem = CreateCompatibleDC(hdc);
    if (!hdcMem) {
        if (releaseDC) ReleaseDC(NULL, hdc);
        return FALSE;
    }

    BITMAPINFO bi;
    __builtin_memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cropW;
    bi.bmiHeader.biHeight = -cropH; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBmp || !pBits) {
        DeleteDC(hdcMem);
        if (releaseDC) ReleaseDC(NULL, hdc);
        return FALSE;
    }

    HGDIOBJ hOld = SelectObject(hdcMem, hBmp);
    BitBlt(hdcMem, 0, 0, cropW, cropH, hdc, L, T, SRCCOPY);
    GdiFlush();

    int rowStride = cropW * 4;
    DWORD imgSize = (DWORD)rowStride * cropH;
    DWORD totalBmpSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + imgSize;

    BYTE* bmpBuf = (BYTE*)HeapAlloc(GetProcessHeap(), 0, totalBmpSize);
    if (!bmpBuf) {
        SelectObject(hdcMem, hOld);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        if (releaseDC) ReleaseDC(NULL, hdc);
        return FALSE;
    }
    __builtin_memset(bmpBuf, 0, totalBmpSize);

    BITMAPFILEHEADER* bmfh = (BITMAPFILEHEADER*)bmpBuf;
    bmfh->bfType = 0x4D42; /* 'BM' */
    bmfh->bfSize = totalBmpSize;
    bmfh->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    BITMAPINFOHEADER* bmih = (BITMAPINFOHEADER*)(bmpBuf + sizeof(BITMAPFILEHEADER));
    bmih->biSize = sizeof(BITMAPINFOHEADER);
    bmih->biWidth = cropW;
    bmih->biHeight = cropH; /* Standard bottom-up */
    bmih->biPlanes = 1;
    bmih->biBitCount = 32;
    bmih->biCompression = BI_RGB;
    bmih->biSizeImage = imgSize;

    const BYTE* srcPix = (const BYTE*)pBits;
    BYTE* dstData = bmpBuf + bmfh->bfOffBits;
    for (int y = 0; y < cropH; y++) {
        int srcY = cropH - 1 - y; /* invert for bottom-up BMP */
        BYTE* dstRow = dstData + y * rowStride;
        for (int x = 0; x < cropW; x++) {
            const BYTE* px = srcPix + (srcY * cropW + x) * 4;
            dstRow[x * 4 + 0] = px[0]; /* B */
            dstRow[x * 4 + 1] = px[1]; /* G */
            dstRow[x * 4 + 2] = px[2]; /* R */
            dstRow[x * 4 + 3] = 255;   /* Default foreground */
        }
    }

    BYTE* mask = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (size_t)cropW * cropH);
    if (mask) {
        if (ttp_chromakey_mask(dstData, cropW, cropH, 4, 25, mask)) {
            for (int i = 0; i < cropW * cropH; i++) {
                dstData[i * 4 + 3] = mask[i] ? 255 : 0;
            }
        }
        HeapFree(GetProcessHeap(), 0, mask);
    }

    *outBmp = bmpBuf;
    *outBmpSize = totalBmpSize;

    SelectObject(hdcMem, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    if (releaseDC) ReleaseDC(NULL, hdc);

    return TRUE;
}

/* =========================================================================
 * 3. Masked Cascaded NCC Template Matcher (Zero-Heap BSS & Probe SAD)
 * ========================================================================= */

#define TTP_SCREEN_GRAY_MAX (3840 * 2160)
#define TTP_TEMPLATE_PIXELS_MAX (512 * 512)

typedef struct {
    int dx;
    int dy;
    double t_norm; /* T_i - mu_T */
} TTPMaskedPixel;

typedef struct {
    BYTE screenGray[TTP_SCREEN_GRAY_MAX];
    BYTE templateGray[TTP_TEMPLATE_PIXELS_MAX];
    BYTE templateMask[TTP_TEMPLATE_PIXELS_MAX];
    TTPMaskedPixel maskedPixels[TTP_TEMPLATE_PIXELS_MAX];
} TTPVisionBuffers;

static TTPVisionBuffers* s_visBuf = NULL;
static TTPVisionBuffers* get_vision_buffers(void) {
    if (!s_visBuf) {
        s_visBuf = (TTPVisionBuffers*)VirtualAlloc(NULL, sizeof(TTPVisionBuffers), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    }
    return s_visBuf;
}


typedef struct {
    int dx;
    int dy;
    BYTE val;
} TTPProbePoint;

/* Helper: Capture a rectangular HDC region directly into 8bpp grayscale buffer without heap allocation */
static BOOL capture_hdc_to_gray(HDC hdcSrc, int srcX, int srcY, int w, int h, BYTE* outGray) {
    if (!hdcSrc || !outGray || w <= 0 || h <= 0) return FALSE;

    HDC hdcMem = CreateCompatibleDC(hdcSrc);
    if (!hdcMem) return FALSE;

    BITMAPINFO bi;
    __builtin_memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pBits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hBmp || !pBits) {
        DeleteDC(hdcMem);
        return FALSE;
    }

    HGDIOBJ hOld = SelectObject(hdcMem, hBmp);
    if (!BitBlt(hdcMem, 0, 0, w, h, hdcSrc, srcX, srcY, SRCCOPY)) {
        SelectObject(hdcMem, hOld);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        return FALSE;
    }
    GdiFlush();

    const BYTE* srcPx = (const BYTE*)pBits;
    int total = w * h;
    for (int i = 0; i < total; i++) {
        const BYTE* px = srcPx + i * 4;
        outGray[i] = (BYTE)((px[2] * 77 + px[1] * 150 + px[0] * 29 + 128) >> 8);
    }

    SelectObject(hdcMem, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    return TRUE;
}

/* Precompute up to 16 feature probe points (dx_k, dy_k) where mask is 1 and contrast/gradient is high */
static int select_probe_points(const BYTE* tGray, const BYTE* tMask, int tw, int th, double muT, TTPProbePoint* outProbes, int maxProbes) {
    if (!tGray || !tMask || !outProbes || maxProbes <= 0 || tw <= 0 || th <= 0) return 0;

    int probeCount = 0;

    /* Pass 1: 4x4 spatial grid partition across template to ensure wide dispersion */
    int gridX = (tw >= 4) ? 4 : tw;
    int gridY = (th >= 4) ? 4 : th;

    for (int gy = 0; gy < gridY && probeCount < maxProbes; gy++) {
        int y0 = gy * th / gridY;
        int y1 = (gy + 1) * th / gridY;
        for (int gx = 0; gx < gridX && probeCount < maxProbes; gx++) {
            int x0 = gx * tw / gridX;
            int x1 = (gx + 1) * tw / gridX;

            int bestX = -1, bestY = -1;
            int bestScore = -1;
            int wantHigh = ((gy + gx) % 2 == 0);

            for (int pass = 0; pass < 2 && bestX < 0; pass++) {
                for (int y = y0; y < y1; y++) {
                    for (int x = x0; x < x1; x++) {
                        int idx = y * tw + x;
                        if (!tMask[idx]) continue;
                        if (pass == 0) {
                            if (wantHigh && (int)tGray[idx] < (int)(muT + 0.5)) continue;
                            if (!wantHigh && (int)tGray[idx] >= (int)(muT + 0.5)) continue;
                        }

                        int grad = 0;
                        if (x > 0 && tMask[idx - 1]) grad += __builtin_abs((int)tGray[idx] - (int)tGray[idx - 1]);
                        if (x < tw - 1 && tMask[idx + 1]) grad += __builtin_abs((int)tGray[idx] - (int)tGray[idx + 1]);
                        if (y > 0 && tMask[idx - tw]) grad += __builtin_abs((int)tGray[idx] - (int)tGray[idx - tw]);
                        if (y < th - 1 && tMask[idx + tw]) grad += __builtin_abs((int)tGray[idx] - (int)tGray[idx + tw]);

                        int contrast = __builtin_abs((int)tGray[idx] - (int)(muT + 0.5));
                        int score = grad * 2 + contrast;

                        if (score > bestScore) {
                            bestScore = score;
                            bestX = x;
                            bestY = y;
                        }
                    }
                }
            }

            if (bestX >= 0) {
                outProbes[probeCount].dx = bestX;
                outProbes[probeCount].dy = bestY;
                outProbes[probeCount].val = tGray[bestY * tw + bestX];
                probeCount++;
            }
        }
    }

    /* Pass 2: Fill remaining slots up to maxProbes from other high-contrast masked pixels */
    if (probeCount < maxProbes) {
        while (probeCount < maxProbes) {
            int highestScore = -1;
            int bestX = -1, bestY = -1;

            for (int y = 0; y < th; y++) {
                for (int x = 0; x < tw; x++) {
                    int idx = y * tw + x;
                    if (!tMask[idx]) continue;

                    BOOL tooClose = FALSE;
                    for (int p = 0; p < probeCount; p++) {
                        if (__builtin_abs(outProbes[p].dx - x) <= 1 && __builtin_abs(outProbes[p].dy - y) <= 1) {
                            tooClose = TRUE;
                            break;
                        }
                    }
                    if (tooClose) continue;

                    int grad = 0;
                    if (x > 0 && tMask[idx - 1]) grad += __builtin_abs((int)tGray[idx] - (int)tGray[idx - 1]);
                    if (x < tw - 1 && tMask[idx + 1]) grad += __builtin_abs((int)tGray[idx] - (int)tGray[idx + 1]);
                    if (y > 0 && tMask[idx - tw]) grad += __builtin_abs((int)tGray[idx] - (int)tGray[idx - tw]);
                    if (y < th - 1 && tMask[idx + tw]) grad += __builtin_abs((int)tGray[idx] - (int)tGray[idx + tw]);
                    int contrast = __builtin_abs((int)tGray[idx] - (int)(muT + 0.5));
                    int score = grad * 2 + contrast;

                    if (score > highestScore) {
                        highestScore = score;
                        bestX = x;
                        bestY = y;
                    }
                }
            }

            if (bestX < 0) break;

            outProbes[probeCount].dx = bestX;
            outProbes[probeCount].dy = bestY;
            outProbes[probeCount].val = tGray[bestY * tw + bestX];
            probeCount++;
        }
    }

    return probeCount;
}

/* Core matcher operating directly on an 8bpp grayscale buffer */
static BOOL match_gray_buffer_masked_ncc(
    const BYTE* grayBuf, int imgW, int imgH,
    const BYTE* bmpPattern, DWORD bmpSize,
    double minScore,
    int* outBestX, int* outBestY, double* outBestScore)
{
    if (!grayBuf || !bmpPattern || bmpSize < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) {
        if (outBestScore) *outBestScore = 0.0;
        return FALSE;
    }

    const BITMAPFILEHEADER* bmfh = (const BITMAPFILEHEADER*)bmpPattern;
    if (bmfh->bfType != 0x4D42) {
        if (outBestScore) *outBestScore = 0.0;
        return FALSE;
    }

    const BITMAPINFOHEADER* bmih = (const BITMAPINFOHEADER*)(bmpPattern + sizeof(BITMAPFILEHEADER));
    int tw = bmih->biWidth;
    int th = __builtin_abs(bmih->biHeight);
    BOOL isBottomUp = (bmih->biHeight > 0);
    int bpp = bmih->biBitCount;

    if (tw <= 0 || th <= 0 || (bpp != 24 && bpp != 32)) {
        if (outBestScore) *outBestScore = 0.0;
        return FALSE;
    }

    if (imgW < tw || imgH < th) {
        if (outBestScore) *outBestScore = 0.0;
        return FALSE;
    }

    int N = tw * th;
    TTPVisionBuffers* vb = get_vision_buffers(); BYTE* tGray = vb ? vb->templateGray : NULL;
    BYTE* tMask = vb ? vb->templateMask : NULL;
    TTPMaskedPixel* mPixels = vb ? vb->maskedPixels : NULL;
    BOOL dynTemplate = FALSE;

    if (N > TTP_TEMPLATE_PIXELS_MAX) {
        tGray = (BYTE*)HeapAlloc(GetProcessHeap(), 0, N);
        tMask = (BYTE*)HeapAlloc(GetProcessHeap(), 0, N);
        mPixels = (TTPMaskedPixel*)HeapAlloc(GetProcessHeap(), 0, N * sizeof(TTPMaskedPixel));
        dynTemplate = TRUE;
        if (!tGray || !tMask || !mPixels) {
            if (tGray) { HeapFree(GetProcessHeap(), 0, tGray); }
            if (tMask) { HeapFree(GetProcessHeap(), 0, tMask); }
            if (mPixels) { HeapFree(GetProcessHeap(), 0, mPixels); }
            if (outBestScore) *outBestScore = 0.0;
            return FALSE;
        }
    }

    int tStride = (bpp == 32) ? (tw * 4) : (((tw * 3 + 3) / 4) * 4);
    const BYTE* tPixels = bmpPattern + bmfh->bfOffBits;
    if (bmfh->bfOffBits >= bmpSize) {
        if (dynTemplate) {
            if (tGray) { HeapFree(GetProcessHeap(), 0, tGray); }
            if (tMask) { HeapFree(GetProcessHeap(), 0, tMask); }
            if (mPixels) { HeapFree(GetProcessHeap(), 0, mPixels); }
        }
        if (outBestScore) *outBestScore = 0.0;
        return FALSE;
    }

    int Nm = 0;
    double sumT = 0.0;

    for (int y = 0; y < th; y++) {
        int bmpRow = isBottomUp ? (th - 1 - y) : y;
        const BYTE* row = tPixels + bmpRow * tStride;
        for (int x = 0; x < tw; x++) {
            int idx = y * tw + x;
            BYTE gray;
            BYTE m = 1;
            if (bpp == 32) {
                BYTE b = row[x * 4 + 0];
                BYTE g = row[x * 4 + 1];
                BYTE r = row[x * 4 + 2];
                BYTE a = row[x * 4 + 3];
                gray = (BYTE)((r * 77 + g * 150 + b * 29 + 128) >> 8);
                m = (a > 128) ? 1 : 0;
            } else {
                BYTE b = row[x * 3 + 0];
                BYTE g = row[x * 3 + 1];
                BYTE r = row[x * 3 + 2];
                gray = (BYTE)((r * 77 + g * 150 + b * 29 + 128) >> 8);
                m = 1;
            }
            tGray[idx] = gray;
            tMask[idx] = m;
            if (m) {
                Nm++;
                sumT += gray;
            }
        }
    }

    BOOL isMaskedTemplate = (Nm >= 16 && Nm < N);

    /* Fall back to standard unmasked template if foreground is too small or covers whole pattern */
    if (!isMaskedTemplate) {
        __builtin_memset(tMask, 1, N);
        Nm = N;
        sumT = 0.0;
        for (int i = 0; i < N; i++) {
            sumT += tGray[i];
        }
    }

    double muT = sumT / (double)Nm;
    double sigmaT2 = 0.0;
    int mCount = 0;

    for (int y = 0; y < th; y++) {
        for (int x = 0; x < tw; x++) {
            int idx = y * tw + x;
            if (tMask[idx]) {
                double d = (double)tGray[idx] - muT;
                sigmaT2 += d * d;
                mPixels[mCount].dx = x;
                mPixels[mCount].dy = y;
                mPixels[mCount].t_norm = d;
                mCount++;
            }
        }
    }

    double varPerPixelT = sigmaT2 / (double)Nm;
    BOOL isFlatVector = isMaskedTemplate && (varPerPixelT < 2.0);

    if (sigmaT2 < 1e-6) {
        if (!isFlatVector || Nm < 4) {
            if (dynTemplate) {
                if (tGray) { HeapFree(GetProcessHeap(), 0, tGray); }
                if (tMask) { HeapFree(GetProcessHeap(), 0, tMask); }
                if (mPixels) { HeapFree(GetProcessHeap(), 0, mPixels); }
            }
            if (outBestScore) *outBestScore = 0.0;
            return FALSE;
        }
        sigmaT2 = 1.0; /* Use artificial variance for flat vector matcher */
    }

    TTPProbePoint probes[16];
    int numProbes = select_probe_points(tGray, tMask, tw, th, muT, probes, 16);
    if (numProbes == 0) {
        probes[0].dx = mPixels[0].dx;
        probes[0].dy = mPixels[0].dy;
        probes[0].val = tGray[mPixels[0].dy * tw + mPixels[0].dx];
        numProbes = 1;
    }

    int maxX = imgW - tw;
    int maxY = imgH - th;
    int bestX = 0;
    int bestY = 0;
    double bestScore = -2.0;

    int step = 1;
    if ((long long)maxX * (long long)maxY > 500000LL) {
        step = 2;
    }

    BOOL skipProbeSAD = ((long long)maxX * (long long)maxY <= 2500LL);
    int maxAllowedSad = (minScore <= 0.60) ? (numProbes * 95) : (numProbes * 60);

    typedef struct {
        int x;
        int y;
        double score;
    } CoarseCand;
    CoarseCand topCands[8];
    int topCandCount = 0;

    for (int y = 0; y <= maxY; y += step) {
        for (int x = 0; x <= maxX; x += step) {
            /* Tier 1: Probe SAD coarse filtering (bypassed for localized small ROI searches) */
            if (!skipProbeSAD) {
                int sadSum = 0;
                BOOL passedProbe = TRUE;
                for (int k = 0; k < numProbes; k++) {
                    int scrVal = grayBuf[(y + probes[k].dy) * imgW + (x + probes[k].dx)];
                    sadSum += __builtin_abs(scrVal - (int)probes[k].val);
                    if (sadSum > maxAllowedSad) {
                        passedProbe = FALSE;
                        break;
                    }
                }
                if (!passedProbe) continue;
            }

            /* Tier 2: Masked NCC */
            double sumI = 0.0;
            double sumI2 = 0.0;
            double cov = 0.0;
            for (int k = 0; k < Nm; k++) {
                int pxX = x + mPixels[k].dx;
                int pxY = y + mPixels[k].dy;
                double val = (double)grayBuf[pxY * imgW + pxX];
                sumI += val;
                sumI2 += val * val;
                cov += mPixels[k].t_norm * val;
            }

            double varI = sumI2 - (sumI * sumI) / (double)Nm;
            if (varI <= 25.0) {
                if (isFlatVector) {
                    double muI = sumI / (double)Nm;
                    double diff = (muT > muI) ? (muT - muI) : (muI - muT);
                    if (diff <= 15.0) {
                        double flatScore = 1.0 - (diff / 60.0);
                        if (flatScore > bestScore) {
                            bestScore = flatScore;
                            bestX = x;
                            bestY = y;
                        }
                    }
                }
                continue;
            }

            double denom = ttp_sqrt(sigmaT2 * varI);
            if (denom < 1e-9) continue;

            double score = cov / denom;
            if (isFlatVector && score > 0.40) {
                score = 0.40;
            }
            if (score > 1.0) score = 1.0;

            if (score > bestScore) {
                bestScore = score;
                bestX = x;
                bestY = y;
            }

            if (step > 1 && score >= 0.35) {
                int existing = -1;
                int distThresh = (tw > th) ? (th / 2) : (tw / 2);
                if (distThresh < 4) distThresh = 4;
                for (int c = 0; c < topCandCount; c++) {
                    if (__builtin_abs(topCands[c].x - x) <= distThresh && __builtin_abs(topCands[c].y - y) <= distThresh) {
                        existing = c;
                        break;
                    }
                }
                if (existing >= 0) {
                    if (score > topCands[existing].score) {
                        topCands[existing].x = x;
                        topCands[existing].y = y;
                        topCands[existing].score = score;
                    }
                } else if (topCandCount < 8) {
                    topCands[topCandCount].x = x;
                    topCands[topCandCount].y = y;
                    topCands[topCandCount].score = score;
                    topCandCount++;
                } else {
                    int minIdx = 0;
                    for (int c = 1; c < topCandCount; c++) {
                        if (topCands[c].score < topCands[minIdx].score) minIdx = c;
                    }
                    if (score > topCands[minIdx].score) {
                        topCands[minIdx].x = x;
                        topCands[minIdx].y = y;
                        topCands[minIdx].score = score;
                    }
                }
            }
        }
    }

    /* Fine refinement pass if coarse step > 1 */
    if (step > 1 && bestScore > 0.20) {
        if (topCandCount == 0) {
            topCands[0].x = bestX;
            topCands[0].y = bestY;
            topCands[0].score = bestScore;
            topCandCount = 1;
        }

        for (int c = 0; c < topCandCount; c++) {
            int cx = topCands[c].x;
            int cy = topCands[c].y;
            int fx0 = (cx - 4 < 0) ? 0 : cx - 4;
            int fx1 = (cx + 4 > maxX) ? maxX : cx + 4;
            int fy0 = (cy - 4 < 0) ? 0 : cy - 4;
            int fy1 = (cy + 4 > maxY) ? maxY : cy + 4;

            for (int y = fy0; y <= fy1; y++) {
                for (int x = fx0; x <= fx1; x++) {
                    double sumI = 0.0;
                    double sumI2 = 0.0;
                    double cov = 0.0;
                    for (int k = 0; k < Nm; k++) {
                        int pxX = x + mPixels[k].dx;
                        int pxY = y + mPixels[k].dy;
                        double val = (double)grayBuf[pxY * imgW + pxX];
                        sumI += val;
                        sumI2 += val * val;
                        cov += mPixels[k].t_norm * val;
                    }

                    double varI = sumI2 - (sumI * sumI) / (double)Nm;
                    if (varI <= 25.0) {
                        if (isFlatVector) {
                            double muI = sumI / (double)Nm;
                            double diff = (muT > muI) ? (muT - muI) : (muI - muT);
                            if (diff <= 15.0) {
                                double flatScore = 1.0 - (diff / 60.0);
                                if (flatScore > bestScore) {
                                    bestScore = flatScore;
                                    bestX = x;
                                    bestY = y;
                                }
                            }
                        }
                        continue;
                    }

                    double denom = ttp_sqrt(sigmaT2 * varI);
                    if (denom < 1e-9) continue;

                    double score = cov / denom;
                    if (isFlatVector && score > 0.40) {
                        score = 0.40;
                    }
                    if (score > 1.0) score = 1.0;

                    if (score > bestScore) {
                        bestScore = score;
                        bestX = x;
                        bestY = y;
                    }
                }
            }
        }
    }

    if (dynTemplate) {
        if (tGray) { HeapFree(GetProcessHeap(), 0, tGray); }
        if (tMask) { HeapFree(GetProcessHeap(), 0, tMask); }
        if (mPixels) { HeapFree(GetProcessHeap(), 0, mPixels); }
    }

    if (outBestX) *outBestX = bestX;
    if (outBestY) *outBestY = bestY;
    if (outBestScore) *outBestScore = (bestScore < -1.0) ? 0.0 : bestScore;

    return (bestScore >= minScore);
}

BOOL ttp_match_template_masked_ncc(HDC hdcScreen, int screenW, int screenH, const BYTE* bmpPattern, DWORD bmpSize, double minScore, POINT* outMatchPos, double* outScore) {
    if (!bmpPattern || bmpSize < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) {
        if (outScore) *outScore = 0.0;
        return FALSE;
    }

    const BITMAPINFOHEADER* bmih = (const BITMAPINFOHEADER*)(bmpPattern + sizeof(BITMAPFILEHEADER));
    int tw = bmih->biWidth;
    int th = __builtin_abs(bmih->biHeight);
    if (tw <= 0 || th <= 0) {
        if (outScore) *outScore = 0.0;
        return FALSE;
    }

    HDC hdc = hdcScreen;
    BOOL releaseDC = FALSE;
    if (!hdc) {
        hdc = GetDC(NULL);
        releaseDC = TRUE;
        if (!hdc) {
            if (outScore) *outScore = 0.0;
            return FALSE;
        }
    }

    if (screenW <= 0 || screenH <= 0) {
        HGDIOBJ hCurBmp = GetCurrentObject(hdc, OBJ_BITMAP);
        BITMAP bm;
        if (hCurBmp && GetObject(hCurBmp, sizeof(BITMAP), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
            screenW = bm.bmWidth;
            screenH = bm.bmHeight;
        }
        if (screenW <= 0) screenW = GetSystemMetrics(SM_CXSCREEN);
        if (screenH <= 0) screenH = GetSystemMetrics(SM_CYSCREEN);
    }

    if (screenW < tw || screenH < th) {
        if (releaseDC) ReleaseDC(NULL, hdc);
        if (outScore) *outScore = 0.0;
        return FALSE;
    }

    /* Fast ROI match if coordinate hint provided (<5ms) */
    if (outMatchPos && (outMatchPos->x > 0 || outMatchPos->y > 0)) {
        POINT roiPt = *outMatchPos;
        double roiScore = 0.0;
        if (ttp_match_template_masked_ncc_roi(hdc, roiPt.x, roiPt.y, 250, bmpPattern, bmpSize, minScore, &roiPt, &roiScore)) {
            *outMatchPos = roiPt;
            if (outScore) *outScore = roiScore;
            if (releaseDC) ReleaseDC(NULL, hdc);
            return TRUE;
        }
    }

    TTPVisionBuffers* vb = get_vision_buffers(); BYTE* scrGray = vb ? vb->screenGray : NULL;
    BOOL dynScreen = FALSE;
    if ((size_t)screenW * (size_t)screenH > TTP_SCREEN_GRAY_MAX) {
        scrGray = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (size_t)screenW * (size_t)screenH);
        dynScreen = TRUE;
        if (!scrGray) {
            if (releaseDC) ReleaseDC(NULL, hdc);
            if (outScore) *outScore = 0.0;
            return FALSE;
        }
    }

    if (!capture_hdc_to_gray(hdc, 0, 0, screenW, screenH, scrGray)) {
        if (dynScreen && scrGray) {
            HeapFree(GetProcessHeap(), 0, scrGray);
        }
        if (releaseDC) ReleaseDC(NULL, hdc);
        if (outScore) *outScore = 0.0;
        return FALSE;
    }
    if (releaseDC) {
        ReleaseDC(NULL, hdc);
        releaseDC = FALSE;
    }

    int bestX = 0, bestY = 0;
    double bestScore = 0.0;
    BOOL matched = match_gray_buffer_masked_ncc(scrGray, screenW, screenH, bmpPattern, bmpSize, minScore, &bestX, &bestY, &bestScore);

    if (dynScreen && scrGray) {
        HeapFree(GetProcessHeap(), 0, scrGray);
    }

    if (outScore) *outScore = bestScore;
    if (outMatchPos && bestScore > 0.0) {
        outMatchPos->x = bestX + tw / 2;
        outMatchPos->y = bestY + th / 2;
    }

    return matched;
}

BOOL ttp_match_template_masked_ncc_roi(HDC hdcScreen, int roiX, int roiY, int roiRadius, const BYTE* bmpPattern, DWORD bmpSize, double minScore, POINT* outMatchPos, double* outScore) {
    if (!bmpPattern || bmpSize < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) {
        if (outScore) *outScore = 0.0;
        return FALSE;
    }
    if (roiRadius <= 0) roiRadius = 200;

    const BITMAPINFOHEADER* bmih = (const BITMAPINFOHEADER*)(bmpPattern + sizeof(BITMAPFILEHEADER));
    int tw = bmih->biWidth;
    int th = __builtin_abs(bmih->biHeight);
    if (tw <= 0 || th <= 0) {
        if (outScore) *outScore = 0.0;
        return FALSE;
    }

    HDC hdc = hdcScreen;
    BOOL releaseDC = FALSE;
    if (!hdc) {
        hdc = GetDC(NULL);
        releaseDC = TRUE;
        if (!hdc) {
            if (outScore) *outScore = 0.0;
            return FALSE;
        }
    }

    int screenW = 0, screenH = 0;
    HGDIOBJ hCurBmp = GetCurrentObject(hdc, OBJ_BITMAP);
    BITMAP bm;
    if (hCurBmp && GetObject(hCurBmp, sizeof(BITMAP), &bm) && bm.bmWidth > 0 && bm.bmHeight > 0) {
        screenW = bm.bmWidth;
        screenH = bm.bmHeight;
    }
    if (screenW <= 0) screenW = GetSystemMetrics(SM_CXSCREEN);
    if (screenH <= 0) screenH = GetSystemMetrics(SM_CYSCREEN);

    int x0 = roiX - roiRadius; if (x0 < 0) x0 = 0;
    int y0 = roiY - roiRadius; if (y0 < 0) y0 = 0;
    int x1 = roiX + roiRadius; if (x1 > screenW) x1 = screenW;
    int y1 = roiY + roiRadius; if (y1 > screenH) y1 = screenH;

    int roiW = x1 - x0;
    int roiH = y1 - y0;
    if (roiW < tw || roiH < th) {
        if (releaseDC) ReleaseDC(NULL, hdc);
        if (outScore) *outScore = 0.0;
        return FALSE;
    }

    TTPVisionBuffers* vb = get_vision_buffers(); BYTE* scrGray = vb ? vb->screenGray : NULL;
    BOOL dynScreen = FALSE;
    if ((size_t)roiW * (size_t)roiH > TTP_SCREEN_GRAY_MAX) {
        scrGray = (BYTE*)HeapAlloc(GetProcessHeap(), 0, (size_t)roiW * (size_t)roiH);
        dynScreen = TRUE;
        if (!scrGray) {
            if (releaseDC) ReleaseDC(NULL, hdc);
            if (outScore) *outScore = 0.0;
            return FALSE;
        }
    }

    if (!capture_hdc_to_gray(hdc, x0, y0, roiW, roiH, scrGray)) {
        if (dynScreen && scrGray) {
            HeapFree(GetProcessHeap(), 0, scrGray);
        }
        if (releaseDC) ReleaseDC(NULL, hdc);
        if (outScore) *outScore = 0.0;
        return FALSE;
    }
    if (releaseDC) ReleaseDC(NULL, hdc);

    int bestX = 0, bestY = 0;
    double bestScore = 0.0;
    BOOL matched = match_gray_buffer_masked_ncc(scrGray, roiW, roiH, bmpPattern, bmpSize, minScore, &bestX, &bestY, &bestScore);

    if (dynScreen && scrGray) {
        HeapFree(GetProcessHeap(), 0, scrGray);
    }

    if (outScore) *outScore = bestScore;
    if (outMatchPos && bestScore > 0.0) {
        outMatchPos->x = x0 + bestX + tw / 2;
        outMatchPos->y = y0 + bestY + th / 2;
    }

    return matched;
}

BOOL ttp_match_template_ncc(HDC hdcScreen, int screenW, int screenH, const BYTE* bmpPattern, DWORD bmpSize, double minScore, POINT* outMatchPos, double* outScore) {
    return ttp_match_template_masked_ncc(hdcScreen, screenW, screenH, bmpPattern, bmpSize, minScore, outMatchPos, outScore);
}

BOOL ttp_match_template_ncc_roi(HDC hdcScreen, int roiX, int roiY, int roiRadius, const BYTE* bmpPattern, DWORD bmpSize, double minScore, POINT* outMatchPos, double* outScore) {
    return ttp_match_template_masked_ncc_roi(hdcScreen, roiX, roiY, roiRadius, bmpPattern, bmpSize, minScore, outMatchPos, outScore);
}

BOOL ttp_get_accessible_element_at_point(POINT pt, char* outName, int maxLen, RECT* outRect) {
    if (outName && maxLen > 0) outName[0] = '\0';
    if (outRect) __builtin_memset(outRect, 0, sizeof(RECT));

    CoInitialize(NULL);
    IAccessible* pAcc = NULL;
    VARIANT varChild;
    VariantInit(&varChild);

    BOOL found = FALSE;
    HRESULT hr = AccessibleObjectFromPoint(pt, &pAcc, &varChild);
    if (SUCCEEDED(hr) && pAcc) {
        if (outName && maxLen > 0) {
            BSTR bstrName = NULL;
            pAcc->lpVtbl->get_accName(pAcc, varChild, &bstrName);
            if (bstrName) {
                WideCharToMultiByte(CP_ACP, 0, bstrName, -1, outName, maxLen - 1, NULL, NULL);
                outName[maxLen - 1] = '\0';
                SysFreeString(bstrName);
            }
        }
        if (outRect) {
            long x = 0, y = 0, w = 0, h = 0;
            if (SUCCEEDED(pAcc->lpVtbl->accLocation(pAcc, &x, &y, &w, &h, varChild))) {
                if (w > 0 && h > 0) {
                    outRect->left = (LONG)x;
                    outRect->top = (LONG)y;
                    outRect->right = (LONG)(x + w);
                    outRect->bottom = (LONG)(y + h);
                }
            }
        }
        found = (outName && outName[0] != '\0') || (outRect && (outRect->right > outRect->left));
        VariantClear(&varChild);
        pAcc->lpVtbl->Release(pAcc);
    }
    CoUninitialize();
    return found;
}

BOOL ttp_get_accessible_name_at_point(POINT pt, char* outName, int maxLen) {
    return ttp_get_accessible_element_at_point(pt, outName, maxLen, NULL);
}

/* =========================================================================
 * 4. Control Text Locator Helper
 * ========================================================================= */

typedef struct {
    const char* targetText;
    POINT* outCenters;
    int maxCount;
    int count;
} TextSearchContext;

static BOOL text_matches(const char* haystack, const char* needle) {
    if (!haystack || !needle) return FALSE;
    int nlen = lstrlenA(needle);
    int hlen = lstrlenA(haystack);
    if (nlen == 0 || hlen < nlen) return FALSE;
    for (int i = 0; i <= hlen - nlen; i++) {
        BOOL match = TRUE;
        for (int j = 0; j < nlen; j++) {
            char c1 = haystack[i + j];
            char c2 = needle[j];
            if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + ('a' - 'A'));
            if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + ('a' - 'A'));
            if (c1 != c2) {
                match = FALSE;
                break;
            }
        }
        if (match) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL check_and_add_window(HWND hwnd, TextSearchContext* ctx) {
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return TRUE;
    if (IsIconic(hwnd)) return TRUE; /* Minimized window (-32000, -32000) must be ignored! */

    RECT rc;
    if (!GetWindowRect(hwnd, &rc)) return TRUE;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    /* Reject off-screen, minimized, or degenerate rects */
    if (rc.left < -1000 || rc.top < -1000 || rc.right <= 0 || rc.bottom <= 0 ||
        rc.left >= screenW || rc.top >= screenH ||
        rc.right - rc.left <= 2 || rc.bottom - rc.top <= 2) {
        return TRUE;
    }

    /* Reject top-level container windows or giant panels:
     * Buttons, icons, and clickable controls are compact elements (width <= 360, height <= 180).
     * Entire application main windows or large container panels must never be added as click targets! */
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;
    HWND hParent = GetParent(hwnd);
    HWND hRoot = GetAncestor(hwnd, GA_ROOT);
    BOOL isTopLevel = (hParent == NULL || hRoot == hwnd);

    if (isTopLevel || winW > 360 || winH > 180) {
        // This is a container / top-level window. Do NOT add its center as a click target.
        // Return TRUE so EnumWindows/EnumChildWindows continues down to its child controls.
        return TRUE;
    }

    POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
    if (pt.x < 0 || pt.y < 0 || pt.x >= screenW || pt.y >= screenH) return TRUE;

    char buf[512] = {0};
    if (GetWindowTextA(hwnd, buf, sizeof(buf)) <= 0) {
        SendMessageTimeoutA(hwnd, WM_GETTEXT, sizeof(buf), (LPARAM)buf, SMTO_ABORTIFHUNG, 50, NULL);
    }

    if (buf[0] != '\0' && text_matches(buf, ctx->targetText)) {
        BOOL dup = FALSE;
        for (int i = 0; i < ctx->count; i++) {
            if (ctx->outCenters[i].x == pt.x && ctx->outCenters[i].y == pt.y) {
                dup = TRUE;
                break;
            }
        }
        if (!dup && ctx->count < ctx->maxCount) {
            ctx->outCenters[ctx->count++] = pt;
        }
    }
    return (ctx->count < ctx->maxCount);
}

static void check_desktop_icons_accessible(const char* targetText, TextSearchContext* ctx) {
    if (!targetText || targetText[0] == '\0') return;

    HWND hProgman = FindWindowA("Progman", NULL);
    HWND hDefView = NULL;
    if (hProgman) {
        hDefView = FindWindowExA(hProgman, NULL, "SHELLDLL_DefView", NULL);
    }
    if (!hDefView) {
        HWND hWorkerW = NULL;
        while ((hWorkerW = FindWindowExA(NULL, hWorkerW, "WorkerW", NULL)) != NULL) {
            hDefView = FindWindowExA(hWorkerW, NULL, "SHELLDLL_DefView", NULL);
            if (hDefView) break;
        }
    }
    if (!hDefView) return;
    HWND hLV = FindWindowExA(hDefView, NULL, "SysListView32", NULL);
    if (!hLV) return;

    CoInitialize(NULL);
    IAccessible* pAcc = NULL;
    static const IID s_IID_IAccessible = { 0x618736e0, 0x3c3d, 0x11cf, { 0x81, 0x0c, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
    if (SUCCEEDED(AccessibleObjectFromWindow(hLV, OBJID_CLIENT, &s_IID_IAccessible, (void**)&pAcc)) && pAcc) {
        long childCount = 0;
        if (SUCCEEDED(pAcc->lpVtbl->get_accChildCount(pAcc, &childCount)) && childCount > 0) {
            VARIANT* pChildren = (VARIANT*)HeapAlloc(GetProcessHeap(), 0, sizeof(VARIANT) * childCount);
            if (pChildren) {
                long obtained = 0;
                if (SUCCEEDED(AccessibleChildren(pAcc, 0, childCount, pChildren, &obtained))) {
                    int screenW = GetSystemMetrics(SM_CXSCREEN);
                    int screenH = GetSystemMetrics(SM_CYSCREEN);
                    for (long i = 0; i < obtained && ctx->count < ctx->maxCount; i++) {
                        BSTR bstrName = NULL;
                        if (SUCCEEDED(pAcc->lpVtbl->get_accName(pAcc, pChildren[i], &bstrName)) && bstrName) {
                            char name[256] = {0};
                            WideCharToMultiByte(CP_ACP, 0, bstrName, -1, name, sizeof(name) - 1, NULL, NULL);
                            SysFreeString(bstrName);
                            if (text_matches(name, targetText)) {
                                long x = 0, y = 0, w = 0, h = 0;
                                if (SUCCEEDED(pAcc->lpVtbl->accLocation(pAcc, &x, &y, &w, &h, pChildren[i]))) {
                                    POINT pt = { (LONG)(x + w / 2), (LONG)(y + h / 2) };
                                    if (pt.x >= 0 && pt.y >= 0 && pt.x < screenW && pt.y < screenH) {
                                        BOOL dup = FALSE;
                                        for (int k = 0; k < ctx->count; k++) {
                                            if (ctx->outCenters[k].x == pt.x && ctx->outCenters[k].y == pt.y) {
                                                dup = TRUE; break;
                                            }
                                        }
                                        if (!dup) ctx->outCenters[ctx->count++] = pt;
                                    }
                                }
                            }
                        }
                        VariantClear(&pChildren[i]);
                    }
                }
                if (pChildren) {
                    HeapFree(GetProcessHeap(), 0, pChildren);
                }
            }
        }
        pAcc->lpVtbl->Release(pAcc);
    }
    CoUninitialize();
}

static BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
    TextSearchContext* ctx = (TextSearchContext*)lParam;
    return check_and_add_window(hwnd, ctx);
}

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    TextSearchContext* ctx = (TextSearchContext*)lParam;
    if (!check_and_add_window(hwnd, ctx)) {
        return FALSE;
    }
    EnumChildWindows(hwnd, EnumChildProc, lParam);
    return (ctx->count < ctx->maxCount);
}

int ttp_find_elements_by_text(const char* targetText, POINT* outCenters, int maxCount) {
    if (!targetText || !outCenters || maxCount <= 0 || targetText[0] == '\0') {
        return 0;
    }

    TextSearchContext ctx;
    ctx.targetText = targetText;
    ctx.outCenters = outCenters;
    ctx.maxCount = maxCount;
    ctx.count = 0;

    /* 1. Check desktop icons via MSAA first (supports moved desktop shortcuts) */
    check_desktop_icons_accessible(targetText, &ctx);

    /* 2. Check foreground window and its descendants */
    if (ctx.count < ctx.maxCount) {
        HWND hFore = GetForegroundWindow();
        if (hFore && IsWindow(hFore)) {
            check_and_add_window(hFore, &ctx);
            if (ctx.count < ctx.maxCount) {
                EnumChildWindows(hFore, EnumChildProc, (LPARAM)&ctx);
            }
        }
    }

    /* 3. Check all top-level desktop windows */
    if (ctx.count < ctx.maxCount) {
        EnumWindows(EnumWindowsProc, (LPARAM)&ctx);
    }

    return ctx.count;
}
