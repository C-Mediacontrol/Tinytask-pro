#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <windows.h>
#include "ttp_vision.h"

/* Helper: Create a 32bpp BGRA BMP buffer in memory with custom pixel callback */
typedef void (*PixelColorFn)(int x, int y, BYTE* outB, BYTE* outG, BYTE* outR, BYTE* outA);

static BYTE* create_bmp_32bpp(int w, int h, PixelColorFn pixelFn, DWORD* outSize) {
    int rowStride = w * 4;
    DWORD imgSize = (DWORD)rowStride * h;
    DWORD totalSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + imgSize;

    BYTE* buf = (BYTE*)malloc(totalSize);
    if (!buf) return NULL;
    memset(buf, 0, totalSize);

    BITMAPFILEHEADER* bmfh = (BITMAPFILEHEADER*)buf;
    bmfh->bfType = 0x4D42; /* 'BM' */
    bmfh->bfSize = totalSize;
    bmfh->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    BITMAPINFOHEADER* bmih = (BITMAPINFOHEADER*)(buf + sizeof(BITMAPFILEHEADER));
    bmih->biSize = sizeof(BITMAPINFOHEADER);
    bmih->biWidth = w;
    bmih->biHeight = h; /* Standard bottom-up */
    bmih->biPlanes = 1;
    bmih->biBitCount = 32;
    bmih->biCompression = BI_RGB;
    bmih->biSizeImage = imgSize;

    BYTE* dstData = buf + bmfh->bfOffBits;
    for (int y = 0; y < h; y++) {
        /* Bottom-up BMP: row 0 is bottom row (y = h - 1) */
        int origY = y;
        int bmpRow = h - 1 - origY;
        BYTE* row = dstData + bmpRow * rowStride;
        for (int x = 0; x < w; x++) {
            BYTE b = 0, g = 0, r = 0, a = 255;
            pixelFn(x, origY, &b, &g, &r, &a);
            row[x * 4 + 0] = b;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = r;
            row[x * 4 + 3] = a;
        }
    }

    if (outSize) *outSize = totalSize;
    return buf;
}

