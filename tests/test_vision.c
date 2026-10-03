#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <windows.h>
#include "ttp_vision.h"

/* Helper: Create a 24bpp uncompressed BMP in memory from an HDC region */
static BYTE* create_test_bmp(HDC hdcSrc, int srcX, int srcY, int w, int h, DWORD* outSize) {
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
    bmih->biHeight = h; /* bottom-up */
    bmih->biPlanes = 1;
    bmih->biBitCount = 24;
    bmih->biCompression = BI_RGB;
    bmih->biSizeImage = imgSize;

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h; /* top-down for easy copy */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC hdcMem = CreateCompatibleDC(hdcSrc);
    void* bits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcMem, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ hOld = SelectObject(hdcMem, hBmp);

    BitBlt(hdcMem, 0, 0, w, h, hdcSrc, srcX, srcY, SRCCOPY);
    GdiFlush();

    /* Copy to bottom-up BMP buffer */
    BYTE* dstData = buf + bmfh->bfOffBits;
    for (int y = 0; y < h; y++) {
        int srcRowY = h - 1 - y;
        const BYTE* srcRow = (const BYTE*)bits + srcRowY * rowStride;
        BYTE* dstRow = dstData + y * rowStride;
        memcpy(dstRow, srcRow, w * 3);
    }

    SelectObject(hdcMem, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);

    if (outSize) *outSize = totalSize;
    return buf;
}

static void test_distance_and_nearest(void) {
    printf("[1/4] Running test_distance_and_nearest...\n");

    /* Test Euclidean distance */
    double d1 = ttp_calc_euclidean_dist(0, 0, 3, 4);
    assert(fabs(d1 - 5.0) < 1e-6);

    double d2 = ttp_calc_euclidean_dist(100, 200, 103, 204);
    assert(fabs(d2 - 5.0) < 1e-6);

    double d3 = ttp_calc_euclidean_dist(50, 50, 50, 50);
    assert(fabs(d3 - 0.0) < 1e-6);

    /* Test pick nearest candidate */
    POINT candidates[3] = { {100, 100}, {300, 300}, {105, 108} };
    int best0 = ttp_pick_nearest_candidate(100, 102, candidates, 3);
    assert(best0 == 0); /* (100,100) is closest to (100,102) */

    int best2 = ttp_pick_nearest_candidate(106, 107, candidates, 3);
    assert(best2 == 2); /* (105,108) is closest to (106,107) */

    int best1 = ttp_pick_nearest_candidate(295, 305, candidates, 3);
    assert(best1 == 1); /* (300,300) is closest to (295,305) */

    /* Spatial disambiguation: 3 buttons on screen with same label */
    POINT identicalLabels[3] = { {100, 50}, {250, 50}, {400, 50} };
    int nearest = ttp_pick_nearest_candidate(245, 52, identicalLabels, 3);
    assert(nearest == 1); /* Matches middle button */

    /* Edge cases */
    assert(ttp_pick_nearest_candidate(100, 100, NULL, 3) == -1);
    assert(ttp_pick_nearest_candidate(100, 100, candidates, 0) == -1);
    assert(ttp_pick_nearest_candidate(100, 100, candidates, -1) == -1);

    printf("      Distance and nearest candidate tests passed!\n");
}

