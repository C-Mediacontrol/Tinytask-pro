#include "ttp_vision.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* =========================================================================
 * 1. Euclidean Distance & Spatial Disambiguation
 * ========================================================================= */

double ttp_calc_euclidean_dist(LONG x1, LONG y1, LONG x2, LONG y2) {
    double dx = (double)(x1 - x2);
    double dy = (double)(y1 - y2);
    return sqrt(dx * dx + dy * dy);
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
        free(bmpBuffer);
    }
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
    memset(&bi, 0, sizeof(bi));
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

    BitBlt(hdcMem, 0, 0, ROI_SIZE, ROI_SIZE, hdc, roiLeft, roiTop, SRCCOPY);
    GdiFlush();

    /* 1. Compute grayscale luminance */
    BYTE gray[256][256];
    const BYTE* srcPix = (const BYTE*)pBits;
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
        }
    }

    /* 2. Compute 3x3 Sobel gradients */
    int* gradX = (int*)calloc(ROI_SIZE * ROI_SIZE, sizeof(int));
    int* gradY = (int*)calloc(ROI_SIZE * ROI_SIZE, sizeof(int));
    int* gradM = (int*)calloc(ROI_SIZE * ROI_SIZE, sizeof(int));
    if (!gradX || !gradY || !gradM) {
        if (gradX) free(gradX);
        if (gradY) free(gradY);
        if (gradM) free(gradM);
        SelectObject(hdcMem, hOld);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        if (releaseDC) ReleaseDC(NULL, hdc);
        return FALSE;
    }

    for (int y = 1; y < ROI_SIZE - 1; y++) {
        for (int x = 1; x < ROI_SIZE - 1; x++) {
            int gx = -gray[y-1][x-1] + gray[y-1][x+1]
                     - 2 * gray[y][x-1] + 2 * gray[y][x+1]
                     - gray[y+1][x-1] + gray[y+1][x+1];
            int gy = -gray[y-1][x-1] - 2 * gray[y-1][x] - gray[y-1][x+1]
                     + gray[y+1][x-1] + 2 * gray[y+1][x] + gray[y+1][x+1];
            int idx = y * ROI_SIZE + x;
            gradX[idx] = abs(gx);
            gradY[idx] = abs(gy);
            gradM[idx] = abs(gx) + abs(gy);
        }
    }

    /* 3. Detect edges around center (cx, cy) = (128, 128) */
    int cx = HALF_ROI;
    int cy = HALF_ROI;

    /* Scan up and down to find horizontal button borders (top and bottom) */
    int bestTop = cy - 20;
    int maxTopGrad = -1;
    for (int y = cy - 4; y >= cy - 65 && y >= 2; y--) {
        int sum = 0;
        for (int x = cx - 12; x <= cx + 12; x++) {
            int idx = y * ROI_SIZE + x;
            sum += gradY[idx] * 2 + gradM[idx];
        }
        if (sum > maxTopGrad) {
            maxTopGrad = sum;
            bestTop = y;
        }
    }

    int bestBottom = cy + 20;
    int maxBottomGrad = -1;
    for (int y = cy + 4; y <= cy + 65 && y < ROI_SIZE - 2; y++) {
        int sum = 0;
        for (int x = cx - 12; x <= cx + 12; x++) {
            int idx = y * ROI_SIZE + x;
            sum += gradY[idx] * 2 + gradM[idx];
        }
        if (sum > maxBottomGrad) {
            maxBottomGrad = sum;
            bestBottom = y;
        }
    }

    /* Scan left and right using detected vertical span [bestTop, bestBottom] */
    int spanTop = (bestTop < bestBottom) ? bestTop : bestBottom;
    int spanBottom = (bestTop < bestBottom) ? bestBottom : bestTop;

    int bestLeft = cx - 40;
    int maxLeftGrad = -1;
    for (int x = cx - 6; x >= cx - 115 && x >= 2; x--) {
        int sum = 0;
        for (int y = spanTop; y <= spanBottom; y++) {
            int idx = y * ROI_SIZE + x;
            sum += gradX[idx] * 2 + gradM[idx];
        }
        if (sum > maxLeftGrad) {
            maxLeftGrad = sum;
            bestLeft = x;
        }
    }

    int bestRight = cx + 40;
    int maxRightGrad = -1;
    for (int x = cx + 6; x <= cx + 115 && x < ROI_SIZE - 2; x++) {
        int sum = 0;
        for (int y = spanTop; y <= spanBottom; y++) {
            int idx = y * ROI_SIZE + x;
            sum += gradX[idx] * 2 + gradM[idx];
        }
        if (sum > maxRightGrad) {
            maxRightGrad = sum;
            bestRight = x;
        }
    }

    /* Refine top and bottom using detected horizontal span [bestLeft, bestRight] */
    int spanLeft = (bestLeft < bestRight) ? bestLeft : bestRight;
    int spanRight = (bestLeft < bestRight) ? bestRight : bestLeft;

    for (int y = cy - 4; y >= cy - 65 && y >= 2; y--) {
        int sum = 0;
        for (int x = spanLeft; x <= spanRight; x++) {
            int idx = y * ROI_SIZE + x;
            sum += gradY[idx] * 2 + gradM[idx];
        }
        if (sum > maxTopGrad) {
            maxTopGrad = sum;
            bestTop = y;
        }
    }

    for (int y = cy + 4; y <= cy + 65 && y < ROI_SIZE - 2; y++) {
        int sum = 0;
        for (int x = spanLeft; x <= spanRight; x++) {
            int idx = y * ROI_SIZE + x;
            sum += gradY[idx] * 2 + gradM[idx];
        }
        if (sum > maxBottomGrad) {
            maxBottomGrad = sum;
            bestBottom = y;
        }
    }

    /* Free gradient tables */
    free(gradX);
    free(gradY);
    free(gradM);

    /* Fallback if flat surface */
    if (maxTopGrad < 50 && maxBottomGrad < 50 && maxLeftGrad < 50 && maxRightGrad < 50) {
        bestLeft = cx - 40;
        bestRight = cx + 40;
        bestTop = cy - 15;
        bestBottom = cy + 15;
    }

    int L = (bestLeft < bestRight) ? bestLeft : bestRight;
    int R = (bestLeft < bestRight) ? bestRight : bestLeft;
    int T = (bestTop < bestBottom) ? bestTop : bestBottom;
    int B = (bestTop < bestBottom) ? bestBottom : bestTop;

    /* Clamp dimensions: width in [20, 220], height in [15, 90] */
    int width = R - L;
    if (width < 20) {
        int mid = (L + R) / 2;
        L = mid - 10;
        R = mid + 10;
    } else if (width > 220) {
        int mid = (L + R) / 2;
        L = mid - 110;
        R = mid + 110;
    }

    int height = B - T;
    if (height < 15) {
        int mid = (T + B) / 2;
        T = mid - 7;
        B = mid + 8;
    } else if (height > 90) {
        int mid = (T + B) / 2;
        T = mid - 45;
        B = mid + 45;
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

    /* Format standard 24bpp uncompressed BMP */
    int rowStride = ((cropW * 3 + 3) / 4) * 4;
    DWORD imgSize = (DWORD)rowStride * cropH;
    DWORD totalBmpSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + imgSize;

    BYTE* bmpBuf = (BYTE*)malloc(totalBmpSize);
    if (!bmpBuf) {
        SelectObject(hdcMem, hOld);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        if (releaseDC) ReleaseDC(NULL, hdc);
        return FALSE;
    }
    memset(bmpBuf, 0, totalBmpSize);

    BITMAPFILEHEADER* bmfh = (BITMAPFILEHEADER*)bmpBuf;
    bmfh->bfType = 0x4D42; /* 'BM' */
    bmfh->bfSize = totalBmpSize;
    bmfh->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    BITMAPINFOHEADER* bmih = (BITMAPINFOHEADER*)(bmpBuf + sizeof(BITMAPFILEHEADER));
    bmih->biSize = sizeof(BITMAPINFOHEADER);
    bmih->biWidth = cropW;
    bmih->biHeight = cropH; /* Standard bottom-up */
    bmih->biPlanes = 1;
    bmih->biBitCount = 24;
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
            dstRow[x * 3 + 0] = px[0]; /* B */
            dstRow[x * 3 + 1] = px[1]; /* G */
            dstRow[x * 3 + 2] = px[2]; /* R */
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

/* =========================================================================
 * 3. Normalized Cross-Correlation (NCC) Template Matcher
 * ========================================================================= */

BOOL ttp_match_template_ncc(HDC hdcScreen, int screenW, int screenH, const BYTE* bmpPattern, DWORD bmpSize, double minScore, POINT* outMatchPos, double* outScore) {
    if (!bmpPattern || bmpSize < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)) {
        if (outScore) *outScore = 0.0;
        return FALSE;
    }

    const BITMAPFILEHEADER* bmfh = (const BITMAPFILEHEADER*)bmpPattern;
    if (bmfh->bfType != 0x4D42) {
        if (outScore) *outScore = 0.0;
        return FALSE;
    }

    const BITMAPINFOHEADER* bmih = (const BITMAPINFOHEADER*)(bmpPattern + sizeof(BITMAPFILEHEADER));
    int tw = bmih->biWidth;
    int th = abs(bmih->biHeight);
    BOOL isBottomUp = (bmih->biHeight > 0);

    if (tw <= 0 || th <= 0 || bmih->biBitCount != 24) {
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

    if (screenW <= 0) screenW = GetSystemMetrics(SM_CXSCREEN);
    if (screenH <= 0) screenH = GetSystemMetrics(SM_CYSCREEN);

    if (screenW < tw || screenH < th) {
        if (releaseDC) ReleaseDC(NULL, hdc);
        if (outScore) *outScore = 0.0;
        return FALSE;
    }

    int tStride = ((tw * 3 + 3) / 4) * 4;
    const BYTE* tPixels = bmpPattern + bmfh->bfOffBits;
    int N = tw * th;

    double* T = (double*)malloc(N * sizeof(double));
    if (!T) {
        if (releaseDC) ReleaseDC(NULL, hdc);
        if (outScore) *outScore = 0.0;
        return FALSE;
    }

    double sumT = 0.0;
    for (int y = 0; y < th; y++) {
        int bmpRow = isBottomUp ? (th - 1 - y) : y;
        const BYTE* row = tPixels + bmpRow * tStride;
        for (int x = 0; x < tw; x++) {
            BYTE b = row[x * 3 + 0];
            BYTE g = row[x * 3 + 1];
            BYTE r = row[x * 3 + 2];
            double lum = 0.299 * r + 0.587 * g + 0.114 * b;
            T[y * tw + x] = lum;
            sumT += lum;
        }
    }

    double meanT = sumT / N;
    double sumSqDiffT = 0.0;
    for (int i = 0; i < N; i++) {
        double d = T[i] - meanT;
        sumSqDiffT += d * d;
    }
    double denomT = sqrt(sumSqDiffT);

    if (denomT < 1e-6) {
        free(T);
        if (releaseDC) ReleaseDC(NULL, hdc);
        if (outScore) *outScore = 0.0;
        return FALSE;
    }

    double* nT = (double*)malloc(N * sizeof(double));
    if (!nT) {
        free(T);
        if (releaseDC) ReleaseDC(NULL, hdc);
        if (outScore) *outScore = 0.0;
        return FALSE;
    }
    for (int i = 0; i < N; i++) {
        nT[i] = (T[i] - meanT) / denomT;
    }
    free(T);

    /* Capture screen DC into 32bpp top-down DIB */
    HDC hdcMem = CreateCompatibleDC(hdc);
    if (!hdcMem) {
        free(nT);
        if (releaseDC) ReleaseDC(NULL, hdc);
        if (outScore) *outScore = 0.0;
        return FALSE;
    }

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = screenW;
    bi.bmiHeader.biHeight = -screenH; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* pScreenBits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &pScreenBits, NULL, 0);
    if (!hBmp || !pScreenBits) {
        DeleteDC(hdcMem);
        free(nT);
        if (releaseDC) ReleaseDC(NULL, hdc);
        if (outScore) *outScore = 0.0;
        return FALSE;
    }

    HGDIOBJ hOld = SelectObject(hdcMem, hBmp);
    BitBlt(hdcMem, 0, 0, screenW, screenH, hdc, 0, 0, SRCCOPY);
    GdiFlush();

    int screenPixels = screenW * screenH;
    double* S = (double*)malloc(screenPixels * sizeof(double));
    if (!S) {
        SelectObject(hdcMem, hOld);
        DeleteObject(hBmp);
        DeleteDC(hdcMem);
        free(nT);
        if (releaseDC) ReleaseDC(NULL, hdc);
        if (outScore) *outScore = 0.0;
        return FALSE;
    }

    const BYTE* srcPx = (const BYTE*)pScreenBits;
    for (int y = 0; y < screenH; y++) {
        for (int x = 0; x < screenW; x++) {
            const BYTE* px = srcPx + (y * screenW + x) * 4;
            S[y * screenW + x] = 0.299 * px[2] + 0.587 * px[1] + 0.114 * px[0];
        }
    }

    SelectObject(hdcMem, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    if (releaseDC) ReleaseDC(NULL, hdc);

    /* Integral images for fast window sum and variance */
    int satStride = screenW + 1;
    int satSize = satStride * (screenH + 1);
    double* sat1 = (double*)calloc(satSize, sizeof(double));
    double* sat2 = (double*)calloc(satSize, sizeof(double));

    if (!sat1 || !sat2) {
        if (sat1) free(sat1);
        if (sat2) free(sat2);
        free(S);
        free(nT);
        if (outScore) *outScore = 0.0;
        return FALSE;
    }

    for (int y = 0; y < screenH; y++) {
        double rowSum1 = 0.0;
        double rowSum2 = 0.0;
        for (int x = 0; x < screenW; x++) {
            double v = S[y * screenW + x];
            rowSum1 += v;
            rowSum2 += v * v;
            sat1[(y + 1) * satStride + (x + 1)] = sat1[y * satStride + (x + 1)] + rowSum1;
            sat2[(y + 1) * satStride + (x + 1)] = sat2[y * satStride + (x + 1)] + rowSum2;
        }
    }

    int maxX = screenW - tw;
    int maxY = screenH - th;
    double bestScore = -1.0;
    int bestX = 0;
    int bestY = 0;

    for (int y = 0; y <= maxY; y++) {
        int y1 = y;
        int y2 = y + th;
        for (int x = 0; x <= maxX; x++) {
            int x1 = x;
            int x2 = x + tw;

            double sumI = sat1[y2 * satStride + x2] - sat1[y1 * satStride + x2]
                        - sat1[y2 * satStride + x1] + sat1[y1 * satStride + x1];
            double sumI2 = sat2[y2 * satStride + x2] - sat2[y1 * satStride + x2]
                         - sat2[y2 * satStride + x1] + sat2[y1 * satStride + x1];

            double varI = sumI2 - (sumI * sumI) / N;
            if (varI <= 1e-4) {
                continue;
            }

            double denomI = sqrt(varI);
            double num = 0.0;
            for (int v = 0; v < th; v++) {
                const double* pS = &S[(y + v) * screenW + x];
                const double* pnT = &nT[v * tw];
                for (int u = 0; u < tw; u++) {
                    num += pnT[u] * pS[u];
                }
            }

            double score = num / denomI;
            if (score > bestScore) {
                bestScore = score;
                bestX = x;
                bestY = y;
            }
        }
    }

    free(sat1);
    free(sat2);
    free(S);
    free(nT);

    if (outScore) {
        *outScore = (bestScore < -1.0) ? 0.0 : bestScore;
    }

    if (bestScore >= minScore) {
        if (outMatchPos) {
            outMatchPos->x = bestX + tw / 2;
            outMatchPos->y = bestY + th / 2;
        }
        return TRUE;
    }

    return FALSE;
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
    int nlen = (int)strlen(needle);
    int hlen = (int)strlen(haystack);
    if (nlen == 0 || hlen < nlen) return FALSE;
    for (int i = 0; i <= hlen - nlen; i++) {
        if (_strnicmp(&haystack[i], needle, nlen) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL check_and_add_window(HWND hwnd, TextSearchContext* ctx) {
    if (!IsWindow(hwnd) || !IsWindowVisible(hwnd)) return TRUE;

    char buf[512] = {0};
    if (GetWindowTextA(hwnd, buf, sizeof(buf)) <= 0) {
        SendMessageTimeoutA(hwnd, WM_GETTEXT, sizeof(buf), (LPARAM)buf, SMTO_ABORTIFHUNG, 50, NULL);
    }

    if (buf[0] != '\0' && text_matches(buf, ctx->targetText)) {
        RECT rc;
        if (GetWindowRect(hwnd, &rc)) {
            if (rc.right > rc.left && rc.bottom > rc.top) {
                POINT pt = { (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
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
        }
    }
    return (ctx->count < ctx->maxCount);
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

    /* 1. Check foreground window and its descendants first */
    HWND hFore = GetForegroundWindow();
    if (hFore && IsWindow(hFore)) {
        check_and_add_window(hFore, &ctx);
        if (ctx.count < ctx.maxCount) {
            EnumChildWindows(hFore, EnumChildProc, (LPARAM)&ctx);
        }
    }

    /* 2. Check all top-level desktop windows */
    if (ctx.count < ctx.maxCount) {
        EnumWindows(EnumWindowsProc, (LPARAM)&ctx);
    }

    return ctx.count;
}
