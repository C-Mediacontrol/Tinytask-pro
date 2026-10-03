#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <windows.h>
#include "ttp_core.h"
#include "ttp_storage.h"

static void test_roundtrip_with_images(void) {
    printf("[TEST] Running test_roundtrip_with_images...\n");

    const char* testFilePath = "test_roundtrip.ttp";

    // Prepare 2 test steps
    TTPStep steps[2];
    memset(steps, 0, sizeof(steps));

    // Step 0: Image click
    steps[0].stepId = 1;
    steps[0].actionType = TTP_ACTION_CLICK;
    steps[0].targetMode = TTP_TARGET_IMAGE;
    steps[0].origX = 100;
    steps[0].origY = 200;
    steps[0].destX = 100;
    steps[0].destY = 200;
    steps[0].timeoutMs = 5000;
    steps[0].postDelayMs = 250;
    strncpy(steps[0].textKey, "button_ok", sizeof(steps[0].textKey) - 1);

    BYTE dummyBmp0[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                          0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};

    // Step 1: Text type
    steps[1].stepId = 2;
    steps[1].actionType = TTP_ACTION_TYPE_TEXT;
    steps[1].targetMode = TTP_TARGET_TEXT;
    steps[1].origX = 350;
    steps[1].origY = 400;
    steps[1].destX = 350;
    steps[1].destY = 400;
    steps[1].timeoutMs = 3000;
    steps[1].postDelayMs = 100;
    strncpy(steps[1].textKey, "Hello World! Test serialization", sizeof(steps[1].textKey) - 1);

    BYTE dummyBmp1[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22};

    const BYTE* bmpBuffers[2] = { dummyBmp0, dummyBmp1 };
    DWORD bmpSizes[2] = { sizeof(dummyBmp0), sizeof(dummyBmp1) };

    // 1. Save project
    BOOL saveOk = ttp_save_project(testFilePath, steps, 2, bmpBuffers, bmpSizes);
    assert(saveOk == TRUE);

    // 2. Load project back
    TTPStep* loadedSteps = NULL;
    DWORD loadedCount = 0;
    BYTE** loadedBmps = NULL;
    DWORD* loadedSizes = NULL;

    BOOL loadOk = ttp_load_project(testFilePath, &loadedSteps, &loadedCount, &loadedBmps, &loadedSizes);
    assert(loadOk == TRUE);
    assert(loadedCount == 2);
    assert(loadedSteps != NULL);
    assert(loadedBmps != NULL);
    assert(loadedSizes != NULL);

    // Verify Step 0
    assert(loadedSteps[0].stepId == 1);
    assert(loadedSteps[0].actionType == TTP_ACTION_CLICK);
    assert(loadedSteps[0].targetMode == TTP_TARGET_IMAGE);
    assert(loadedSteps[0].origX == 100);
    assert(loadedSteps[0].origY == 200);
    assert(loadedSteps[0].destX == 100);
    assert(loadedSteps[0].destY == 200);
    assert(loadedSteps[0].timeoutMs == 5000);
    assert(loadedSteps[0].postDelayMs == 250);
    assert(strcmp(loadedSteps[0].textKey, "button_ok") == 0);
    assert(loadedSizes[0] == sizeof(dummyBmp0));
    assert(loadedBmps[0] != NULL);
    assert(memcmp(loadedBmps[0], dummyBmp0, sizeof(dummyBmp0)) == 0);

    // Verify Step 1
    assert(loadedSteps[1].stepId == 2);
    assert(loadedSteps[1].actionType == TTP_ACTION_TYPE_TEXT);
    assert(loadedSteps[1].targetMode == TTP_TARGET_TEXT);
    assert(loadedSteps[1].origX == 350);
    assert(loadedSteps[1].origY == 400);
    assert(loadedSteps[1].destX == 350);
    assert(loadedSteps[1].destY == 400);
    assert(loadedSteps[1].timeoutMs == 3000);
    assert(loadedSteps[1].postDelayMs == 100);
    assert(strcmp(loadedSteps[1].textKey, "Hello World! Test serialization") == 0);
    assert(loadedSizes[1] == sizeof(dummyBmp1));
    assert(loadedBmps[1] != NULL);
    assert(memcmp(loadedBmps[1], dummyBmp1, sizeof(dummyBmp1)) == 0);

    // Verify offsets in header/steps
    DWORD expectedBlobStart = (DWORD)(sizeof(TTPHeader) + 2 * sizeof(TTPStep));
    assert(loadedSteps[0].imageOffset == expectedBlobStart);
    assert(loadedSteps[0].imageSize == sizeof(dummyBmp0));
    assert(loadedSteps[1].imageOffset == expectedBlobStart + sizeof(dummyBmp0));
    assert(loadedSteps[1].imageSize == sizeof(dummyBmp1));

    ttp_free_project(loadedSteps, loadedBmps, loadedSizes, loadedCount);
    remove(testFilePath);
    printf("[TEST] test_roundtrip_with_images PASSED.\n");
}

