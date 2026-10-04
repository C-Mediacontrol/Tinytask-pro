#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <windows.h>
#include "ttp_vision.h"

/* Test 1: Solid background + centered icon -> border masked (0), icon kept (1) */
static void test_chromakey_solid_background(void) {
    printf("[1/4] Running test_chromakey_solid_background...\n");

    /* 1A. Test 24bpp BGR buffer (w=40, h=40, total=1600) */
    {
        int w = 40, h = 40;
        int bpp = 3;
        BYTE* pixels = (BYTE*)malloc((size_t)w * h * bpp);
        assert(pixels != NULL);
        BYTE* mask = (BYTE*)malloc((size_t)w * h);
        assert(mask != NULL);

        /* Fill solid light background (B=210, G=210, R=210) */
        for (int i = 0; i < w * h; i++) {
            pixels[i * 3 + 0] = 210;
            pixels[i * 3 + 1] = 210;
            pixels[i * 3 + 2] = 210;
        }

        /* Centered icon: x in [10, 29], y in [10, 29] (20x20 = 400 pixels, ratio 25%) */
        for (int y = 10; y < 30; y++) {
            for (int x = 10; x < 30; x++) {
                int idx = y * w + x;
                pixels[idx * 3 + 0] = 15;
                pixels[idx * 3 + 1] = 15;
                pixels[idx * 3 + 2] = 15;
            }
        }

        BOOL res = ttp_chromakey_mask(pixels, w, h, bpp, 25, mask);
        assert(res == TRUE);

        /* Verify borders and background pixels are 0 */
        assert(mask[0] == 0);
        assert(mask[w - 1] == 0);
        assert(mask[(h - 1) * w] == 0);
        assert(mask[w * h - 1] == 0);
        assert(mask[5 * w + 5] == 0);

        /* Verify centered icon pixels are 1 */
        for (int y = 10; y < 30; y++) {
            for (int x = 10; x < 30; x++) {
                assert(mask[y * w + x] == 1);
            }
        }

        /* Check foreground count */
        int fgCount = 0;
        for (int i = 0; i < w * h; i++) {
            if (mask[i] == 1) fgCount++;
        }
        assert(fgCount == 400);

        free(pixels);
        free(mask);
    }

    /* 1B. Test 32bpp BGRA buffer with default tol (tol=0 -> 25) */
    {
        int w = 40, h = 40;
        int bpp = 4;
        BYTE* pixels = (BYTE*)malloc((size_t)w * h * bpp);
        assert(pixels != NULL);
        BYTE* mask = (BYTE*)malloc((size_t)w * h);
        assert(mask != NULL);

        for (int i = 0; i < w * h; i++) {
            pixels[i * 4 + 0] = 180;
            pixels[i * 4 + 1] = 190;
            pixels[i * 4 + 2] = 200;
            pixels[i * 4 + 3] = 255;
        }

        /* Centered icon 20x20 */
        for (int y = 10; y < 30; y++) {
            for (int x = 10; x < 30; x++) {
                int idx = y * w + x;
                pixels[idx * 4 + 0] = 20;
                pixels[idx * 4 + 1] = 20;
                pixels[idx * 4 + 2] = 20;
                pixels[idx * 4 + 3] = 255;
            }
        }

        BOOL res = ttp_chromakey_mask(pixels, w, h, bpp, 0, mask); /* tol=0 -> default 25 */
        assert(res == TRUE);

        assert(mask[0] == 0);
        assert(mask[35 * w + 35] == 0);
        for (int y = 10; y < 30; y++) {
            for (int x = 10; x < 30; x++) {
                assert(mask[y * w + x] == 1);
            }
        }

        free(pixels);
        free(mask);
    }

    printf("      test_chromakey_solid_background passed!\n");
}

