#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <windows.h>
#include <commctrl.h>

#include "ttp_core.h"
#include "ttp_storage.h"
#include "ttp_vision.h"
#include "ttp_engine.h"

/* Define TTP_TEST_MODE to include tinytask_pro without its WinMain */
#define TTP_TEST_MODE 1
#include "../src/tinytask_pro.c"

/* -------------------------------------------------------------------------
 * Test 1: Step array lifecycle (Add, MoveUp, MoveDown, Delete, Renumber)
 * ------------------------------------------------------------------------- */
static void test_step_array_lifecycle(void) {
    printf("[TEST 1/6] Running test_step_array_lifecycle...\n");
    StepArray_Clear();
    assert(g_stepCount == 0);

    TTPStep s1;
    memset(&s1, 0, sizeof(s1));
    s1.actionType = TTP_ACTION_CLICK;
    s1.targetMode = TTP_TARGET_COORD;
    s1.origX = 100;
    s1.origY = 200;
    s1.timeoutMs = 3000;
    s1.postDelayMs = 100;
    StepArray_Add(&s1, NULL, 0);

    TTPStep s2;
    memset(&s2, 0, sizeof(s2));
    s2.actionType = TTP_ACTION_TYPE_TEXT;
    s2.targetMode = TTP_TARGET_TEXT;
    strcpy(s2.textKey, "Hello TinyTask");
    s2.timeoutMs = 5000;
    s2.postDelayMs = 150;
    BYTE mockBmp[54] = { 0x42, 0x4D }; /* "BM" */
    StepArray_Add(&s2, mockBmp, sizeof(mockBmp));

    TTPStep s3;
    memset(&s3, 0, sizeof(s3));
    s3.actionType = TTP_ACTION_RCLICK;
    s3.targetMode = TTP_TARGET_COORD;
    s3.origX = 300;
    s3.origY = 400;
    s3.timeoutMs = 2000;
    s3.postDelayMs = 50;
    StepArray_Add(&s3, NULL, 0);

    assert(g_stepCount == 3);
    assert(g_steps[0].stepId == 1 && g_steps[0].actionType == TTP_ACTION_CLICK);
    assert(g_steps[1].stepId == 2 && g_steps[1].actionType == TTP_ACTION_TYPE_TEXT);
    assert(g_bmpBuffers[1] != NULL && g_bmpSizes[1] == 54);
    assert(g_steps[2].stepId == 3 && g_steps[2].actionType == TTP_ACTION_RCLICK);

    /* Move step 1 up to index 0 */
    BOOL movedUp = StepArray_MoveUp(1);
    assert(movedUp == TRUE);
    assert(g_steps[0].stepId == 1 && g_steps[0].actionType == TTP_ACTION_TYPE_TEXT);
    assert(g_bmpBuffers[0] != NULL && g_bmpSizes[0] == 54);
    assert(g_steps[1].stepId == 2 && g_steps[1].actionType == TTP_ACTION_CLICK);

    /* Move step 0 down to index 1 */
    BOOL movedDown = StepArray_MoveDown(0);
    assert(movedDown == TRUE);
    assert(g_steps[0].stepId == 1 && g_steps[0].actionType == TTP_ACTION_CLICK);
    assert(g_steps[1].stepId == 2 && g_steps[1].actionType == TTP_ACTION_TYPE_TEXT);

    /* Delete step 0 */
    BOOL deleted = StepArray_Delete(0);
    assert(deleted == TRUE);
    assert(g_stepCount == 2);
    assert(g_steps[0].stepId == 1 && g_steps[0].actionType == TTP_ACTION_TYPE_TEXT);
    assert(g_steps[1].stepId == 2 && g_steps[1].actionType == TTP_ACTION_RCLICK);

    StepArray_Clear();
    assert(g_stepCount == 0);
    printf("      => PASSED (Step array Add, Move, Delete, Renumber verified)\n");
}

