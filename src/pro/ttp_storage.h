#ifndef TTP_STORAGE_H
#define TTP_STORAGE_H

#include <windows.h>
#include "ttp_core.h"

#ifdef __cplusplus
extern "C" {
#endif

BOOL ttp_save_project(const char* filepath, const TTPStep* steps, DWORD stepCount, const BYTE** bmpBuffers, const DWORD* bmpSizes);
BOOL ttp_load_project(const char* filepath, TTPStep** outSteps, DWORD* outStepCount, BYTE*** outBmpBuffers, DWORD** outBmpSizes);
void ttp_free_project(TTPStep* steps, BYTE** bmpBuffers, DWORD* bmpSizes, DWORD stepCount);

#ifdef __cplusplus
}
#endif

#endif /* TTP_STORAGE_H */