/* Test 2: Gradient background within tolerance -> cleanly masked */
static void test_chromakey_gradient_background(void) {
    printf("[2/4] Running test_chromakey_gradient_background...\n");

    int w = 50, h = 50;
    int bpp = 4;
    BYTE* pixels = (BYTE*)malloc((size_t)w * h * bpp);
    assert(pixels != NULL);
    BYTE* mask = (BYTE*)malloc((size_t)w * h);
    assert(mask != NULL);

    /* Background with mild gradient: variation within +- 10 levels */
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int idx = y * w + x;
            pixels[idx * 4 + 0] = (BYTE)(195 + (x * 8) / w);  /* 195 .. 202 */
            pixels[idx * 4 + 1] = (BYTE)(195 + (y * 8) / h);  /* 195 .. 202 */
            pixels[idx * 4 + 2] = 195;
            pixels[idx * 4 + 3] = 255;
        }
    }

    /* Centered icon: x in [15, 34], y in [15, 34] (20x20 = 400 pixels, ratio 16%) */
    for (int y = 15; y < 35; y++) {
        for (int x = 15; x < 35; x++) {
            int idx = y * w + x;
            pixels[idx * 4 + 0] = 30;
            pixels[idx * 4 + 1] = 40;
            pixels[idx * 4 + 2] = 50;
            pixels[idx * 4 + 3] = 255;
        }
    }

    BOOL res = ttp_chromakey_mask(pixels, w, h, bpp, 25, mask);
    assert(res == TRUE);

    /* Borders and outside background must be cleanly masked (0) */
    assert(mask[0] == 0);
    assert(mask[w - 1] == 0);
    assert(mask[(h - 1) * w] == 0);
    assert(mask[w * h - 1] == 0);
    assert(mask[5 * w + 5] == 0);
    assert(mask[45 * w + 45] == 0);

    /* Icon pixels must be preserved (1) */
    for (int y = 15; y < 35; y++) {
        for (int x = 15; x < 35; x++) {
            assert(mask[y * w + x] == 1);
        }
    }

    int fgCount = 0;
    for (int i = 0; i < w * h; i++) {
        if (mask[i] == 1) fgCount++;
    }
    assert(fgCount == 400);

    free(pixels);
    free(mask);
    printf("      test_chromakey_gradient_background passed!\n");
}

/* Test 3: Over-carving / high-variance image -> safety fallback triggered (all 1s) */
static void test_chromakey_safety_fallback(void) {
    printf("[3/4] Running test_chromakey_safety_fallback...\n");

    /* 3A: Over-carving fallback (foreground ratio < 15%) */
    {
        int w = 40, h = 40; /* 1600 pixels */
        BYTE* pixels = (BYTE*)malloc((size_t)w * h * 3);
        assert(pixels != NULL);
        BYTE* mask = (BYTE*)malloc((size_t)w * h);
        assert(mask != NULL);

        /* Solid background */
        for (int i = 0; i < w * h; i++) {
            pixels[i * 3 + 0] = 220;
            pixels[i * 3 + 1] = 220;
            pixels[i * 3 + 2] = 220;
        }

        /* Tiny icon 3x3 = 9 pixels (ratio = 9 / 1600 = 0.56% < 15%) */
        for (int y = 18; y <= 20; y++) {
            for (int x = 18; x <= 20; x++) {
                int idx = y * w + x;
                pixels[idx * 3 + 0] = 10;
                pixels[idx * 3 + 1] = 10;
                pixels[idx * 3 + 2] = 10;
            }
        }

        BOOL res = ttp_chromakey_mask(pixels, w, h, 3, 25, mask);
        assert(res == TRUE);

        /* Safety fallback must revert ALL pixels to 1 */
        for (int i = 0; i < w * h; i++) {
            assert(mask[i] == 1);
        }

        free(pixels);
        free(mask);
    }

    /* 3B: Solid uniform image (0% foreground -> over-carving fallback) */
    {
        int w = 30, h = 30;
        BYTE* pixels = (BYTE*)malloc((size_t)w * h * 4);
        assert(pixels != NULL);
        BYTE* mask = (BYTE*)malloc((size_t)w * h);
        assert(mask != NULL);

        for (int i = 0; i < w * h; i++) {
            pixels[i * 4 + 0] = 255;
            pixels[i * 4 + 1] = 255;
            pixels[i * 4 + 2] = 255;
            pixels[i * 4 + 3] = 255;
        }

        BOOL res = ttp_chromakey_mask(pixels, w, h, 4, 25, mask);
        assert(res == TRUE);

        for (int i = 0; i < w * h; i++) {
            assert(mask[i] == 1);
        }

        free(pixels);
        free(mask);
    }

    /* 3C: High-variance border fallback (sigma_B > 60) */
    {
        int w = 40, h = 40;
        BYTE* pixels = (BYTE*)malloc((size_t)w * h * 4);
        assert(pixels != NULL);
        BYTE* mask = (BYTE*)malloc((size_t)w * h);
        assert(mask != NULL);

        /* Fill canvas with alternating black/white checkerboard or noise on border */
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int idx = y * w + x;
                BYTE val = ((x + y) % 2 == 0) ? 0 : 255;
                pixels[idx * 4 + 0] = val;
                pixels[idx * 4 + 1] = val;
                pixels[idx * 4 + 2] = val;
                pixels[idx * 4 + 3] = 255;
            }
        }

        BOOL res = ttp_chromakey_mask(pixels, w, h, 4, 25, mask);
        assert(res == TRUE);

        /* Safety fallback must keep all 1s due to high border variance */
        for (int i = 0; i < w * h; i++) {
            assert(mask[i] == 1);
        }

        free(pixels);
        free(mask);
    }

    printf("      test_chromakey_safety_fallback passed!\n");
}