/* -------------------------------------------------------------------------
 * Test 2: Timeout parsing & formatting
 * ------------------------------------------------------------------------- */
static void test_timeout_formatting(void) {
    printf("[TEST 2/6] Running test_timeout_formatting...\n");

    DWORD ms1 = 0; int act1 = 0;
    ParseTimeoutString("5.0", &ms1, &act1);
    assert(ms1 == 5000);

    DWORD ms2 = 0; int act2 = 0;
    ParseTimeoutString("0.5s", &ms2, &act2);
    assert(ms2 == 500);

    DWORD ms3 = 0; int act3 = 0;
    ParseTimeoutString("10", &ms3, &act3);
    assert(ms3 == 10000);

    DWORD ms4 = 0; int act4 = 0;
    ParseTimeoutString("0.05", &ms4, &act4); /* Clamped to 100ms min */
    assert(ms4 >= 100);

    TTPStep step1;
    memset(&step1, 0, sizeof(step1));
    step1.timeoutMs = 3000;
    step1.targetMode = TTP_MAKE_TARGET_MODE(TTP_TARGET_IMAGE, TTP_TIMEOUT_ACT_DEFAULT);
    char outStr[64];
    FormatTimeoutString(&step1, outStr, sizeof(outStr));
    assert(strstr(outStr, "3.0s") != NULL);

    TTPStep step2;
    memset(&step2, 0, sizeof(step2));
    step2.timeoutMs = 750;
    step2.targetMode = TTP_MAKE_TARGET_MODE(TTP_TARGET_IMAGE, TTP_TIMEOUT_ACT_RETRY);
    FormatTimeoutString(&step2, outStr, sizeof(outStr));
    assert(strstr(outStr, "0.8s") != NULL || strstr(outStr, "0.7s") != NULL);
    assert(strstr(outStr, "[Retry]") != NULL);

    /* FormatTimeoutSecondsString assertions: pure numeric seconds with 's' */
    char secBuf[32];
    FormatTimeoutSecondsString(&step1, secBuf, sizeof(secBuf));
    assert(strcmp(secBuf, "3.0s") == 0);

    step1.timeoutMs = 500;
    FormatTimeoutSecondsString(&step1, secBuf, sizeof(secBuf));
    assert(strcmp(secBuf, "0.5s") == 0);

    step1.timeoutMs = 1200;
    FormatTimeoutSecondsString(&step1, secBuf, sizeof(secBuf));
    assert(strcmp(secBuf, "1.2s") == 0);

    /* FormatTimeoutPolicyString assertions: [Prompt], [Retry], [Coord], [Skip], [Stop] */
    char polBuf[32];
    TTPStep stepPol;
    memset(&stepPol, 0, sizeof(stepPol));

    stepPol.targetMode = TTP_MAKE_TARGET_MODE(TTP_TARGET_IMAGE, TTP_TIMEOUT_ACT_DEFAULT);
    FormatTimeoutPolicyString(&stepPol, polBuf, sizeof(polBuf));
    assert(strcmp(polBuf, "[Prompt]") == 0);

    stepPol.targetMode = TTP_MAKE_TARGET_MODE(TTP_TARGET_IMAGE, TTP_TIMEOUT_ACT_RETRY);
    FormatTimeoutPolicyString(&stepPol, polBuf, sizeof(polBuf));
    assert(strcmp(polBuf, "[Retry]") == 0);

    stepPol.targetMode = TTP_MAKE_TARGET_MODE(TTP_TARGET_IMAGE, TTP_TIMEOUT_ACT_USE_RECORDED);
    FormatTimeoutPolicyString(&stepPol, polBuf, sizeof(polBuf));
    assert(strcmp(polBuf, "[Coord]") == 0);

    stepPol.targetMode = TTP_MAKE_TARGET_MODE(TTP_TARGET_IMAGE, TTP_TIMEOUT_ACT_SKIP);
    FormatTimeoutPolicyString(&stepPol, polBuf, sizeof(polBuf));
    assert(strcmp(polBuf, "[Skip]") == 0);

    stepPol.targetMode = TTP_MAKE_TARGET_MODE(TTP_TARGET_IMAGE, TTP_TIMEOUT_ACT_STOP);
    FormatTimeoutPolicyString(&stepPol, polBuf, sizeof(polBuf));
    assert(strcmp(polBuf, "[Stop]") == 0);

    printf("      => PASSED (Timeout parse and format verified)\n");
}