static void test_ncc_template_matching(void) {
    printf("[2/4] Running test_ncc_template_matching...\n");

    /* Create synthetic 400x300 canvas */
    HDC hdcScreen = GetDC(NULL);
    HDC hdcCanvas = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 400;
    bi.bmiHeader.biHeight = -300; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcCanvas, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ hOld = SelectObject(hdcCanvas, hBmp);

    /* Fill canvas with background RGB(220, 220, 220) */
    RECT bgRect = { 0, 0, 400, 300 };
    HBRUSH hBgBrush = CreateSolidBrush(RGB(220, 220, 220));
    FillRect(hdcCanvas, &bgRect, hBgBrush);
    DeleteObject(hBgBrush);

    /* Draw button at (150, 100) to (230, 130) -> width 80, height 30 */
    RECT btnRect = { 150, 100, 230, 130 };
    HBRUSH hBtnBrush = CreateSolidBrush(RGB(30, 80, 180));
    FillRect(hdcCanvas, &btnRect, hBtnBrush);
    DeleteObject(hBtnBrush);

    /* Add a distinctive contrasting center detail inside the button */
    RECT innerRect = { 185, 110, 195, 120 };
    HBRUSH hInnerBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(hdcCanvas, &innerRect, hInnerBrush);
    DeleteObject(hInnerBrush);
    GdiFlush();

    /* Create template BMP from the button (80x30) */
    DWORD bmpSize = 0;
    BYTE* bmpTemplate = create_test_bmp(hdcCanvas, 150, 100, 80, 30, &bmpSize);
    assert(bmpTemplate != NULL);
    assert(bmpSize > 54);

    /* Match template using NCC */
    POINT matchPos = { 0, 0 };
    double matchScore = 0.0;
    BOOL matched = ttp_match_template_ncc(hdcCanvas, 400, 300, bmpTemplate, bmpSize, 0.90, &matchPos, &matchScore);

    assert(matched == TRUE);
    assert(matchScore >= 0.95);
    /* Center should be 150 + 40 = 190, 100 + 15 = 115 */
    assert(matchPos.x == 190);
    assert(matchPos.y == 115);
    printf("      NCC Match found at (%ld, %ld) with score %.4f\n", matchPos.x, matchPos.y, matchScore);

    /* Test negative match with a different pattern */
    /* Create a distinct red square BMP that doesn't exist on canvas */
    HDC hdcRed = CreateCompatibleDC(hdcScreen);
    void* redBits = NULL;
    BITMAPINFO biRed = bi;
    biRed.bmiHeader.biWidth = 30;
    biRed.bmiHeader.biHeight = -30;
    HBITMAP hRedBmp = CreateDIBSection(hdcRed, &biRed, DIB_RGB_COLORS, &redBits, NULL, 0);
    HGDIOBJ hOldRed = SelectObject(hdcRed, hRedBmp);
    RECT redRect = { 0, 0, 30, 30 };
    HBRUSH hRedBrush = CreateSolidBrush(RGB(255, 0, 0));
    FillRect(hdcRed, &redRect, hRedBrush);
    DeleteObject(hRedBrush);
    GdiFlush();

    DWORD redBmpSize = 0;
    BYTE* redBmpTemplate = create_test_bmp(hdcRed, 0, 0, 30, 30, &redBmpSize);
    POINT negPos = { 0, 0 };
    double negScore = 0.0;
    BOOL negMatched = ttp_match_template_ncc(hdcCanvas, 400, 300, redBmpTemplate, redBmpSize, 0.85, &negPos, &negScore);
    assert(negMatched == FALSE);
    assert(negScore < 0.85);

    SelectObject(hdcRed, hOldRed);
    DeleteObject(hRedBmp);
    DeleteDC(hdcRed);
    free(redBmpTemplate);

    free(bmpTemplate);
    SelectObject(hdcCanvas, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcCanvas);
    ReleaseDC(NULL, hdcScreen);

    printf("      NCC Template Matching tests passed!\n");
}

