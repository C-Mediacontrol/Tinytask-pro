#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <windows.h>
#include "ttp_core.h"
#include "ttp_vision.h"
#include "ttp_engine.h"

/* -------------------------------------------------------------------------
 * Test 1: Mouse down -> move 2px -> mouse up => Produces 1 TTP_ACTION_CLICK
 * (Idle moves without mouse buttons held are suppressed)
 * ------------------------------------------------------------------------- */
static void test_synth_click_and_idle_moves(void) {
    printf("[TEST 1/8] Running test_synth_click_and_idle_moves...\n");
    ttp_synth_reset();

    // Idle mouse movements (no buttons held) - should be suppressed
    ttp_synth_add_mouse_event(WM_MOUSEMOVE, 10, 10, 100);
    ttp_synth_add_mouse_event(WM_MOUSEMOVE, 20, 20, 150);
    ttp_synth_add_mouse_event(WM_MOUSEMOVE, 30, 30, 200);

    // Left down at (100, 100)
    ttp_synth_add_mouse_event(WM_LBUTTONDOWN, 100, 100, 300);

    // Small jitter move: 2px (below drag threshold of 6px)
    ttp_synth_add_mouse_event(WM_MOUSEMOVE, 102, 100, 350);

    // Left up at (102, 100)
    ttp_synth_add_mouse_event(WM_LBUTTONUP, 102, 100, 400);

    // More idle mouse moves
    ttp_synth_add_mouse_event(WM_MOUSEMOVE, 300, 300, 500);

    TTPStep steps[8];
    memset(steps, 0, sizeof(steps));
    DWORD count = ttp_synth_finalize(steps, 8);

    assert(count == 1);
    assert(steps[0].stepId == 1);
    assert(steps[0].actionType == TTP_ACTION_CLICK);
    assert(steps[0].origX == 100);
    assert(steps[0].origY == 100);
    assert(steps[0].timeoutMs == 3000);
    assert(steps[0].postDelayMs == 100);
    assert(steps[0].targetMode == TTP_TARGET_COORD);

    printf("      => PASSED (1 CLICK generated, idle moves ignored)\n");
}

/* -------------------------------------------------------------------------
 * Test 2: Mouse down -> move 100px -> mouse up => Produces 1 TTP_ACTION_DRAG
 * ------------------------------------------------------------------------- */
static void test_synth_drag(void) {
    printf("[TEST 2/8] Running test_synth_drag...\n");
    ttp_synth_reset();

    // Left down at (50, 50)
    ttp_synth_add_mouse_event(WM_LBUTTONDOWN, 50, 50, 1000);

    // Move 100px to (150, 50)
    ttp_synth_add_mouse_event(WM_MOUSEMOVE, 100, 50, 1100);
    ttp_synth_add_mouse_event(WM_MOUSEMOVE, 150, 50, 1200);

    // Left up at (150, 50)
    ttp_synth_add_mouse_event(WM_LBUTTONUP, 150, 50, 1300);

    TTPStep steps[8];
    memset(steps, 0, sizeof(steps));
    DWORD count = ttp_synth_finalize(steps, 8);

    assert(count == 1);
    assert(steps[0].stepId == 1);
    assert(steps[0].actionType == TTP_ACTION_DRAG);
    assert(steps[0].origX == 50);
    assert(steps[0].origY == 50);
    assert(steps[0].destX == 150);
    assert(steps[0].destY == 50);
    assert(steps[0].timeoutMs == 3000);
    assert(steps[0].postDelayMs == 100);

    printf("      => PASSED (1 DRAG generated with origin (50,50) -> dest (150,50))\n");
}

/* -------------------------------------------------------------------------
 * Test 3: Typing 'H', 'e', 'l', 'l', 'o' => Produces 1 TTP_ACTION_TYPE_TEXT "Hello"
 * ------------------------------------------------------------------------- */
static void test_synth_type_text(void) {
    printf("[TEST 3/8] Running test_synth_type_text...\n");
    ttp_synth_reset();

    ttp_synth_add_key_event('H', TRUE, 2000);
    ttp_synth_add_key_event('H', FALSE, 2020);
    ttp_synth_add_key_event('e', TRUE, 2050);
    ttp_synth_add_key_event('e', FALSE, 2070);
    ttp_synth_add_key_event('l', TRUE, 2100);
    ttp_synth_add_key_event('l', FALSE, 2120);
    ttp_synth_add_key_event('l', TRUE, 2150);
    ttp_synth_add_key_event('l', FALSE, 2170);
    ttp_synth_add_key_event('o', TRUE, 2200);
    ttp_synth_add_key_event('o', FALSE, 2220);

    TTPStep steps[8];
    memset(steps, 0, sizeof(steps));
    DWORD count = ttp_synth_finalize(steps, 8);

    assert(count == 1);
    assert(steps[0].stepId == 1);
    assert(steps[0].actionType == TTP_ACTION_TYPE_TEXT);
    assert(strcmp(steps[0].textKey, "Hello") == 0);
    assert(steps[0].timeoutMs == 3000);
    assert(steps[0].postDelayMs == 100);

    printf("      => PASSED (1 TYPE_TEXT generated with textKey='Hello')\n");
}

