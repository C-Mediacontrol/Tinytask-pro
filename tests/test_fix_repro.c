#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <windows.h>
#include "ttp_core.h"
#include "ttp_vision.h"
#include "ttp_engine.h"

int main() {
    printf("==========================================\n");
    printf("Running TinyTask Pro Bugfix Regression Tests\n");
    printf("==========================================\n");

    // Test 1: Verify timeout dialog class is registered
    ttp_register_timeout_dialog_class(NULL);
    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    BOOL registered = GetClassInfoExA(GetModuleHandleA(NULL), "TTPTimeoutDialog", &wc);
    DWORD err = GetLastError();
    printf("Test 1: TTPTimeoutDialog registered = %d, err = %lu\n", registered, err);
    fflush(stdout);
    assert(registered != 0);

    // Test 2: Verify macro encoding of timeout fallback action in targetMode
    DWORD baseMode = TTP_TARGET_IMAGE;
    int action = TTP_TIMEOUT_ACT_USE_RECORDED;
    DWORD combined = TTP_MAKE_TARGET_MODE(baseMode, action);
    assert(TTP_GET_BASE_TARGET_MODE(combined) == TTP_TARGET_IMAGE);
    assert(TTP_GET_TIMEOUT_ACTION(combined) == TTP_TIMEOUT_ACT_USE_RECORDED);
    printf("Test 2: Target mode macro roundtrip passed\n");

    // Test 3: Verify NCC speedup on a synthetic test canvas
    int w = 2560, h = 1440;
    int tW = 60, tH = 30;
    DWORD tBmpSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + tW * tH * 3;
    BYTE* bmpData = (BYTE*)calloc(1, tBmpSize);
    BITMAPFILEHEADER* bfh = (BITMAPFILEHEADER*)bmpData;
    bfh->bfType = 0x4D42;
    bfh->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)(bmpData + sizeof(BITMAPFILEHEADER));
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = tW;
    bih->biHeight = tH;
    bih->biPlanes = 1;
    bih->biBitCount = 24;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, w, h);
    SelectObject(hdcMem, hBmp);

    POINT matchPos = {0};
    double score = 0;
    DWORD t0 = GetTickCount();
    BOOL matchRes = ttp_match_template_ncc(hdcMem, w, h, bmpData, tBmpSize, 0.80, &matchPos, &score);
    DWORD elapsed = GetTickCount() - t0;
    printf("Test 3: Coarse-to-fine NCC completed in %lu ms (limit 200 ms)\n", elapsed);
    assert(elapsed < 200);

    DeleteObject(hBmp);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    free(bmpData);

    // Test 4: Two consecutive clicks with micro-jitters
    ttp_synth_reset();
    ttp_synth_add_mouse_event(WM_LBUTTONDOWN, 542, 474, 1000);
    ttp_synth_add_mouse_event(WM_MOUSEMOVE, 552, 478, 1050);
    ttp_synth_add_mouse_event(WM_LBUTTONUP, 552, 478, 1100);

    ttp_synth_add_mouse_event(WM_LBUTTONDOWN, 740, 474, 2000);
    ttp_synth_add_mouse_event(WM_MOUSEMOVE, 749, 481, 2050);
    ttp_synth_add_mouse_event(WM_LBUTTONUP, 749, 481, 2100);

    TTPStep steps[8];
    memset(steps, 0, sizeof(steps));
    DWORD stepCount = ttp_synth_finalize(steps, 8);
    assert(stepCount == 2);
    assert(steps[0].actionType == TTP_ACTION_CLICK);
    assert(steps[1].actionType == TTP_ACTION_CLICK);
    printf("Test 4: Micro-jitter clicks correctly recorded as CLICK\n");

    printf("==========================================\n");
    printf("ALL REGRESSION TESTS PASSED (GREEN)!\n");
    printf("==========================================\n");
    return 0;
}