static void test_adaptive_edge_detection(void) {
    printf("[3/4] Running test_adaptive_edge_detection...\n");

    /* Create synthetic 500x500 canvas */
    HDC hdcScreen = GetDC(NULL);
    HDC hdcCanvas = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 500;
    bi.bmiHeader.biHeight = -500; /* top-down */
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = NULL;
    HBITMAP hBmp = CreateDIBSection(hdcCanvas, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    HGDIOBJ hOld = SelectObject(hdcCanvas, hBmp);

    /* Fill canvas with light background RGB(245, 245, 245) */
    RECT bgRect = { 0, 0, 500, 500 };
    HBRUSH hBgBrush = CreateSolidBrush(RGB(245, 245, 245));
    FillRect(hdcCanvas, &bgRect, hBgBrush);
    DeleteObject(hBgBrush);

    /* Draw rectangular button: Left=200, Top=220, Right=300, Bottom=270 (Width=100, Height=50) */
    RECT btnRect = { 200, 220, 300, 270 };
    HBRUSH hBtnBrush = CreateSolidBrush(RGB(50, 120, 200));
    FillRect(hdcCanvas, &btnRect, hBtnBrush);
    DeleteObject(hBtnBrush);

    /* Frame button with dark 1px border */
    HBRUSH hBorderBrush = CreateSolidBrush(RGB(20, 20, 20));
    FrameRect(hdcCanvas, &btnRect, hBorderBrush);
    DeleteObject(hBorderBrush);
    GdiFlush();

    /* Click inside button at (250, 245) */
    RECT outRect;
    memset(&outRect, 0, sizeof(outRect));
    BYTE* outBmp = NULL;
    DWORD outBmpSize = 0;

    BOOL success = ttp_adaptive_crop_button(hdcCanvas, 250, 245, &outRect, &outBmp, &outBmpSize);
    assert(success == TRUE);
    assert(outBmp != NULL);
    assert(outBmpSize > 54);

    printf("      Detected Button Rect: [%ld, %ld, %ld, %ld], size %ldx%ld\n",
           outRect.left, outRect.top, outRect.right, outRect.bottom,
           outRect.right - outRect.left, outRect.bottom - outRect.top);

    /* Assert bounding box is very close to [200, 220, 300, 270] (+- 2 pixels) */
    assert(abs(outRect.left - 200) <= 2);
    assert(abs(outRect.top - 220) <= 2);
    assert(abs(outRect.right - 300) <= 2);
    assert(abs(outRect.bottom - 270) <= 2);

    /* Verify BMP header in output buffer */
    BITMAPFILEHEADER* bmfh = (BITMAPFILEHEADER*)outBmp;
    BITMAPINFOHEADER* bmih = (BITMAPINFOHEADER*)(outBmp + sizeof(BITMAPFILEHEADER));
    assert(bmfh->bfType == 0x4D42);
    assert(bmih->biWidth == (outRect.right - outRect.left));
    assert(bmih->biHeight == (outRect.bottom - outRect.top));
    assert(bmih->biBitCount == 24);

    ttp_free_bmp_buffer(outBmp);

    SelectObject(hdcCanvas, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcCanvas);
    ReleaseDC(NULL, hdcScreen);

    printf("      Adaptive Edge Detection tests passed!\n");
}

static void test_find_elements_by_text(void) {
    printf("[4/4] Running test_find_elements_by_text...\n");

    /* Create temporary test window with unique text */
    const char* uniqueText = "TTP_Unique_Button_Test";
    HWND hwndBtn = CreateWindowA("STATIC", uniqueText,
                                 WS_POPUP | WS_VISIBLE,
                                 120, 140, 160, 40,
                                 NULL, NULL, NULL, NULL);
    assert(hwndBtn != NULL);
    ShowWindow(hwndBtn, SW_SHOW);
    UpdateWindow(hwndBtn);

    POINT centers[5];
    memset(centers, 0, sizeof(centers));
    int count = ttp_find_elements_by_text(uniqueText, centers, 5);

    printf("      Found %d element(s) matching '%s'\n", count, uniqueText);
    assert(count >= 1);
    /* Window rect center should be 120 + 160/2 = 200, 140 + 40/2 = 160 */
    assert(abs(centers[0].x - 200) <= 5);
    assert(abs(centers[0].y - 160) <= 5);

    DestroyWindow(hwndBtn);
    printf("      Find Elements By Text tests passed!\n");
}

int main(void) {
    printf("=========================================\n");
    printf("Starting TinyTask Pro Vision Engine Tests\n");
    printf("=========================================\n");

    test_distance_and_nearest();
    test_ncc_template_matching();
    test_adaptive_edge_detection();
    test_find_elements_by_text();

    printf("=========================================\n");
    printf("ALL VISION TESTS PASSED SUCCESSFULLY!\n");
    printf("=========================================\n");
    return 0;
}
