#ifndef TTP_ENGINE_H
#define TTP_ENGINE_H

#include "ttp_core.h"
#include "ttp_vision.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fallback user choice returned by timeout modal or handler */
#define TTP_TIMEOUT_RETRY        1
#define TTP_TIMEOUT_USE_RECORDED 2
#define TTP_TIMEOUT_SKIP         3
#define TTP_TIMEOUT_STOP         4

/* Action Synthesizer Lifecycle:
 * Aggregates high-frequency 10ms mouse move streams and raw button/key states
 * into high-level semantic actions: Click, DblClick, RClick, Drag, TypeText.
 */
void  ttp_synth_init(void);
void  ttp_synth_reset(void);
void  ttp_synth_add_mouse_event(DWORD uMsg, LONG x, LONG y, DWORD timestamp);
void  ttp_synth_add_key_event(DWORD vkCode, BOOL isDown, DWORD timestamp);
DWORD ttp_synth_finalize(TTPStep* outSteps, DWORD maxSteps);

/* Playback Engine:
 * Executes a single step:
 * 1. Checks step->targetMode:
 *    - TTP_TARGET_TEXT: searches controls matching step->textKey. If multiple matches, picks closest by Euclidean distance to (origX, origY).
 *    - TTP_TARGET_IMAGE: runs NCC template match using bmpData / bmpSize.
 *    - TTP_TARGET_COORD: directly uses (origX, origY).
 * 2. If target not found immediately, polls every 50-200ms until step->timeoutMs.
 * 3. If timeout expired without target, triggers modal dialog or callback:
 *    - Retry: resets timer and retries
 *    - Use Recorded Pos: clicks (origX, origY)
 *    - Skip: skips step and returns TRUE
 *    - Stop: stops and returns FALSE
 * 4. Synthesizes input event via mouse_event / SendInput / keybd_event.
 * 5. Sleeps step->postDelayMs.
 */
typedef int (*TTPTimeoutCallback)(const TTPStep* step, void* userData);

void ttp_engine_set_timeout_callback(TTPTimeoutCallback cb, void* userData);
BOOL ttp_playback_step(const TTPStep* step, const BYTE* bmpData, DWORD bmpSize, HWND hParentForModal);

/* Default native Win32 modal dialog for timeout prompt */
int ttp_show_timeout_dialog(HWND hParent, const TTPStep* step);

#ifdef __cplusplus
}
#endif

#endif /* TTP_ENGINE_H */
