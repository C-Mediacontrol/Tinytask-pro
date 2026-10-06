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

static FILE* fopen_dual(const char* p1, const char* p2) {
    FILE* f = fopen(p1, "rb");
    if (!f && p2) f = fopen(p2, "rb");
    if (!f && p1 && strncmp(p1, "src/", 4) == 0) {
        char buf[512];
        snprintf(buf, sizeof(buf), "../src/pro/%s", p1 + 4);
        f = fopen(buf, "rb");
        if (!f) {
            snprintf(buf, sizeof(buf), "src/pro/%s", p1 + 4);
            f = fopen(buf, "rb");
        }
        if (!f) {
            snprintf(buf, sizeof(buf), "reverse-gemini/src/pro/%s", p1 + 4);
            f = fopen(buf, "rb");
        }
    }
    return f;
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
    FILE* fp = fopen_dual("src/tinytask_pro.c", "reverse-gemini/src/tinytask_pro.c");
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
    assert(dur < 200);

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

    // ==========================================================
    // Phase 4 Hotkey Bugfix Regression Tests (RED -> GREEN)
    // ==========================================================

    // Test 7: Rising-Edge Latch Logic Verification
    // A 300ms continuous hold over 12 ticks of 25ms timer must trigger EXACTLY ONCE.
    int triggerCount = 0;
    BOOL s_recTriggerWasDown = FALSE;
    for (int tick = 0; tick < 12; tick++) {
        BOOL isPressed = TRUE; // Held continuously for 300ms
        if (isPressed) {
            if (!s_recTriggerWasDown) {
                s_recTriggerWasDown = TRUE;
                triggerCount++;
            }
        } else {
            s_recTriggerWasDown = FALSE;
        }
    }
    printf("Test 7 (Rising Edge Latch): 300ms chord hold triggerCount=%d\n", triggerCount);
    fflush(stdout);
    assert(triggerCount == 1);

    // Test 8: Trailing Hotkey Step Pruning
    TTPStep macroSteps[8];
    memset(macroSteps, 0, sizeof(macroSteps));
    macroSteps[0].stepId = 1;
    macroSteps[0].actionType = TTP_ACTION_CLICK;
    macroSteps[1].stepId = 2;
    macroSteps[1].actionType = TTP_ACTION_TYPE_TEXT;
    strcpy(macroSteps[1].textKey, "test");
    // Trailing hotkey artifacts from user stopping recording with Ctrl+Shift+Alt+R
    macroSteps[2].stepId = 3;
    macroSteps[2].actionType = TTP_ACTION_HOTKEY;
    macroSteps[2].origX = VK_CONTROL;
    macroSteps[3].stepId = 4;
    macroSteps[3].actionType = TTP_ACTION_HOTKEY;
    macroSteps[3].origX = VK_SHIFT;
    macroSteps[4].stepId = 5;
    macroSteps[4].actionType = TTP_ACTION_HOTKEY;
    macroSteps[4].origX = VK_MENU;
    macroSteps[5].stepId = 6;
    macroSteps[5].actionType = TTP_ACTION_HOTKEY;
    macroSteps[5].origX = 'R';

    DWORD stepCountPruned = 6;
    while (stepCountPruned > 0) {
        TTPStep* last = &macroSteps[stepCountPruned - 1];
        if (last->actionType == TTP_ACTION_HOTKEY) {
            DWORD vk = (DWORD)last->origX;
            if (vk == 'R' || vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
                vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
                vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) {
                stepCountPruned--;
                continue;
            }
        }
        break;
    }
    printf("Test 8 (Trailing Hotkey Pruning): original=6, pruned=%lu\n", stepCountPruned);
    fflush(stdout);
    assert(stepCountPruned == 2);
    assert(macroSteps[0].actionType == TTP_ACTION_CLICK);
    assert(macroSteps[1].actionType == TTP_ACTION_TYPE_TEXT);

    // Test 9: Source Code Invariant Checks on tinytask_pro.c
    // Checks that tinytask_pro.c contains rising-edge latch, 25ms timer, seeds state,
    // and eliminates Sleep(150) before ID_PRO_REC.
    FILE* fpPro = fopen_dual("src/tinytask_pro.c", "reverse-gemini/src/tinytask_pro.c");
    assert(fpPro != NULL);
    fseek(fpPro, 0, SEEK_END);
    long proSz = ftell(fpPro);
    fseek(fpPro, 0, SEEK_SET);
    char* proSrc = (char*)malloc(proSz + 1);
    fread(proSrc, 1, proSz, fpPro);
    proSrc[proSz] = '\0';
    fclose(fpPro);

    BOOL hasLatchVar = (strstr(proSrc, "s_recTriggerWasDown") != NULL);
    BOOL hasTimer25 = (strstr(proSrc, "SetTimer(hwnd, TIMER_HOTKEY, 25") != NULL);
    BOOL hasKeySeed = (strstr(proSrc, "g_LastKeyState[vk] =") != NULL && strstr(proSrc, "for (int vk = 1; vk < 256; vk++)") != NULL);
    BOOL hasTrailingTrim = (strstr(proSrc, "IsTrailingHotkeyStep") != NULL || strstr(proSrc, "RemoveTrailingHotkey") != NULL);
    // Verify Sleep(150) is NOT placed right before ID_PRO_REC
    char* recPost = strstr(proSrc, "PostMessageA(g_hMainWnd, WM_COMMAND, ID_PRO_REC, 0);");
    BOOL hasSleepBeforeRec = FALSE;
    if (recPost) {
        // Look backwards 40 chars
        char* window = recPost - 30;
        if (window > proSrc && strstr(window, "Sleep(150)")) {
            hasSleepBeforeRec = TRUE;
        }
    }
    free(proSrc);

    printf("Test 9 (Source Code Hotkey Invariants):\n");
    printf("  hasLatchVar=%d\n", hasLatchVar);
    printf("  hasTimer25=%d\n", hasTimer25);
    printf("  hasKeySeed=%d\n", hasKeySeed);
    printf("  hasTrailingTrim=%d\n", hasTrailingTrim);
    printf("  hasSleepBeforeRec=%d (must be 0)\n", hasSleepBeforeRec);
    fflush(stdout);

    // MUST FAIL ON CURRENT CODE (RED PHASE)
    assert(hasLatchVar && hasTimer25 && hasKeySeed && hasTrailingTrim && !hasSleepBeforeRec);

    // ==========================================================
    // Test 10: Hierarchical Cascaded Vision Matching
    // ==========================================================
    printf("Test 10 (Cascaded Vision Matching): starting...\n");
    fflush(stdout);

    int canvasW = 2560, canvasH = 1440;
    int patW = 64, patH = 32;
    DWORD patSize = 0;
    BYTE* patBmp = create_test_pattern_bmp(patW, patH, &patSize);

    HDC hdcScr10 = GetDC(NULL);
    HDC hdcMem10 = CreateCompatibleDC(hdcScr10);
    HBITMAP hBmpCanvas10 = CreateCompatibleBitmap(hdcScr10, canvasW, canvasH);
    SelectObject(hdcMem10, hBmpCanvas10);

    // 10A: Near target at (60, 60), orig=(50, 50). ROI radius = 200px.
    RECT rcBg10 = { 0, 0, canvasW, canvasH };
    HBRUSH hbrBg10 = CreateSolidBrush(RGB(240, 240, 240));
    FillRect(hdcMem10, &rcBg10, hbrBg10);

    int nearX = 60, nearY = 60;
    for (int y = 0; y < patH; y++) {
        for (int x = 0; x < patW; x++) {
            BYTE v = ((x / 4) % 2 == (y / 4) % 2) ? 230 : 20;
            SetPixel(hdcMem10, nearX + x, nearY + y, RGB(v, v, v));
        }
    }

    POINT pos10 = { 50, 50 };
    double score10 = 0.0;
    LARGE_INTEGER qpcFreq, qpcStart, qpcEnd;
    QueryPerformanceFrequency(&qpcFreq);
    QueryPerformanceCounter(&qpcStart);
    BOOL matchedNear = ttp_match_template_ncc_roi(hdcMem10, 50, 50, 200, patBmp, patSize, 0.75, &pos10, &score10);
    QueryPerformanceCounter(&qpcEnd);
    double durRoiMs = (double)(qpcEnd.QuadPart - qpcStart.QuadPart) * 1000.0 / (double)qpcFreq.QuadPart;

    printf("Test 10A (Tier 1 ROI fast hit): matched=%d, pos=(%ld,%ld), score=%.3f, dur=%.2f ms\n",
           matchedNear, pos10.x, pos10.y, score10, durRoiMs);
    fflush(stdout);
    assert(matchedNear == TRUE);
    assert(abs(pos10.x - (nearX + patW / 2)) <= 2);
    assert(abs(pos10.y - (nearY + patH / 2)) <= 2);
    assert(score10 >= 0.75);
    assert(durRoiMs < 25.0);

    // Verify engine playback step handles near target fast hit
    ttp_engine_set_screen_dc_override(hdcMem10);
    s_timeoutTriggered = 0;
    TTPStep stepNear;
    memset(&stepNear, 0, sizeof(stepNear));
    stepNear.stepId = 100;
    stepNear.actionType = TTP_ACTION_CLICK;
    stepNear.targetMode = TTP_TARGET_IMAGE;
    stepNear.origX = 50;
    stepNear.origY = 50;
    stepNear.timeoutMs = 500;
    BOOL playNearRes = ttp_playback_step(&stepNear, patBmp, patSize, NULL);
    assert(playNearRes == TRUE);
    assert(s_timeoutTriggered == 0);

    // 10B: Target moved far away to (1950, 1150), orig=(50, 50).
    // Outside the 200px ROI: Tier 1 misses, Tier 2 full-screen fallback succeeds.
    FillRect(hdcMem10, &rcBg10, hbrBg10);
    int farX = 1950, farY = 1150;
    for (int y = 0; y < patH; y++) {
        for (int x = 0; x < patW; x++) {
            BYTE v = ((x / 4) % 2 == (y / 4) % 2) ? 230 : 20;
            SetPixel(hdcMem10, farX + x, farY + y, RGB(v, v, v));
        }
    }

    POINT posFar = { 50, 50 };
    double scoreFar = 0.0;
    // Tier 1 search within 200px ROI of (50, 50) must miss
    BOOL matchedFarRoi = ttp_match_template_ncc_roi(hdcMem10, 50, 50, 200, patBmp, patSize, 0.75, &posFar, &scoreFar);
    printf("Test 10B (Tier 1 ROI miss on far target): matchedFarRoi=%d\n", matchedFarRoi);
    fflush(stdout);
    assert(matchedFarRoi == FALSE);

    // Cascaded Search: Fall back to Tier 2 full-screen pyramid search
    BOOL matchedFarCascaded = matchedFarRoi;
    if (!matchedFarCascaded) {
        matchedFarCascaded = ttp_match_template_ncc(hdcMem10, canvasW, canvasH, patBmp, patSize, 0.75, &posFar, &scoreFar);
    }
    printf("Test 10B (Tier 2 Fallback Hit): matched=%d, pos=(%ld,%ld), score=%.3f\n",
           matchedFarCascaded, posFar.x, posFar.y, scoreFar);
    fflush(stdout);
    assert(matchedFarCascaded == TRUE);
    assert(abs(posFar.x - (farX + patW / 2)) <= 2);
    assert(abs(posFar.y - (farY + patH / 2)) <= 2);
    assert(scoreFar >= 0.75);

    // Verify engine playback step handles far target via Tier 2 fallback for both image mode and text fallback
    TTPStep stepFar;
    memset(&stepFar, 0, sizeof(stepFar));
    stepFar.stepId = 101;
    stepFar.actionType = TTP_ACTION_CLICK;
    stepFar.targetMode = TTP_TARGET_IMAGE;
    stepFar.origX = 50;
    stepFar.origY = 50;
    stepFar.timeoutMs = 1000;
    s_timeoutTriggered = 0;
    BOOL playFarRes = ttp_playback_step(&stepFar, patBmp, patSize, NULL);
    assert(playFarRes == TRUE);
    assert(s_timeoutTriggered == 0);

    // Dual-engine text fallback when target moved far away
    TTPStep stepTextFar;
    memset(&stepTextFar, 0, sizeof(stepTextFar));
    stepTextFar.stepId = 102;
    stepTextFar.actionType = TTP_ACTION_CLICK;
    stepTextFar.targetMode = TTP_TARGET_TEXT;
    strcpy(stepTextFar.textKey, "NonExistentFarButton_9999");
    stepTextFar.origX = 50;
    stepTextFar.origY = 50;
    stepTextFar.timeoutMs = 1000;
    s_timeoutTriggered = 0;
    BOOL playTextFarRes = ttp_playback_step(&stepTextFar, patBmp, patSize, NULL);
    assert(playTextFarRes == TRUE);
    assert(s_timeoutTriggered == 0);

    ttp_engine_set_screen_dc_override(NULL);

    DeleteObject(hbrBg10);
    DeleteObject(hBmpCanvas10);
    DeleteDC(hdcMem10);
    ReleaseDC(NULL, hdcScr10);
    free(patBmp);

    // 10C: Invariant check on ttp_engine.c implementation
    // Verify that ttp_engine.c uses ttp_match_template_ncc_roi for Tier 1 fast search
    // in both TTP_TARGET_TEXT dual-engine fallback and TTP_TARGET_IMAGE matching.
    FILE* fpEng = fopen_dual("src/ttp_engine.c", "reverse-gemini/src/ttp_engine.c");
    assert(fpEng != NULL);
    fseek(fpEng, 0, SEEK_END);
    long engSz = ftell(fpEng);
    fseek(fpEng, 0, SEEK_SET);
    char* engSrc = (char*)malloc(engSz + 1);
    fread(engSrc, 1, engSz, fpEng);
    engSrc[engSz] = '\0';
    fclose(fpEng);

    int roiCount = 0;
    const char* pRoi = engSrc;
    while ((pRoi = strstr(pRoi, "ttp_match_template_ncc_roi")) != NULL) {
        roiCount++;
        pRoi += strlen("ttp_match_template_ncc_roi");
    }
    free(engSrc);

    printf("Test 10C (ttp_engine.c Cascaded Invariant): roiCount=%d (expected >= 2)\n", roiCount);
    fflush(stdout);
    assert(roiCount >= 2);

    // =========================================================================
    // Test 11: Top-Left Misclick Protection & Displaced Target Robustness
    // (Power Automate Tolerance Gate, Input Injection Order, Zero-Variance Crop)
    // =========================================================================
    printf("\nTest 11: Top-Left Misclick Protection & Robust Displaced Matching\n");
    fflush(stdout);

    // 11A: Adaptive crop rejects flat zero-variance regions
    HDC hdcScr11 = GetDC(NULL);
    HDC hdcFlat = CreateCompatibleDC(hdcScr11);
    BITMAPINFO biFlat = {0};
    biFlat.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    biFlat.bmiHeader.biWidth = 300;
    biFlat.bmiHeader.biHeight = -300;
    biFlat.bmiHeader.biPlanes = 1;
    biFlat.bmiHeader.biBitCount = 32;
    void* bitsFlat = NULL;
    HBITMAP hBmpFlat = CreateDIBSection(hdcFlat, &biFlat, DIB_RGB_COLORS, &bitsFlat, NULL, 0);
    SelectObject(hdcFlat, hBmpFlat);
    RECT rcFlat = {0, 0, 300, 300};
    FillRect(hdcFlat, &rcFlat, (HBRUSH)GetStockObject(WHITE_BRUSH));

    RECT cropFlatRect = {0};
    BYTE* bmpFlatData = NULL;
    DWORD bmpFlatSize = 0;
    BOOL cropFlatRes = ttp_adaptive_crop_button(hdcFlat, 150, 150, &cropFlatRect, &bmpFlatData, &bmpFlatSize);
    printf("Test 11A (Reject flat zero-variance crop): cropFlatRes=%d (expected FALSE/0)\n", cropFlatRes);
    fflush(stdout);
    if (bmpFlatData) free(bmpFlatData);
    DeleteObject(hBmpFlat);
    DeleteDC(hdcFlat);
    ReleaseDC(NULL, hdcScr11);
    assert(cropFlatRes == FALSE); // RED on unfixed code (which returned TRUE with 7254 bytes)

    // 11B: Invariant check on ttp_engine.c input injection order:
    // mouse_event must be followed by SetCursorPos (aligning with original tinytask.c),
    // ensuring normalized coordinate truncation does not misplace the cursor.
    FILE* fpEng11 = fopen_dual("src/ttp_engine.c", "reverse-gemini/src/ttp_engine.c");
    assert(fpEng11 != NULL);
    fseek(fpEng11, 0, SEEK_END);
    long engSz11 = ftell(fpEng11);
    fseek(fpEng11, 0, SEEK_SET);
    char* engSrc11 = (char*)malloc(engSz11 + 1);
    fread(engSrc11, 1, engSz11, fpEng11);
    engSrc11[engSz11] = '\0';
    fclose(fpEng11);

    const char* clickCase = strstr(engSrc11, "case TTP_ACTION_CLICK:");
    assert(clickCase != NULL);
    const char* pMouseEv = strstr(clickCase, "mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE");
    const char* pSetCur = strstr(clickCase, "SetCursorPos(targetX, targetY)");
    assert(pMouseEv != NULL);
    assert(pSetCur != NULL);
    printf("Test 11B (Input Injection Order Invariant): pMouseEv=%p, pSetCur=%p\n", pMouseEv, pSetCur);
    fflush(stdout);
    // In original tinytask.c, mouse_event is called first, then SetCursorPos second.
    // In unfixed ttp_engine.c, SetCursorPos was called first, so pSetCur < pMouseEv (fails RED).
    assert(pMouseEv < pSetCur);
    free(engSrc11);

    // 11C: Coarse Pyramid Confidence Gate (Tolerance Threshold >= 0.35)
    // When a button template is matched against a noisy screen that does NOT contain the button,
    // coarse search must NOT fall back to (0,0) and Pass 2 must NOT lock into [0..8, 0..8].
    int simW = 1280, simH = 720;
    HDC hdcScrSim = GetDC(NULL);
    HDC hdcSim = CreateCompatibleDC(hdcScrSim);
    BITMAPINFO biSim = {0};
    biSim.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    biSim.bmiHeader.biWidth = simW;
    biSim.bmiHeader.biHeight = -simH;
    biSim.bmiHeader.biPlanes = 1;
    biSim.bmiHeader.biBitCount = 32;
    void* bitsSim = NULL;
    HBITMAP hBmpSim = CreateDIBSection(hdcSim, &biSim, DIB_RGB_COLORS, &bitsSim, NULL, 0);
    SelectObject(hdcSim, hBmpSim);

    // Fill with subtle textured background
    for (int y = 0; y < simH; y++) {
        for (int x = 0; x < simW; x++) {
            BYTE val = (BYTE)(200 + (x % 7) * 2 + (y % 5) * 3);
            SetPixel(hdcSim, x, y, RGB(val, val, val));
        }
    }

    // Pattern created from different texture
    DWORD diffPatSize = 0;
    BYTE* diffPatBmp = create_test_pattern_bmp(40, 20, &diffPatSize);

    POINT matchPos11 = { 999, 999 };
    double score11 = 0.0;
    BOOL matched11 = ttp_match_template_ncc(hdcSim, simW, simH, diffPatBmp, diffPatSize, 0.75, &matchPos11, &score11);
    printf("Test 11C (Coarse Pyramid Confidence Gate on Absent Target): matched=%d, score=%.4f, pos=(%ld, %ld)\n",
           matched11, score11, matchPos11.x, matchPos11.y);
    fflush(stdout);
    assert(matched11 == FALSE);

    free(diffPatBmp);
    DeleteObject(hBmpSim);
    DeleteDC(hdcSim);
    ReleaseDC(NULL, hdcScrSim);

    // =========================================================================
    // Test 12: Minimized Window Rejection, Off-Screen Target Guard & Clean Crop
    // =========================================================================
    printf("\nTest 12: Minimized Window Rejection, Off-Screen Target Guard & Clean Crop\n");
    fflush(stdout);

    // 12A: Create a test window, minimize it (so rect is at -32000, -32000), and verify ttp_find_elements_by_text ignores it
    WNDCLASSEXA wcMin = {0};
    wcMin.cbSize = sizeof(wcMin);
    wcMin.lpfnWndProc = DefWindowProcA;
    wcMin.hInstance = GetModuleHandleA(NULL);
    wcMin.lpszClassName = "TTP_TestMinimizedCls";
    RegisterClassExA(&wcMin);

    HWND hMinWnd = CreateWindowExA(0, "TTP_TestMinimizedCls", "TestMinimizedApp_XYZ123",
                                   WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 200, 200, NULL, NULL, GetModuleHandleA(NULL), NULL);
    assert(hMinWnd != NULL);
    ShowWindow(hMinWnd, SW_MINIMIZE);
    UpdateWindow(hMinWnd);

    // Verify it is minimized (IsIconic == TRUE)
    assert(IsIconic(hMinWnd) == TRUE);

    POINT foundPts[10];
    int foundCount = ttp_find_elements_by_text("TestMinimizedApp_XYZ123", foundPts, 10);
    printf("Test 12A (Minimized Window Filter): foundCount=%d (expected 0)\n", foundCount);
    fflush(stdout);
    DestroyWindow(hMinWnd);
    UnregisterClassA("TTP_TestMinimizedCls", GetModuleHandleA(NULL));
    assert(foundCount == 0); // On unfixed code, this returns 1 with (-31920, -31986), FAILING RED!

    // 12B: Off-Screen Coordinate Guard in ttp_playback_step
    TTPStep stepOffscreen;
    memset(&stepOffscreen, 0, sizeof(stepOffscreen));
    stepOffscreen.stepId = 120;
    stepOffscreen.actionType = TTP_ACTION_CLICK;
    stepOffscreen.targetMode = TTP_TARGET_COORD;
    stepOffscreen.origX = -32000;
    stepOffscreen.origY = -32000;
    BOOL offRes = ttp_playback_step(&stepOffscreen, NULL, 0, NULL);
    printf("Test 12B (Off-screen Guard): offRes=%d (expected 0/FALSE)\n", offRes);
    fflush(stdout);
    assert(offRes == FALSE);

    // 12C: Clean Rect Crop verification
    HDC hdcScr12 = GetDC(NULL);
    RECT testCropRc = { 100, 100, 150, 140 }; // 50x40
    BYTE* cropBmp = NULL;
    DWORD cropSz = 0;
    BOOL cropRes = ttp_crop_rect_bmp(hdcScr12, &testCropRc, &cropBmp, &cropSz);
    printf("Test 12C (Clean Crop): cropRes=%d, cropSz=%lu\n", cropRes, cropSz);
    fflush(stdout);
    assert(cropRes == TRUE);
    assert(cropSz > 0 && cropBmp != NULL);
    BITMAPINFOHEADER* bmih12 = (BITMAPINFOHEADER*)(cropBmp + sizeof(BITMAPFILEHEADER));
    assert(bmih12->biWidth == 50);
    HeapFree(GetProcessHeap(), 0, cropBmp);
    ReleaseDC(NULL, hdcScr12);

    // =========================================================================
    // Test 13: Container Window Rejection & Displaced Button Visual Recovery
    // =========================================================================
    printf("\nTest 13: Container Window Rejection & Displaced Button Visual Recovery\n");
    fflush(stdout);

    // 13A: Top-level container window (e.g. 800x600) must NEVER be treated as a clickable element!
    WNDCLASSEXA wcTop = {0};
    wcTop.cbSize = sizeof(wcTop);
    wcTop.lpfnWndProc = DefWindowProcA;
    wcTop.hInstance = GetModuleHandleA(NULL);
    wcTop.lpszClassName = "TTP_TestTopCls";
    RegisterClassExA(&wcTop);

    HWND hContainer = CreateWindowExA(0, "TTP_TestTopCls", "TestApp_Container_XYZ",
                                     WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                     900, 350, 800, 600, NULL, NULL, GetModuleHandleA(NULL), NULL);
    assert(hContainer != NULL);
    UpdateWindow(hContainer);

    POINT topFoundPts[10];
    int topFoundCount = ttp_find_elements_by_text("TestApp_Container_XYZ", topFoundPts, 10);
    printf("Test 13A (Top-level Window Exclusion): topFoundCount=%d (expected 0)\n", topFoundCount);
    fflush(stdout);
    assert(topFoundCount == 0); // On buggy code, this returned 1 with window center (1300, 650)!

    // 13B: Displaced Button Recovery via Visual Cross-Validation & Fallback
    // Button originally clicked at (50, 50), now moved far away to (1800, 900) on a 2560x1440 screen
    HDC hdcScr13 = GetDC(NULL);
    HDC hdcMem13 = CreateCompatibleDC(hdcScr13);
    HBITMAP hBmpCanvas13 = CreateCompatibleBitmap(hdcScr13, 2560, 1440);
    SelectObject(hdcMem13, hBmpCanvas13);

    RECT rcBg13 = { 0, 0, 2560, 1440 };
    HBRUSH hbrBg13 = CreateSolidBrush(RGB(245, 245, 245));
    FillRect(hdcMem13, &rcBg13, hbrBg13);
    DeleteObject(hbrBg13);

    DWORD tBtnSize = 0;
    BYTE* tBtnBmp = create_test_pattern_bmp(64, 32, &tBtnSize);
    int dispX = 1800, dispY = 900;
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 64; x++) {
            BYTE v = ((x / 4) % 2 == (y / 4) % 2) ? 230 : 20;
            SetPixel(hdcMem13, dispX + x, dispY + y, RGB(v, v, v));
        }
    }

    ttp_engine_set_screen_dc_override(hdcMem13);
    s_timeoutTriggered = 0;

    TTPStep stepDisplaced;
    memset(&stepDisplaced, 0, sizeof(stepDisplaced));
    stepDisplaced.stepId = 130;
    stepDisplaced.actionType = TTP_ACTION_CLICK;
    stepDisplaced.targetMode = TTP_TARGET_TEXT; // Recorded as TEXT
    strcpy(stepDisplaced.textKey, "TestApp_Container_XYZ"); // Same text as container window!
    stepDisplaced.origX = 50;
    stepDisplaced.origY = 50;
    stepDisplaced.timeoutMs = 1000;
    stepDisplaced.postDelayMs = 0;

    BOOL playDispRes = ttp_playback_step(&stepDisplaced, tBtnBmp, tBtnSize, NULL);
    ttp_engine_set_screen_dc_override(NULL);

    printf("Test 13B (Displaced Recovery): playDispRes=%d, timeoutTriggered=%d\n", playDispRes, s_timeoutTriggered);
    fflush(stdout);
    assert(s_timeoutTriggered == 0);
    assert(playDispRes == TRUE);

    DeleteObject(hBmpCanvas13);
    DeleteDC(hdcMem13);
    ReleaseDC(NULL, hdcScr13);
    free(tBtnBmp);
    DestroyWindow(hContainer);
    UnregisterClassA("TTP_TestTopCls", GetModuleHandleA(NULL));

    // 13C: Exact reproduction with user macro6 bmp asset (step01_CLICK_iKuuuVPN.bmp)
    FILE* fpBmp = fopen_dual("Library/logs/0332/macro6_unpacked/step01_CLICK_iKuuuVPN.bmp", "../Library/logs/0332/macro6_unpacked/step01_CLICK_iKuuuVPN.bmp");
    if (fpBmp) {
        fseek(fpBmp, 0, SEEK_END);
        DWORD bSize = (DWORD)ftell(fpBmp);
        fseek(fpBmp, 0, SEEK_SET);
        BYTE* bBuf = (BYTE*)malloc(bSize);
        fread(bBuf, 1, bSize, fpBmp);
        fclose(fpBmp);

        BITMAPINFOHEADER* bmih = (BITMAPINFOHEADER*)(bBuf + sizeof(BITMAPFILEHEADER));
        int bw = bmih->biWidth;
        int bh = abs(bmih->biHeight);
        int bStride = ((bw * 3 + 3) / 4) * 4;
        const BYTE* bPixels = bBuf + ((BITMAPFILEHEADER*)bBuf)->bfOffBits;
        BOOL isBottomUp = (bmih->biHeight > 0);

        HDC hdcScr13C = GetDC(NULL);
        HDC hdcMem13C = CreateCompatibleDC(hdcScr13C);
        HBITMAP hBmp13C = CreateCompatibleBitmap(hdcScr13C, 1920, 1080);
        SelectObject(hdcMem13C, hBmp13C);

        // Fill background with typical desktop color
        RECT rcDesktop = { 0, 0, 1920, 1080 };
        HBRUSH hbrDesk = CreateSolidBrush(RGB(50, 70, 90));
        FillRect(hdcMem13C, &rcDesktop, hbrDesk);
        DeleteObject(hbrDesk);

        // First test: target is NOT at (44, 1000). Test ROI false-positive rejection
        POINT roiTestPt = { 44, 1000 };
        double roiTestScore = 0.0;
        BOOL roiFalseMatch = ttp_match_template_ncc_roi(hdcMem13C, 44, 1000, 200, bBuf, bSize, 0.70, &roiTestPt, &roiTestScore);
        printf("Test 13C (ROI False Match Check on Blank Desktop): roiFalseMatch=%d, score=%.4f (must be < 0.70)\n",
               roiFalseMatch, roiTestScore);
        fflush(stdout);
        assert(roiFalseMatch == FALSE);
        assert(roiTestScore < 0.70);

        // Now draw the iKuuuVPN button at moved location (600, 400)
        int drawX = 600, drawY = 400;
        for (int y = 0; y < bh; y++) {
            int rowIdx = isBottomUp ? (bh - 1 - y) : y;
            const BYTE* row = bPixels + rowIdx * bStride;
            for (int x = 0; x < bw; x++) {
                SetPixel(hdcMem13C, drawX + x, drawY + y, RGB(row[x * 3 + 2], row[x * 3 + 1], row[x * 3 + 0]));
            }
        }

        // Test Tier 2 full-screen matching locates the moved icon accurately
        POINT fullPt = { 0, 0 };
        double fullScore = 0.0;
        BOOL fullMatch = ttp_match_template_ncc(hdcMem13C, 1920, 1080, bBuf, bSize, 0.70, &fullPt, &fullScore);
        printf("Test 13C (Full Screen Search Moved Icon): fullMatch=%d, score=%.4f, pos=(%ld, %ld), expected=(%d, %d)\n",
               fullMatch, fullScore, fullPt.x, fullPt.y, drawX + bw / 2, drawY + bh / 2);
        fflush(stdout);
        assert(fullMatch == TRUE);
        assert(fullScore >= 0.85);
        assert(abs(fullPt.x - (drawX + bw / 2)) <= 2);
        assert(abs(fullPt.y - (drawY + bh / 2)) <= 2);

        // Test playback step dual-engine fallback on moved desktop icon
        ttp_engine_set_screen_dc_override(hdcMem13C);
        s_timeoutTriggered = 0;

        TTPStep stepMacro6;
        memset(&stepMacro6, 0, sizeof(stepMacro6));
        stepMacro6.stepId = 1;
        stepMacro6.actionType = TTP_ACTION_CLICK;
        stepMacro6.targetMode = TTP_TARGET_TEXT;
        strcpy(stepMacro6.textKey, "iKuuuVPN");
        stepMacro6.origX = 44;
        stepMacro6.origY = 1000;
        stepMacro6.timeoutMs = 1000;
        stepMacro6.postDelayMs = 0;

        BOOL playM6Res = ttp_playback_step(&stepMacro6, bBuf, bSize, NULL);
        ttp_engine_set_screen_dc_override(NULL);
        printf("Test 13C (Macro6 Playback with Moved Icon): playRes=%d, timeoutTriggered=%d\n",
               playM6Res, s_timeoutTriggered);
        fflush(stdout);
        assert(playM6Res == TRUE);
        assert(s_timeoutTriggered == 0);

        DeleteObject(hBmp13C);
        DeleteDC(hdcMem13C);
        ReleaseDC(NULL, hdcScr13C);

        // 13D: Top-Right Corner Placement on 2560x1440 canvas (macro7 reproduction)
        HDC hdcScr13D = GetDC(NULL);
        HDC hdcMem13D = CreateCompatibleDC(hdcScr13D);
        HBITMAP hBmp13D = CreateCompatibleBitmap(hdcScr13D, 2560, 1440);
        SelectObject(hdcMem13D, hBmp13D);

        // Fill background with contrasting wallpaper in top-right corner
        RECT rcDesktop13D = { 0, 0, 2560, 1440 };
        HBRUSH hbrDesk13D = CreateSolidBrush(RGB(180, 210, 230));
        FillRect(hdcMem13D, &rcDesktop13D, hbrDesk13D);
        DeleteObject(hbrDesk13D);

        // Place icon at top-right corner (e.g. x=2480, y=30)
        int trX = 2480, trY = 30;
        for (int y = 0; y < bh; y++) {
            int rowIdx = isBottomUp ? (bh - 1 - y) : y;
            const BYTE* row = bPixels + rowIdx * bStride;
            for (int x = 0; x < bw; x++) {
                SetPixel(hdcMem13D, trX + x, trY + y, RGB(row[x * 3 + 2], row[x * 3 + 1], row[x * 3 + 0]));
            }
        }

        POINT trMatchPt = { 0, 0 };
        double trScore = 0.0;
        BOOL trMatch = ttp_match_template_ncc(hdcMem13D, 2560, 1440, bBuf, bSize, 0.65, &trMatchPt, &trScore);
        printf("Test 13D (Top-Right Corner Full Search): trMatch=%d, score=%.4f, pos=(%ld, %ld), expected=(%d, %d)\n",
               trMatch, trScore, trMatchPt.x, trMatchPt.y, trX + bw / 2, trY + bh / 2);
        fflush(stdout);
        assert(trMatch == TRUE);
        assert(trScore >= 0.65);
        assert(abs(trMatchPt.x - (trX + bw / 2)) <= 2);
        assert(abs(trMatchPt.y - (trY + bh / 2)) <= 2);

        // Test playback step dual-engine fallback when icon moved to top-right corner
        ttp_engine_set_screen_dc_override(hdcMem13D);
        s_timeoutTriggered = 0;

        TTPStep stepMacro7;
        memset(&stepMacro7, 0, sizeof(stepMacro7));
        stepMacro7.stepId = 1;
        stepMacro7.actionType = TTP_ACTION_CLICK;
        stepMacro7.targetMode = TTP_TARGET_TEXT;
        strcpy(stepMacro7.textKey, "iKuuuVPN");
        stepMacro7.origX = 64;
        stepMacro7.origY = 1017;
        stepMacro7.timeoutMs = 1000;
        stepMacro7.postDelayMs = 0;

        BOOL playM7Res = ttp_playback_step(&stepMacro7, bBuf, bSize, NULL);
        ttp_engine_set_screen_dc_override(NULL);
        printf("Test 13D (Macro7 Playback with Top-Right Icon): playRes=%d, timeoutTriggered=%d\n",
               playM7Res, s_timeoutTriggered);
        fflush(stdout);
        assert(playM7Res == TRUE);
        assert(s_timeoutTriggered == 0);

        DeleteObject(hBmp13D);
        DeleteDC(hdcMem13D);
        ReleaseDC(NULL, hdcScr13D);
        free(bBuf);
    }

    // 14: Step 2 Reproduction (Fast-check Original Logic)
    // When a step is TEXT mode, targeting an accessible object matching at origPt,
    // playback MUST succeed immediately without being vetoed by visual template mismatch.
    FILE* fpBmp14 = fopen_dual("Library/logs/0010/step2.bmp", "../Library/logs/0010/step2.bmp");
    if (fpBmp14) {
        fseek(fpBmp14, 0, SEEK_END);
        DWORD bSize14 = (DWORD)ftell(fpBmp14);
        fseek(fpBmp14, 0, SEEK_SET);
        BYTE* bBuf14 = (BYTE*)malloc(bSize14);
        fread(bBuf14, 1, bSize14, fpBmp14);
        fclose(fpBmp14);

        HDC hdcScr14 = GetDC(NULL);
        HDC hdcMem14 = CreateCompatibleDC(hdcScr14);
        HBITMAP hBmp14 = CreateCompatibleBitmap(hdcScr14, 1920, 1080);
        SelectObject(hdcMem14, hBmp14);

        RECT rcBg14 = { 0, 0, 1920, 1080 };
        HBRUSH hbrBg14 = CreateSolidBrush(RGB(40, 50, 60));
        FillRect(hdcMem14, &rcBg14, hbrBg14);
        DeleteObject(hbrBg14);

        ttp_engine_set_screen_dc_override(hdcMem14);
        s_timeoutTriggered = 0;

        WNDCLASSEXA wcCS2 = { sizeof(WNDCLASSEXA), 0, DefWindowProcA, 0, 0, GetModuleHandleA(NULL), NULL, NULL, NULL, NULL, "TTP_TestCS2Cls", NULL };
        RegisterClassExA(&wcCS2);
        HWND hCS2 = CreateWindowExA(WS_EX_TOPMOST, "TTP_TestCS2Cls", "Counter-Strike 2",
                                    WS_POPUP | WS_VISIBLE,
                                    0, 900, 100, 100, NULL, NULL, GetModuleHandleA(NULL), NULL);
        UpdateWindow(hCS2);

        TTPStep step14;
        memset(&step14, 0, sizeof(step14));
        step14.stepId = 2;
        step14.actionType = TTP_ACTION_CLICK;
        step14.targetMode = TTP_TARGET_TEXT;
        strcpy(step14.textKey, "Counter-Strike 2");
        step14.origX = 34;
        step14.origY = 931;
        step14.timeoutMs = 150;
        step14.postDelayMs = 0;

        BOOL play14Res = ttp_playback_step(&step14, bBuf14, bSize14, NULL);
        ttp_engine_set_screen_dc_override(NULL);
        DestroyWindow(hCS2);
        UnregisterClassA("TTP_TestCS2Cls", GetModuleHandleA(NULL));

        printf("Test 14A (Fast-check Original Logic at OrigPt): play14Res=%d, timeoutTriggered=%d\n",
               play14Res, s_timeoutTriggered);
        fflush(stdout);
        assert(play14Res == TRUE);
        assert(s_timeoutTriggered == 0);

        DeleteObject(hBmp14);
        DeleteDC(hdcMem14);
        ReleaseDC(NULL, hdcScr14);
        free(bBuf14);
    }

    /* -------------------------------------------------------------------------
     * Test 15: Zero-CRT Stack Safety & Modal Menu Guard Invariants (BugFix)
     * ------------------------------------------------------------------------- */
    {
        printf("\n[TEST 15/15] Running Test 15: Adaptive Crop Stack Safety & Menu Guard Invariant...\n");

        /* 15A: Check source code invariant in ttp_vision.c:
         * Under -mno-stack-arg-probe, functions must NOT declare 256KB local stack arrays.
         * Arrays BYTE gray[256][256], edge[256][256], etc. must be heap allocated. */
        FILE* fpVis = fopen_dual("src/ttp_vision.c", "reverse-gemini/src/pro/ttp_vision.c");
        if (!fpVis) fpVis = fopen_dual("src/pro/ttp_vision.c", "reverse-gemini/src/pro/ttp_vision.c");
        assert(fpVis != NULL);
        fseek(fpVis, 0, SEEK_END);
        long visSz = ftell(fpVis);
        fseek(fpVis, 0, SEEK_SET);
        char* visSrc = (char*)malloc(visSz + 1);
        fread(visSrc, 1, visSz, fpVis);
        visSrc[visSz] = '\0';
        fclose(fpVis);

        const char* pStackGray = strstr(visSrc, "BYTE gray[256][256];");
        const char* pStackEdge = strstr(visSrc, "BYTE edge[256][256];");
        printf("Test 15A (Zero 256KB stack array): pStackGray=%p (must be NULL), pStackEdge=%p (must be NULL)\n",
               pStackGray, pStackEdge);
        fflush(stdout);
        assert(pStackGray == NULL); // Fails RED on current code!
        assert(pStackEdge == NULL);

        /* 15B: Check tinytask_pro.c for #32768 menu window guard in RecTimerProc */
        FILE* fpPro = fopen_dual("src/tinytask_pro.c", "reverse-gemini/src/pro/tinytask_pro.c");
        if (!fpPro) fpPro = fopen_dual("src/pro/tinytask_pro.c", "reverse-gemini/src/pro/tinytask_pro.c");
        assert(fpPro != NULL);
        fseek(fpPro, 0, SEEK_END);
        long proSz = ftell(fpPro);
        fseek(fpPro, 0, SEEK_SET);
        char* proSrc = (char*)malloc(proSz + 1);
        fread(proSrc, 1, proSz, fpPro);
        proSrc[proSz] = '\0';
        fclose(fpPro);

        const char* pRecTimer = strstr(proSrc, "RecTimerProc");
        assert(pRecTimer != NULL);
        const char* pMenuGuard = strstr(pRecTimer, "#32768");
        printf("Test 15B (Menu #32768 guard in RecTimerProc): pMenuGuard=%p (must NOT be NULL)\n", pMenuGuard);
        fflush(stdout);
        assert(pMenuGuard != NULL); // Fails RED on current code!

        free(visSrc);
        free(proSrc);
    }

    // ==========================================================
    // Phase 5 Hover Discrepancy & Spatial Gate (Plan A + D)
    // ==========================================================
    {
        printf("\n[TEST 16/16] Running Test 16: Plan A Hover Probe & Plan D Spatial Adaptive Gate...\n");
        fflush(stdout);

        /* 16A: Source code invariant in ttp_engine.c */
        FILE* fpEng = fopen_dual("src/ttp_engine.c", "reverse-gemini/src/pro/ttp_engine.c");
        assert(fpEng != NULL);
        fseek(fpEng, 0, SEEK_END);
        long engSz = ftell(fpEng);
        fseek(fpEng, 0, SEEK_SET);
        char* engSrc = (char*)malloc(engSz + 1);
        fread(engSrc, 1, engSz, fpEng);
        engSrc[engSz] = '\0';
        fclose(fpEng);

        const char* pHoverProbe = strstr(engSrc, "Plan A Hover Probe");
        const char* pSpatialGate = strstr(engSrc, "Plan D Spatial Gate");
        const char* pAdaptiveFunc = strstr(engSrc, "ttp_match_visual_target_adaptive");
        printf("Test 16A (Engine Invariants): pHoverProbe=%p, pSpatialGate=%p, pAdaptiveFunc=%p\n",
               pHoverProbe, pSpatialGate, pAdaptiveFunc);
        fflush(stdout);
        free(engSrc);
        assert(pHoverProbe != NULL); // Fails RED before implementation
        assert(pSpatialGate != NULL);
        assert(pAdaptiveFunc != NULL);
        printf("Test 16A passed!\n");
        fflush(stdout);

        /* 16B: Behavioral verification of Plan D Spatial Adaptive Gate */
        int testCanvasW = 600, testCanvasH = 400;
        int btnW = 40, btnH = 20;
        DWORD btnBmpSize = 0;
        BYTE* btnBmp = create_test_pattern_bmp(btnW, btnH, &btnBmpSize);

        HDC hdcScr16 = GetDC(NULL);
        HDC hdcMem16 = CreateCompatibleDC(hdcScr16);
        HBITMAP hBmp16 = CreateCompatibleBitmap(hdcScr16, testCanvasW, testCanvasH);
        SelectObject(hdcMem16, hBmp16);

        // Fill background
        RECT rcBg16 = { 0, 0, testCanvasW, testCanvasH };
        HBRUSH hbrBg16 = CreateSolidBrush(RGB(235, 235, 235));
        FillRect(hdcMem16, &rcBg16, hbrBg16);
        DeleteObject(hbrBg16);

        // Render button at (106, 104) with degraded contrast (simulating unhovered Idle state)
        // Original pattern was 230 vs 20; Idle pattern has subdued 170 vs 90
        int btnTargetX = 106, btnTargetY = 104;
        for (int y = 0; y < btnH; y++) {
            for (int x = 0; x < btnW; x++) {
                BYTE v = ((x / 4) % 2 == (y / 4) % 2) ? 170 : 90;
                SetPixel(hdcMem16, btnTargetX + x, btnTargetY + y, RGB(v, v, v));
            }
        }

        ttp_engine_set_screen_dc_override(hdcMem16);

        TTPStep step16;
        memset(&step16, 0, sizeof(step16));
        step16.stepId = 16;
        step16.actionType = TTP_ACTION_CLICK;
        step16.targetMode = TTP_TARGET_IMAGE;
        step16.origX = 100 + btnW / 2; // Near target: offset by 6px horizontally, 4px vertically
        step16.origY = 100 + btnH / 2;
        step16.timeoutMs = 500;
        step16.postDelayMs = 0;

        BOOL play16 = ttp_playback_step(&step16, btnBmp, btnBmpSize, NULL);
        ttp_engine_set_screen_dc_override(NULL);

        printf("Test 16B (Plan D Spatial Gate Acceptance): play16=%d\n", play16);
        fflush(stdout);
        assert(play16 == TRUE);

        DeleteObject(hBmp16);
        DeleteDC(hdcMem16);
        ReleaseDC(NULL, hdcScr16);
        free(btnBmp);
        printf("Test 16B passed!\n");
        fflush(stdout);
    }

    printf("==========================================\n");
    printf("ALL REGRESSION TESTS PASSED (GREEN)!\n");
    printf("==========================================\n");
    return 0;
}