/* -------------------------------------------------------------------------
 * Test 4: Two clicks close in time and position => Produces 1 TTP_ACTION_DBLCLICK
 * ------------------------------------------------------------------------- */
static void test_synth_double_click(void) {
    printf("[TEST 4/8] Running test_synth_double_click...\n");
    ttp_synth_reset();

    // First click at (200, 200)
    ttp_synth_add_mouse_event(WM_LBUTTONDOWN, 200, 200, 3000);
    ttp_synth_add_mouse_event(WM_LBUTTONUP, 200, 200, 3050);

    // Second click at (201, 200) within 150ms
    ttp_synth_add_mouse_event(WM_LBUTTONDOWN, 201, 200, 3200);
    ttp_synth_add_mouse_event(WM_LBUTTONUP, 201, 200, 3250);

    TTPStep steps[8];
    memset(steps, 0, sizeof(steps));
    DWORD count = ttp_synth_finalize(steps, 8);

    assert(count == 1);
    assert(steps[0].stepId == 1);
    assert(steps[0].actionType == TTP_ACTION_DBLCLICK);
    assert(steps[0].origX == 200);
    assert(steps[0].origY == 200);

    printf("      => PASSED (1 DBLCLICK generated from rapid consecutive clicks)\n");
}

/* -------------------------------------------------------------------------
 * Test 5: Right click and Hotkey handling
 * ------------------------------------------------------------------------- */
static void test_synth_rclick_and_hotkey(void) {
    printf("[TEST 5/8] Running test_synth_rclick_and_hotkey...\n");
    ttp_synth_reset();

    // Right click at (350, 450)
    ttp_synth_add_mouse_event(WM_RBUTTONDOWN, 350, 450, 4000);
    ttp_synth_add_mouse_event(WM_RBUTTONUP, 350, 450, 4060);

    // Type "Hi"
    ttp_synth_add_key_event('H', TRUE, 4100);
    ttp_synth_add_key_event('i', TRUE, 4150);

    // Press Enter (non-printable hotkey)
    ttp_synth_add_key_event(VK_RETURN, TRUE, 4200);
    ttp_synth_add_key_event(VK_RETURN, FALSE, 4250);

    TTPStep steps[8];
    memset(steps, 0, sizeof(steps));
    DWORD count = ttp_synth_finalize(steps, 8);

    assert(count == 3);
    assert(steps[0].actionType == TTP_ACTION_RCLICK);
    assert(steps[0].origX == 350 && steps[0].origY == 450);

    assert(steps[1].actionType == TTP_ACTION_TYPE_TEXT);
    assert(strcmp(steps[1].textKey, "Hi") == 0);

    assert(steps[2].actionType == TTP_ACTION_HOTKEY);
    assert(steps[2].origX == VK_RETURN);

    printf("      => PASSED (RCLICK + TYPE_TEXT + HOTKEY sequenced correctly)\n");
}

/* -------------------------------------------------------------------------
 * Test 6: Timeout callback mechanism with TTP_TIMEOUT_USE_RECORDED
 * ------------------------------------------------------------------------- */
static int s_recordedCbCount = 0;
static const TTPStep* s_lastCallbackStep = NULL;

static int callback_use_recorded(const TTPStep* step, void* userData) {
    s_recordedCbCount++;
    s_lastCallbackStep = step;
    int* pFlag = (int*)userData;
    if (pFlag) *pFlag = 42;
    return TTP_TIMEOUT_USE_RECORDED;
}