static void test_empty_project(void) {
    printf("[TEST] Running test_empty_project...\n");
    const char* testFilePath = "test_empty.ttp";

    BOOL saveOk = ttp_save_project(testFilePath, NULL, 0, NULL, NULL);
    assert(saveOk == TRUE);

    TTPStep* loadedSteps = NULL;
    DWORD loadedCount = 0;
    BYTE** loadedBmps = NULL;
    DWORD* loadedSizes = NULL;

    BOOL loadOk = ttp_load_project(testFilePath, &loadedSteps, &loadedCount, &loadedBmps, &loadedSizes);
    assert(loadOk == TRUE);
    assert(loadedCount == 0);
    assert(loadedSteps == NULL);
    assert(loadedBmps == NULL);
    assert(loadedSizes == NULL);

    ttp_free_project(loadedSteps, loadedBmps, loadedSizes, loadedCount);
    remove(testFilePath);
    printf("[TEST] test_empty_project PASSED.\n");
}

static void test_step_without_image(void) {
    printf("[TEST] Running test_step_without_image...\n");
    const char* testFilePath = "test_no_img.ttp";

    TTPStep step;
    memset(&step, 0, sizeof(step));
    step.stepId = 42;
    step.actionType = TTP_ACTION_HOTKEY;
    step.targetMode = TTP_TARGET_COORD;
    step.origX = 50;
    step.origY = 75;

    const BYTE* bmpBuffers[1] = { NULL };
    DWORD bmpSizes[1] = { 0 };

    BOOL saveOk = ttp_save_project(testFilePath, &step, 1, bmpBuffers, bmpSizes);
    assert(saveOk == TRUE);

    TTPStep* loadedSteps = NULL;
    DWORD loadedCount = 0;
    BYTE** loadedBmps = NULL;
    DWORD* loadedSizes = NULL;

    BOOL loadOk = ttp_load_project(testFilePath, &loadedSteps, &loadedCount, &loadedBmps, &loadedSizes);
    assert(loadOk == TRUE);
    assert(loadedCount == 1);
    assert(loadedSteps != NULL);
    assert(loadedSteps[0].stepId == 42);
    assert(loadedSteps[0].imageOffset == 0);
    assert(loadedSteps[0].imageSize == 0);
    assert(loadedSizes[0] == 0);
    assert(loadedBmps[0] == NULL);

    ttp_free_project(loadedSteps, loadedBmps, loadedSizes, loadedCount);
    remove(testFilePath);
    printf("[TEST] test_step_without_image PASSED.\n");
}

static void test_invalid_file(void) {
    printf("[TEST] Running test_invalid_file...\n");
    const char* testFilePath = "test_invalid.ttp";

    // Non-existent file
    TTPStep* loadedSteps = NULL;
    DWORD loadedCount = 0;
    BYTE** loadedBmps = NULL;
    DWORD* loadedSizes = NULL;
    assert(ttp_load_project("non_existent_file.xyz", &loadedSteps, &loadedCount, &loadedBmps, &loadedSizes) == FALSE);

    // Corrupt magic
    FILE* fp = fopen(testFilePath, "wb");
    assert(fp != NULL);
    const char badMagic[4] = "BAD!";
    fwrite(badMagic, 1, 4, fp);
    DWORD dummy = 0;
    fwrite(&dummy, sizeof(DWORD), 3, fp);
    fclose(fp);

    assert(ttp_load_project(testFilePath, &loadedSteps, &loadedCount, &loadedBmps, &loadedSizes) == FALSE);
    remove(testFilePath);

    // Null parameters
    assert(ttp_save_project(NULL, NULL, 0, NULL, NULL) == FALSE);
    assert(ttp_load_project(NULL, &loadedSteps, &loadedCount, &loadedBmps, &loadedSizes) == FALSE);

    printf("[TEST] test_invalid_file PASSED.\n");
}

int main(void) {
    printf("[TEST] Starting all TTP storage unit tests...\n");
    test_roundtrip_with_images();
    test_empty_project();
    test_step_without_image();
    test_invalid_file();
    printf("[TEST] ALL UNIT TESTS PASSED SUCCESSFULLY!\n");
    return 0;
}
