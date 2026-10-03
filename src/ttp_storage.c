#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ttp_storage.h"

BOOL ttp_save_project(const char* filepath, const TTPStep* steps, DWORD stepCount, const BYTE** bmpBuffers, const DWORD* bmpSizes) {
    if (!filepath) {
        return FALSE;
    }
    if (stepCount > 0 && !steps) {
        return FALSE;
    }

    FILE* fp = fopen(filepath, "wb");
    if (!fp) {
        return FALSE;
    }

    TTPHeader header;
    memset(&header, 0, sizeof(header));
    memcpy(header.magic, TTP_MAGIC, 4);
    header.version = TTP_VERSION;
    header.stepCount = stepCount;
    header.flags = 0;

    if (fwrite(&header, sizeof(TTPHeader), 1, fp) != 1) {
        fclose(fp);
        return FALSE;
    }

    TTPStep* stepsCopy = NULL;
    if (stepCount > 0) {
        stepsCopy = (TTPStep*)malloc(sizeof(TTPStep) * stepCount);
        if (!stepsCopy) {
            fclose(fp);
            return FALSE;
        }
        memcpy(stepsCopy, steps, sizeof(TTPStep) * stepCount);

        DWORD currentBlobOffset = (DWORD)(sizeof(TTPHeader) + stepCount * sizeof(TTPStep));
        for (DWORD i = 0; i < stepCount; i++) {
            if (bmpSizes && bmpBuffers && bmpSizes[i] > 0 && bmpBuffers[i] != NULL) {
                stepsCopy[i].imageOffset = currentBlobOffset;
                stepsCopy[i].imageSize = bmpSizes[i];
                currentBlobOffset += bmpSizes[i];
            } else {
                stepsCopy[i].imageOffset = 0;
                stepsCopy[i].imageSize = 0;
            }
        }

        if (fwrite(stepsCopy, sizeof(TTPStep), stepCount, fp) != stepCount) {
            free(stepsCopy);
            fclose(fp);
            return FALSE;
        }

        for (DWORD i = 0; i < stepCount; i++) {
            if (stepsCopy[i].imageSize > 0 && bmpBuffers && bmpBuffers[i] != NULL) {
                if (fwrite(bmpBuffers[i], 1, stepsCopy[i].imageSize, fp) != stepsCopy[i].imageSize) {
                    free(stepsCopy);
                    fclose(fp);
                    return FALSE;
                }
            }
        }

        free(stepsCopy);
    }

    fclose(fp);
    return TRUE;
}

BOOL ttp_load_project(const char* filepath, TTPStep** outSteps, DWORD* outStepCount, BYTE*** outBmpBuffers, DWORD** outBmpSizes) {
    if (!filepath || !outSteps || !outStepCount || !outBmpBuffers || !outBmpSizes) {
        return FALSE;
    }

    *outSteps = NULL;
    *outStepCount = 0;
    *outBmpBuffers = NULL;
    *outBmpSizes = NULL;

    FILE* fp = fopen(filepath, "rb");
    if (!fp) {
        return FALSE;
    }

    TTPHeader header;
    if (fread(&header, sizeof(TTPHeader), 1, fp) != 1) {
        fclose(fp);
        return FALSE;
    }

    if (memcmp(header.magic, TTP_MAGIC, 4) != 0) {
        fclose(fp);
        return FALSE;
    }

    DWORD count = header.stepCount;
    TTPStep* steps = NULL;
    BYTE** bmpBuffers = NULL;
    DWORD* bmpSizes = NULL;

    if (count > 0) {
        steps = (TTPStep*)malloc(sizeof(TTPStep) * count);
        if (!steps) {
            fclose(fp);
            return FALSE;
        }

        if (fread(steps, sizeof(TTPStep), count, fp) != count) {
            free(steps);
            fclose(fp);
            return FALSE;
        }

        bmpBuffers = (BYTE**)calloc(count, sizeof(BYTE*));
        bmpSizes = (DWORD*)calloc(count, sizeof(DWORD));
        if (!bmpBuffers || !bmpSizes) {
            if (bmpBuffers) free(bmpBuffers);
            if (bmpSizes) free(bmpSizes);
            free(steps);
            fclose(fp);
            return FALSE;
        }

        for (DWORD i = 0; i < count; i++) {
            if (steps[i].imageSize > 0) {
                if (fseek(fp, (long)steps[i].imageOffset, SEEK_SET) != 0) {
                    ttp_free_project(steps, bmpBuffers, bmpSizes, count);
                    fclose(fp);
                    return FALSE;
                }

                bmpBuffers[i] = (BYTE*)malloc(steps[i].imageSize);
                if (!bmpBuffers[i]) {
                    ttp_free_project(steps, bmpBuffers, bmpSizes, count);
                    fclose(fp);
                    return FALSE;
                }

                if (fread(bmpBuffers[i], 1, steps[i].imageSize, fp) != steps[i].imageSize) {
                    ttp_free_project(steps, bmpBuffers, bmpSizes, count);
                    fclose(fp);
                    return FALSE;
                }

                bmpSizes[i] = steps[i].imageSize;
            } else {
                bmpBuffers[i] = NULL;
                bmpSizes[i] = 0;
            }
        }
    }

    fclose(fp);

    *outSteps = steps;
    *outStepCount = count;
    *outBmpBuffers = bmpBuffers;
    *outBmpSizes = bmpSizes;

    return TRUE;
}

void ttp_free_project(TTPStep* steps, BYTE** bmpBuffers, DWORD* bmpSizes, DWORD stepCount) {
    if (bmpBuffers) {
        for (DWORD i = 0; i < stepCount; i++) {
            if (bmpBuffers[i]) {
                free(bmpBuffers[i]);
                bmpBuffers[i] = NULL;
            }
        }
        free(bmpBuffers);
    }
    if (bmpSizes) {
        free(bmpSizes);
    }
    if (steps) {
        free(steps);
    }
}