static void test_playback_timeout_use_recorded(void) {
    printf("[TEST 6/8] Running test_playback_timeout_use_recorded...\n");

    s_recordedCbCount = 0;
    s_lastCallbackStep = NULL;
    int userFlag = 0;
    ttp_engine_set_timeout_callback(callback_use_recorded, &userFlag);

    TTPStep step;
    memset(&step, 0, sizeof(step));
    step.stepId = 10;
    step.actionType = TTP_ACTION_CLICK;
    step.targetMode = TTP_TARGET_TEXT;
    strcpy(step.textKey, "NonExistentButtonXYZ_9999");
    step.origX = 120;
    step.origY = 140;
    step.timeoutMs = 150; // fast timeout
    step.postDelayMs = 0;

    DWORD start = GetTickCount();
    BOOL res = ttp_playback_step(&step, NULL, 0, NULL);
    DWORD elapsed = GetTickCount() - start;

    assert(res == TRUE);
    assert(s_recordedCbCount == 1);
    assert(s_lastCallbackStep == &step);
    assert(userFlag == 42);
    assert(elapsed >= 100);

    printf("      => PASSED (Timeout callback triggered, returned TTP_TIMEOUT_USE_RECORDED, resumed in %lu ms)\n", (unsigned long)elapsed);
}

/* -------------------------------------------------------------------------
 * Test 7: Timeout callback mechanism with TTP_TIMEOUT_SKIP & TTP_TIMEOUT_STOP
 * ------------------------------------------------------------------------- */
static int s_skipCbCount = 0;
static int callback_skip(const TTPStep* step, void* userData) {
    (void)step; (void)userData;
    s_skipCbCount++;
    return TTP_TIMEOUT_SKIP;
}

static int s_stopCbCount = 0;
static int callback_stop(const TTPStep* step, void* userData) {
    (void)step; (void)userData;
    s_stopCbCount++;
    return TTP_TIMEOUT_STOP;
}

static void test_playback_timeout_skip_and_stop(void) {
    printf("[TEST 7/8] Running test_playback_timeout_skip_and_stop...\n");

    // Test TTP_TIMEOUT_SKIP
    s_skipCbCount = 0;
    ttp_engine_set_timeout_callback(callback_skip, NULL);

    TTPStep stepSkip;
    memset(&stepSkip, 0, sizeof(stepSkip));
    stepSkip.stepId = 11;
    stepSkip.actionType = TTP_ACTION_CLICK;
    stepSkip.targetMode = TTP_TARGET_TEXT;
    strcpy(stepSkip.textKey, "AnotherNonExistentControl");
    stepSkip.timeoutMs = 100;
    stepSkip.postDelayMs = 0;

    BOOL resSkip = ttp_playback_step(&stepSkip, NULL, 0, NULL);
    assert(resSkip == TRUE);
    assert(s_skipCbCount == 1);

    // Test TTP_TIMEOUT_STOP
    s_stopCbCount = 0;
    ttp_engine_set_timeout_callback(callback_stop, NULL);

    TTPStep stepStop;
    memset(&stepStop, 0, sizeof(stepStop));
    stepStop.stepId = 12;
    stepStop.actionType = TTP_ACTION_CLICK;
    stepStop.targetMode = TTP_TARGET_TEXT;
    strcpy(stepStop.textKey, "AnotherNonExistentControl2");
    stepStop.timeoutMs = 100;
    stepStop.postDelayMs = 0;

    BOOL resStop = ttp_playback_step(&stepStop, NULL, 0, NULL);
    assert(resStop == FALSE);
    assert(s_stopCbCount == 1);

    printf("      => PASSED (SKIP returned TRUE, STOP returned FALSE)\n");
}

/* -------------------------------------------------------------------------
 * Test 8: Playback step with direct coordinate mode
 * ------------------------------------------------------------------------- */
static void test_playback_coord_mode(void) {
    printf("[TEST 8/8] Running test_playback_coord_mode...\n");

    // Clear callback to ensure default path doesn't fail
    ttp_engine_set_timeout_callback(NULL, NULL);

    TTPStep step;
    memset(&step, 0, sizeof(step));
    step.stepId = 1;
    step.actionType = TTP_ACTION_CLICK;
    step.targetMode = TTP_TARGET_COORD;
    step.origX = 200;
    step.origY = 200;
    step.timeoutMs = 1000;
    step.postDelayMs = 10;

    BOOL res = ttp_playback_step(&step, NULL, 0, NULL);
    assert(res == TRUE);

    printf("      => PASSED (TTP_TARGET_COORD executed directly without polling)\n");
}

int main(void) {
    printf("==========================================\n");
    printf("Starting TinyTask Pro Engine Unit Tests\n");
    printf("==========================================\n");

    ttp_synth_init();

    test_synth_click_and_idle_moves();
    test_synth_drag();
    test_synth_type_text();
    test_synth_double_click();
    test_synth_rclick_and_hotkey();
    test_playback_timeout_use_recorded();
    test_playback_timeout_skip_and_stop();
    test_playback_coord_mode();

    printf("==========================================\n");
    printf("ALL ENGINE TESTS PASSED SUCCESSFULLY!\n");
    printf("==========================================\n");
    return 0;
}