/* -------------------------------------------------------------------------
 * Test 3: Action & Target column text representation & 6-column Drawer layout
 * ------------------------------------------------------------------------- */
static void test_column_formatting(void) {
    printf("[TEST 3/6] Running test_column_formatting...\n");

    assert(strcmp(GetActionName(TTP_ACTION_CLICK), "Click") == 0);
    assert(strcmp(GetActionName(TTP_ACTION_DBLCLICK), "DblClick") == 0);
    assert(strcmp(GetActionName(TTP_ACTION_RCLICK), "RClick") == 0);
    assert(strcmp(GetActionName(TTP_ACTION_DRAG), "Drag") == 0);
    assert(strcmp(GetActionName(TTP_ACTION_TYPE_TEXT), "Type") == 0);
    assert(strcmp(GetActionName(TTP_ACTION_HOTKEY), "Hotkey") == 0);

    TTPStep sText;
    memset(&sText, 0, sizeof(sText));
    sText.actionType = TTP_ACTION_CLICK;
    sText.targetMode = TTP_TARGET_TEXT;
    strcpy(sText.textKey, "Submit");
    char desc[128];
    GetTargetDescription(&sText, desc, sizeof(desc));
    assert(strstr(desc, "Submit") != NULL);

    TTPStep sImg;
    memset(&sImg, 0, sizeof(sImg));
    sImg.actionType = TTP_ACTION_CLICK;
    sImg.targetMode = TTP_TARGET_IMAGE;
    GetTargetDescription(&sImg, desc, sizeof(desc));
    assert(strstr(desc, "Image") != NULL);

    TTPStep sCoord;
    memset(&sCoord, 0, sizeof(sCoord));
    sCoord.actionType = TTP_ACTION_CLICK;
    sCoord.targetMode = TTP_TARGET_COORD;
    sCoord.origX = 120;
    sCoord.origY = 340;
    GetTargetDescription(&sCoord, desc, sizeof(desc));
    assert(strstr(desc, "120") != NULL && strstr(desc, "340") != NULL);

    /* Verify 6 drawer columns with headers "#", "Action", "Target", "Timeout", "On Timeout", "Asset" */
    assert(DRAWER_COLUMN_COUNT == 6);
    assert(strcmp(g_drawerColumns[0].header, "#") == 0 && g_drawerColumns[0].width == 28);
    assert(strcmp(g_drawerColumns[1].header, "Action") == 0 && g_drawerColumns[1].width == 52);
    assert(strcmp(g_drawerColumns[2].header, "Target") == 0 && g_drawerColumns[2].width == 95);
    assert(strcmp(g_drawerColumns[3].header, "Timeout") == 0 && g_drawerColumns[3].width == 50);
    assert(strcmp(g_drawerColumns[4].header, "On Timeout") == 0 && g_drawerColumns[4].width == 75);
    assert(strcmp(g_drawerColumns[5].header, "Asset") == 0 && g_drawerColumns[5].width == 40);

    printf("      => PASSED (Action and target column strings verified)\n");
}

/* -------------------------------------------------------------------------
 * Test 4: Dynamic Titlebar string generation
 * ------------------------------------------------------------------------- */
