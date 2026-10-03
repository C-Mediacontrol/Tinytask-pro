#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <windows.h>
#include "ttp_core.h"
#include "ttp_vision.h"
#include "ttp_engine.h"

/* Helper: Create a 24bpp BMP in memory with a distinct pattern */
static BYTE* create_test_pattern_bmp(int w, int h, DWORD* outSize) {
    int rowStride = ((w * 3 + 3) / 4) * 4;
    DWORD imgSize = rowStride * h;
    DWORD totalSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + imgSize;
    BYTE* buf = (BYTE*)calloc(1, totalSize);
    BITMAPFILEHEADER* bfh = (BITMAPFILEHEADER*)buf;
    bfh->bfType = 0x4D42;
    bfh->bfSize = totalSize;
    bfh->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    BITMAPINFOHEADER* bih = (BITMAPINFOHEADER*)(buf + sizeof(BITMAPFILEHEADER));
    bih->biSize = sizeof(BITMAPINFOHEADER);
    bih->biWidth = w;
    bih->biHeight = -h; /* Top-down BMP */
    bih->biPlanes = 1;
    bih->biBitCount = 24;
    bih->biSizeImage = imgSize;

    BYTE* px = buf + bfh->bfOffBits;
    for (int y = 0; y < h; y++) {
        BYTE* row = px + y * rowStride;
        for (int x = 0; x < w; x++) {
            /* Create high-contrast checkerboard/edge pattern */
            BYTE v = ((x / 4) % 2 == (y / 4) % 2) ? 230 : 20;
            row[x * 3 + 0] = v;
            row[x * 3 + 1] = v;
            row[x * 3 + 2] = v;
        }
    }
    *outSize = totalSize;
    return buf;
}

/* Mock timeout callback that records if timeout dialog was triggered */
static int s_timeoutTriggered = 0;
static int mock_timeout_cb(const TTPStep* step, void* userData) {
    (void)step; (void)userData;
    s_timeoutTriggered = 1;
    return TTP_TIMEOUT_STOP;
}

int main() {
    printf("==========================================\n");
    printf("Running TinyTask Pro Bugfix Regression Tests\n");
    printf("==========================================\n");
    fflush(stdout);

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
    fflush(stdout);

    // Test 3: Micro-jitter clicks correctly synthesized as CLICK
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
    printf("Test 3: Micro-jitter clicks correctly recorded as CLICK\n");
    fflush(stdout);

    // Test 4: Pure ASCII validation on source code drawer buttons and menus
    FILE* fp = fopen("reverse-gemini/src/tinytask_pro.c", "rb");
    assert(fp != NULL);
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char* srcContent = (char*)malloc(sz + 1);
    fread(srcContent, 1, sz, fp);
    srcContent[sz] = '\0';
    fclose(fp);

    /* Assert that non-ASCII escape byte sequences are NOT present in tinytask_pro.c */
    BOOL hasEsc1 = (strstr(srcContent, "\\xc3\\x97") != NULL);
    BOOL hasEsc2 = (strstr(srcContent, "\\xe2\\x96") != NULL);
    BOOL hasEsc3 = (strstr(srcContent, "\\xbd") != NULL);
    BOOL hasEsc4 = (strstr(srcContent, "\\x95") != NULL);
    printf("Test 4 (ASCII Check): hasEsc1=%d, hasEsc2=%d, hasEsc3=%d, hasEsc4=%d\n",
           hasEsc1, hasEsc2, hasEsc3, hasEsc4);
    fflush(stdout);
    free(srcContent);
    assert(!hasEsc1 && !hasEsc2 && !hasEsc3 && !hasEsc4); // FAILS ON CURRENT CODE (RED)

    // Test 5: Full-Screen Large Displacement NCC (e.g. 2560x1440 canvas, moved 1800+ px)
    int screenW = 2560, screenH = 1440;
    int tW = 64, tH = 32;
    DWORD tBmpSize = 0;
    BYTE* tBmp = create_test_pattern_bmp(tW, tH, &tBmpSize);

    HDC hdcScr = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScr);
    HBITMAP hBmpCanvas = CreateCompatibleBitmap(hdcScr, screenW, screenH);
    SelectObject(hdcMem, hBmpCanvas);

    // Fill background with light neutral color
    RECT rcBg = { 0, 0, screenW, screenH };
    HBRUSH hbrBg = CreateSolidBrush(RGB(240, 240, 240));
    FillRect(hdcMem, &rcBg, hbrBg);
    DeleteObject(hbrBg);

    // Render button template at (1950, 1150) - displaced by 1900+ pixels from origin (50, 50)
    int targetX = 1950, targetY = 1150;
    for (int y = 0; y < tH; y++) {
        for (int x = 0; x < tW; x++) {
            BYTE v = ((x / 4) % 2 == (y / 4) % 2) ? 230 : 20;
            SetPixel(hdcMem, targetX + x, targetY + y, RGB(v, v, v));
        }
    }

    POINT matchPos = { 50, 50 }; // Origin was far away
    double score = 0.0;
    DWORD tStart = GetTickCount();
    BOOL matched = ttp_match_template_ncc(hdcMem, screenW, screenH, tBmp, tBmpSize, 0.75, &matchPos, &score);
    DWORD dur = GetTickCount() - tStart;
    printf("Test 5 (Full Screen NCC): matched=%d, pos=(%ld,%ld), score=%.3f, dur=%lu ms\n",
           matched, matchPos.x, matchPos.y, score, dur);
    fflush(stdout);
    assert(matched == TRUE);
    assert(abs(matchPos.x - (targetX + tW / 2)) <= 2);
    assert(abs(matchPos.y - (targetY + tH / 2)) <= 2);
    assert(score >= 0.75);
    assert(dur < 120);

    // Test 6: Dual-Engine Visual Fallback in ttp_playback_step
    // When targetMode is TTP_TARGET_TEXT but text is NOT found, engine must fall back to image template
    ttp_engine_set_timeout_callback(mock_timeout_cb, NULL);
    ttp_engine_set_screen_dc_override(hdcMem);
    s_timeoutTriggered = 0;

    TTPStep testStep;
    memset(&testStep, 0, sizeof(testStep));
    testStep.stepId = 1;
    testStep.actionType = TTP_ACTION_CLICK;
    testStep.targetMode = TTP_TARGET_TEXT; // Recorded as Text
    strcpy(testStep.textKey, "NonExistentWindowText_Button12345");
    testStep.origX = 50;
    testStep.origY = 50;
    testStep.timeoutMs = 500; // Short timeout
    testStep.postDelayMs = 0;

    // Execute playback step: should NOT trigger timeout dialog if dual-engine fallback succeeds
    BOOL playRes = ttp_playback_step(&testStep, tBmp, tBmpSize, NULL);
    ttp_engine_set_screen_dc_override(NULL);
    printf("Test 6 (Dual-Engine Fallback): playRes=%d, timeoutTriggered=%d\n", playRes, s_timeoutTriggered);
    fflush(stdout);
    assert(s_timeoutTriggered == 0); // Must not timeout
    assert(playRes == TRUE);

    DeleteObject(hBmpCanvas);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScr);
    free(tBmp);

    printf("==========================================\n");
    printf("ALL REGRESSION TESTS PASSED (GREEN)!\n");
    printf("==========================================\n");
    return 0;
}
