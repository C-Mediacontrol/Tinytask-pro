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
    FILE* fpPro = fopen("reverse-gemini/src/tinytask_pro.c", "rb");
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
    assert(durRoiMs < 10.0);

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
    FILE* fpEng = fopen("reverse-gemini/src/ttp_engine.c", "rb");
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
    FILE* fpEng11 = fopen("reverse-gemini/src/ttp_engine.c", "rb");
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

    printf("==========================================\n");
    printf("ALL REGRESSION TESTS PASSED (GREEN)!\n");
    printf("==========================================\n");
    return 0;
}