static void test_titlebar_strings(void) {
    printf("[TEST 4/6] Running test_titlebar_strings...\n");

    char title[128];
    FormatTitleRec(3, 4, title, sizeof(title));
    assert(strcmp(title, "REC 00:03 (4 steps)") == 0);

    FormatTitlePlay(1, 2, 4, title, sizeof(title));
    assert(strcmp(title, "PLAY 00:01 (Step 2/4)") == 0);

    FormatTitleIdle(NULL, title, sizeof(title));
    assert(strcmp(title, "TinyTask Pro") == 0);

    FormatTitleIdle("my_task.ttp", title, sizeof(title));
    assert(strcmp(title, "TinyTask Pro - my_task.ttp") == 0);

    printf("      => PASSED (Titlebar dynamic strings verified)\n");
}

/* -------------------------------------------------------------------------
 * Test 5: Storage .ttp save & load roundtrip
 * ------------------------------------------------------------------------- */
static void test_project_storage_roundtrip(void) {
    printf("[TEST 5/6] Running test_project_storage_roundtrip...\n");
    const char* testFile = "test_pro_temp.ttp";

    StepArray_Clear();
    TTPStep s1;
    memset(&s1, 0, sizeof(s1));
    s1.stepId = 1;
    s1.actionType = TTP_ACTION_CLICK;
    s1.targetMode = TTP_TARGET_TEXT;
    strcpy(s1.textKey, "ButtonOK");
    s1.origX = 150;
    s1.origY = 250;
    s1.timeoutMs = 4000;
    s1.postDelayMs = 200;
    BYTE bmp1[64];
    memset(bmp1, 0xAA, sizeof(bmp1));
    StepArray_Add(&s1, bmp1, sizeof(bmp1));

    TTPStep s2;
    memset(&s2, 0, sizeof(s2));
    s2.stepId = 2;
    s2.actionType = TTP_ACTION_DRAG;
    s2.targetMode = TTP_TARGET_COORD;
    s2.origX = 50;
    s2.origY = 50;
    s2.destX = 150;
    s2.destY = 150;
    s2.timeoutMs = 3000;
    s2.postDelayMs = 100;
    StepArray_Add(&s2, NULL, 0);

    BOOL saved = SaveProjectFile(testFile);
    assert(saved == TRUE);

    StepArray_Clear();
    assert(g_stepCount == 0);

    BOOL loaded = LoadProjectFile(testFile);
    assert(loaded == TRUE);
    assert(g_stepCount == 2);
    assert(g_steps[0].actionType == TTP_ACTION_CLICK);
    assert(strcmp(g_steps[0].textKey, "ButtonOK") == 0);
    assert(g_bmpBuffers[0] != NULL && g_bmpSizes[0] == 64);
    assert(g_steps[1].actionType == TTP_ACTION_DRAG);
    assert(g_steps[1].destX == 150);

    DeleteFileA(testFile);
    StepArray_Clear();
    printf("      => PASSED (Project save & load roundtrip verified)\n");
}

/* -------------------------------------------------------------------------
 * Test 6: Single step playback execution via ttp_playback_step
 * ------------------------------------------------------------------------- */
static void test_step_playback_execution(void) {
    printf("[TEST 6/6] Running test_step_playback_execution...\n");

    TTPStep s;
    memset(&s, 0, sizeof(s));
    s.stepId = 1;
    s.actionType = TTP_ACTION_CLICK;
    s.targetMode = TTP_TARGET_COORD;
    s.origX = 500;
    s.origY = 500;
    s.timeoutMs = 1000;
    s.postDelayMs = 50;

    /* Execute without parent modal */
    BOOL ok = ttp_playback_step(&s, NULL, 0, NULL);
    assert(ok == TRUE);

    printf("      => PASSED (Single step execution verified)\n");
}

int main(void) {
    printf("====================================================\n");
    printf("        TinyTask Pro Unit & Integration Tests       \n");
    printf("====================================================\n");

    test_step_array_lifecycle();
    test_timeout_formatting();
    test_column_formatting();
    test_titlebar_strings();
    test_project_storage_roundtrip();
    test_step_playback_execution();

    printf("====================================================\n");
    printf("  ALL 6 TINYTASK PRO TEST SUITES PASSED!            \n");
    printf("====================================================\n");
    return 0;
}