/* Test 4: ttp_adaptive_crop_button generates valid 32bpp BMP with alpha mask */
static void test_adaptive_crop_button_32bpp_mask(void) {
    printf("[4/4] Running test_adaptive_crop_button_32bpp_mask...\n");

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

    /* Background: light gray RGB(245, 245, 245) */
    RECT bgRect = { 0, 0, 500, 500 };
    HBRUSH hBgBrush = CreateSolidBrush(RGB(245, 245, 245));
    FillRect(hdcCanvas, &bgRect, hBgBrush);
    DeleteObject(hBgBrush);

    /* Draw button at [200, 220, 300, 270] */
    RECT btnRect = { 200, 220, 300, 270 };
    HBRUSH hBtnBrush = CreateSolidBrush(RGB(50, 120, 200));
    FillRect(hdcCanvas, &btnRect, hBtnBrush);
    DeleteObject(hBtnBrush);

    HBRUSH hBorderBrush = CreateSolidBrush(RGB(20, 20, 20));
    FrameRect(hdcCanvas, &btnRect, hBorderBrush);
    DeleteObject(hBorderBrush);
    GdiFlush();

    RECT outRect;
    memset(&outRect, 0, sizeof(outRect));
    BYTE* outBmp = NULL;
    DWORD outBmpSize = 0;

    BOOL success = ttp_adaptive_crop_button(hdcCanvas, 250, 245, &outRect, &outBmp, &outBmpSize);
    assert(success == TRUE);
    assert(outBmp != NULL);
    assert(outBmpSize > sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER));

    BITMAPFILEHEADER* bmfh = (BITMAPFILEHEADER*)outBmp;
    BITMAPINFOHEADER* bmih = (BITMAPINFOHEADER*)(outBmp + sizeof(BITMAPFILEHEADER));

    assert(bmfh->bfType == 0x4D42);
    /* Verify standard 32bpp BGRA */
    assert(bmih->biBitCount == 32);
    assert(bmih->biCompression == BI_RGB);

    int cropW = bmih->biWidth;
    int cropH = abs(bmih->biHeight);
    assert(cropW == (outRect.right - outRect.left));
    assert(cropH == (outRect.bottom - outRect.top));

    DWORD expectedImgSize = (DWORD)cropW * 4 * cropH;
    DWORD expectedTotal = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + expectedImgSize;
    assert(bmfh->bfSize == expectedTotal);
    assert(outBmpSize == expectedTotal);

    /* Check Alpha channel values */
    const BYTE* dstData = outBmp + bmfh->bfOffBits;
    int fgCount = 0;
    for (int i = 0; i < cropW * cropH; i++) {
        BYTE a = dstData[i * 4 + 3];
        assert(a == 0 || a == 255);
        if (a == 255) fgCount++;
    }
    assert(fgCount > 0);

    ttp_free_bmp_buffer(outBmp);
    SelectObject(hdcCanvas, hOld);
    DeleteObject(hBmp);
    DeleteDC(hdcCanvas);
    ReleaseDC(NULL, hdcScreen);

    printf("      test_adaptive_crop_button_32bpp_mask passed!\n");
}

int main(void) {
    printf("=========================================\n");
    printf("Starting TinyTask Pro Chromakey Tests\n");
    printf("=========================================\n");

    test_chromakey_solid_background();
    test_chromakey_gradient_background();
    test_chromakey_safety_fallback();
    test_adaptive_crop_button_32bpp_mask();

    printf("=========================================\n");
    printf("ALL CHROMAKEY TESTS PASSED SUCCESSFULLY!\n");
    printf("=========================================\n");
    return 0;
}