/* Helper: Create a 24bpp BGR BMP buffer in memory with custom pixel callback */
static BYTE* create_bmp_24bpp(int w, int h, PixelColorFn pixelFn, DWORD* outSize) {
    int rowStride = ((w * 3 + 3) / 4) * 4;
    DWORD imgSize = (DWORD)rowStride * h;
    DWORD totalSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + imgSize;

    BYTE* buf = (BYTE*)malloc(totalSize);
    if (!buf) return NULL;
    memset(buf, 0, totalSize);

    BITMAPFILEHEADER* bmfh = (BITMAPFILEHEADER*)buf;
    bmfh->bfType = 0x4D42; /* 'BM' */
    bmfh->bfSize = totalSize;
    bmfh->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    BITMAPINFOHEADER* bmih = (BITMAPINFOHEADER*)(buf + sizeof(BITMAPFILEHEADER));
    bmih->biSize = sizeof(BITMAPINFOHEADER);
    bmih->biWidth = w;
    bmih->biHeight = h; /* Standard bottom-up */
    bmih->biPlanes = 1;
    bmih->biBitCount = 24;
    bmih->biCompression = BI_RGB;
    bmih->biSizeImage = imgSize;

    BYTE* dstData = buf + bmfh->bfOffBits;
    for (int y = 0; y < h; y++) {
        int origY = y;
        int bmpRow = h - 1 - origY;
        BYTE* row = dstData + bmpRow * rowStride;
        for (int x = 0; x < w; x++) {
            BYTE b = 0, g = 0, r = 0, a = 255;
            pixelFn(x, origY, &b, &g, &r, &a);
            row[x * 3 + 0] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
    }

    if (outSize) *outSize = totalSize;
    return buf;
}

#define ICON_W 74
#define ICON_H 70
#define BORDER_THICKNESS 10

/* Returns TRUE if (x, y) is in the outer 10-pixel border */
static BOOL is_border_pixel(int x, int y) {
    return (x < BORDER_THICKNESS || x >= ICON_W - BORDER_THICKNESS ||
            y < BORDER_THICKNESS || y >= ICON_H - BORDER_THICKNESS);
}

/* Distinctive central icon pattern callback */
static void get_icon_pixel(int x, int y, BYTE* outB, BYTE* outG, BYTE* outR, BYTE* outA, BOOL withAlphaMask) {
    if (is_border_pixel(x, y)) {
        /* Border pixels recorded on dark gray background RGB(30, 30, 40) */
        *outR = 30;
        *outG = 30;
        *outB = 40;
        *outA = withAlphaMask ? 0 : 255;
        return;
    }

    /* Central icon area (54x50): Alpha = 255 with high-contrast geometric & text features */
    *outA = 255;

    /* Base icon background: deep crimson RGB(190, 45, 45) */
    BYTE r = 190, g = 45, b = 45;

    /* Inner 2px frame at perimeter of central area */
    if (x == BORDER_THICKNESS || x == ICON_W - BORDER_THICKNESS - 1 ||
        y == BORDER_THICKNESS || y == ICON_H - BORDER_THICKNESS - 1 ||
        x == BORDER_THICKNESS + 1 || x == ICON_W - BORDER_THICKNESS - 2 ||
        y == BORDER_THICKNESS + 1 || y == ICON_H - BORDER_THICKNESS - 2) {
        r = 30; g = 20; b = 20;
    }
    /* Bright yellow cross in center */
    else if ((y >= 31 && y <= 37 && x >= 22 && x <= 51) ||
             (x >= 33 && x <= 39 && y >= 19 && y <= 49)) {
        r = 255; g = 220; b = 30; /* Bright yellow */
    }
    /* Horizontal white text-like lines */
    else if ((y >= 21 && y <= 23 && x >= 18 && x <= 29) ||
             (y >= 26 && y <= 28 && x >= 18 && x <= 29) ||
             (y >= 43 && y <= 45 && x >= 43 && x <= 56) ||
             (y >= 48 && y <= 50 && x >= 43 && x <= 56)) {
        r = 255; g = 255; b = 255; /* White */
    }
    /* Blue badge accent */
    else if (x >= 45 && x <= 55 && y >= 18 && y <= 26) {
        r = 40; g = 120; b = 230; /* Cyan blue */
    }

    *outR = r;
    *outG = g;
    *outB = b;
}

static void icon_pixel_masked_fn(int x, int y, BYTE* outB, BYTE* outG, BYTE* outR, BYTE* outA) {
    get_icon_pixel(x, y, outB, outG, outR, outA, TRUE);
}

static void icon_pixel_unmasked_fn(int x, int y, BYTE* outB, BYTE* outG, BYTE* outR, BYTE* outA) {
    get_icon_pixel(x, y, outB, outG, outR, outA, FALSE);
}

int main(void) {
    printf("===============================================================\n");
    printf("Task 4: Masked Cascaded NCC Engine & Zero-Heap Architecture Test\n");
    printf("===============================================================\n\n");

    /* 1. Create simulated 2560x1440 desktop screen memory DC */
    printf("[Step 1] Creating 2560x1440 simulated desktop screen DC...\n");
    HDC hdcScreen = GetDC(NULL);
    HDC hdcCanvas = CreateCompatibleDC(hdcScreen);
    assert(hdcCanvas != NULL);

    BITMAPINFO biCanvas;
    memset(&biCanvas, 0, sizeof(biCanvas));
    biCanvas.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    biCanvas.bmiHeader.biWidth = 2560;
    biCanvas.bmiHeader.biHeight = -1440; /* top-down */
    biCanvas.bmiHeader.biPlanes = 1;
    biCanvas.bmiHeader.biBitCount = 32;
    biCanvas.bmiHeader.biCompression = BI_RGB;

    void* pCanvasBits = NULL;
    HBITMAP hBmpCanvas = CreateDIBSection(hdcCanvas, &biCanvas, DIB_RGB_COLORS, &pCanvasBits, NULL, 0);
    assert(hBmpCanvas != NULL && pCanvasBits != NULL);
    HGDIOBJ hOldCanvas = SelectObject(hdcCanvas, hBmpCanvas);

    /* Fill canvas with bright sky blue background RGB(100, 180, 240) */
    RECT rcScreen = { 0, 0, 2560, 1440 };
    HBRUSH hSkyBrush = CreateSolidBrush(RGB(100, 180, 240));
    FillRect(hdcCanvas, &rcScreen, hSkyBrush);
    DeleteObject(hSkyBrush);

    /* 2. Place the exact same icon at position (2100, 150) (top-right corner).
     * Center is (2100, 150), so top-left is:
     * iconLeft = 2100 - 74/2 = 2063
     * iconTop  = 150 - 70/2  = 115
     * On the canvas, only the central icon area is drawn; the surrounding
     * perimeter remains the bright sky blue desktop wallpaper RGB(100, 180, 240)!
     */
    printf("[Step 2] Placing 74x70 icon at center (2100, 150) over sky-blue wallpaper...\n");
    int targetCenterX = 2100;
    int targetCenterY = 150;
    int iconLeft = targetCenterX - ICON_W / 2; /* 2063 */
    int iconTop = targetCenterY - ICON_H / 2;  /* 115 */

    for (int y = 0; y < ICON_H; y++) {
        for (int x = 0; x < ICON_W; x++) {
            if (!is_border_pixel(x, y)) {
                BYTE b = 0, g = 0, r = 0, a = 255;
                get_icon_pixel(x, y, &b, &g, &r, &a, TRUE);
                /* Draw central icon pixel onto canvas */
                BYTE* px = (BYTE*)pCanvasBits + ((iconTop + y) * 2560 + (iconLeft + x)) * 4;
                px[0] = b;
                px[1] = g;
                px[2] = r;
                px[3] = 255;
            }
        }
    }
    GdiFlush();
    printf("         Icon placed: top-left=(%d, %d), center=(%d, %d)\n",
           iconLeft, iconTop, targetCenterX, targetCenterY);

    /* 3. Build templates:
     * - Masked Template: 32bpp BGRA with Alpha=0 on border, Alpha=255 in center
     * - Unmasked Template: 24bpp BGR (border is dark gray RGB(30, 30, 40))
     */
    printf("[Step 3] Creating templates in memory...\n");
    DWORD maskedBmpSize = 0;
    BYTE* bmpMasked = create_bmp_32bpp(ICON_W, ICON_H, icon_pixel_masked_fn, &maskedBmpSize);
    assert(bmpMasked != NULL && maskedBmpSize > 0);

    DWORD unmaskedBmpSize = 0;
    BYTE* bmpUnmasked = create_bmp_24bpp(ICON_W, ICON_H, icon_pixel_unmasked_fn, &unmaskedBmpSize);
    assert(bmpUnmasked != NULL && unmaskedBmpSize > 0);

    printf("         Masked template size:   %lu bytes (32bpp BGRA with alpha mask)\n", maskedBmpSize);
    printf("         Unmasked template size: %lu bytes (24bpp BGR unmasked)\n\n", unmaskedBmpSize);

    /* 4. Test Standard Unmasked NCC:
     * Due to wallpaper difference (sky blue RGB(100, 180, 240) vs recorded dark gray RGB(30, 30, 40)),
     * the unmasked NCC score across the full 74x70 rectangle drops below 0.72!
     */
    printf("[Test 1] Running standard unmasked NCC on 2560x1440 canvas...\n");
    POINT unmaskedPos = { 0, 0 };
    double unmaskedScore = 0.0;
    BOOL unmaskedMatched = ttp_match_template_masked_ncc(
        hdcCanvas, 2560, 1440, bmpUnmasked, unmaskedBmpSize, 0.72, &unmaskedPos, &unmaskedScore);

    printf("         Unmasked NCC Result: matched=%s, score=%.4f, pos=(%ld, %ld)\n",
           unmaskedMatched ? "TRUE" : "FALSE", unmaskedScore, unmaskedPos.x, unmaskedPos.y);
    assert(unmaskedScore < 0.72);
    assert(unmaskedMatched == FALSE);
    printf("         [PASS] Confirmed unmasked NCC drops below 0.72 due to background interference!\n\n");

    /* 5. Test Masked NCC Full-Screen Search:
     * Alpha mask (A == 0) ignores the border pixels. Masked NCC evaluates only the
     * central icon area. Score must be >= 0.90 (in fact near 1.0000) and match position
     * must be exactly (2100, 150) +- 2px!
     */
    printf("[Test 2] Running Masked Cascaded NCC (full screen 2560x1440)...\n");
    POINT maskedPos = { 0, 0 };
    double maskedScore = 0.0;
    BOOL maskedMatched = ttp_match_template_masked_ncc(
        hdcCanvas, 2560, 1440, bmpMasked, maskedBmpSize, 0.90, &maskedPos, &maskedScore);

    printf("         Masked NCC Result: matched=%s, score=%.4f, pos=(%ld, %ld)\n",
           maskedMatched ? "TRUE" : "FALSE", maskedScore, maskedPos.x, maskedPos.y);
    assert(maskedMatched == TRUE);
    assert(maskedScore >= 0.90);
    assert(abs(maskedPos.x - 2100) <= 2);
    assert(abs(maskedPos.y - 150) <= 2);
    printf("         [PASS] Masked NCC succeeded! Score >= 0.90, pos=(2100, 150)+-2px verified!\n\n");

    /* Also verify that ttp_match_template_ncc transparently uses the masked engine */
    printf("[Test 2b] Verifying standard ttp_match_template_ncc automatically activates masked engine...\n");
    POINT nccPos = { 0, 0 };
    double nccScore = 0.0;
    BOOL nccMatched = ttp_match_template_ncc(
        hdcCanvas, 2560, 1440, bmpMasked, maskedBmpSize, 0.90, &nccPos, &nccScore);

    assert(nccMatched == TRUE);
    assert(nccScore >= 0.90);
    assert(abs(nccPos.x - 2100) <= 2);
    assert(abs(nccPos.y - 150) <= 2);
    printf("         [PASS] ttp_match_template_ncc matched at (%ld, %ld) with score %.4f!\n\n",
           nccPos.x, nccPos.y, nccScore);

    /* 6. Test Masked ROI Matching (ttp_match_template_masked_ncc_roi):
     * Localized ROI radius = 200px around (2100, 150). Must succeed with score >= 0.90!
     */
    printf("[Test 3] Running Masked ROI Matching (radius=200 around (2100, 150))...\n");
    POINT roiPos = { 0, 0 };
    double roiScore = 0.0;
    BOOL roiMatched = ttp_match_template_masked_ncc_roi(
        hdcCanvas, 2100, 150, 200, bmpMasked, maskedBmpSize, 0.90, &roiPos, &roiScore);

    printf("         Masked ROI Result: matched=%s, score=%.4f, pos=(%ld, %ld)\n",
           roiMatched ? "TRUE" : "FALSE", roiScore, roiPos.x, roiPos.y);
    assert(roiMatched == TRUE);
    assert(roiScore >= 0.90);
    assert(abs(roiPos.x - 2100) <= 2);
    assert(abs(roiPos.y - 150) <= 2);
    printf("         [PASS] Masked ROI matching succeeded with score >= 0.90 at (%ld, %ld)!\n\n",
           roiPos.x, roiPos.y);

    /* Also verify ttp_match_template_ncc_roi transparently calls the masked ROI engine */
    printf("[Test 3b] Verifying standard ttp_match_template_ncc_roi calls masked engine...\n");
    POINT nccRoiPos = { 0, 0 };
    double nccRoiScore = 0.0;
    BOOL nccRoiMatched = ttp_match_template_ncc_roi(
        hdcCanvas, 2100, 150, 200, bmpMasked, maskedBmpSize, 0.90, &nccRoiPos, &nccRoiScore);

    assert(nccRoiMatched == TRUE);
    assert(nccRoiScore >= 0.90);
    assert(abs(nccRoiPos.x - 2100) <= 2);
    assert(abs(nccRoiPos.y - 150) <= 2);
    printf("         [PASS] ttp_match_template_ncc_roi matched at (%ld, %ld) with score %.4f!\n\n",
           nccRoiPos.x, nccRoiPos.y, nccRoiScore);

    /* Cleanup */
    free(bmpMasked);
    free(bmpUnmasked);
    SelectObject(hdcCanvas, hOldCanvas);
    DeleteObject(hBmpCanvas);
    DeleteDC(hdcCanvas);
    ReleaseDC(NULL, hdcScreen);

    printf("===============================================================\n");
    printf("ALL MASKED VISION ENGINE TESTS PASSED PERFECTLY!\n");
    printf("===============================================================\n");
    return 0;
}
