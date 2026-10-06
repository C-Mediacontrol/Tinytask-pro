#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "ttp_core.h"
#include "ttp_vision.h"
#include "ttp_engine.h"

/* =========================================================================
 * 1. Action Synthesizer Implementation
 * ========================================================================= */

typedef struct {
    DWORD actionType;
    DWORD targetMode;
    LONG origX;
    LONG origY;
    LONG destX;
    LONG destY;
    DWORD timeoutMs;
    DWORD postDelayMs;
    char textKey[128];
    DWORD timestamp;
} SynthAction;

static SynthAction* g_actions = NULL;
static DWORD g_actionCount = 0;
static DWORD g_actionCap = 0;

/* Mouse state tracking */
static BOOL  g_isLDown = FALSE;
static LONG  g_lDownX = 0;
static LONG  g_lDownY = 0;
static LONG  g_lLastX = 0;
static LONG  g_lLastY = 0;
static DWORD g_lDownTime = 0;

static BOOL  g_isRDown = FALSE;
static LONG  g_rDownX = 0;
static LONG  g_rDownY = 0;
static DWORD g_rDownTime = 0;

static DWORD g_lastClickTime = 0;

/* Text typing aggregation buffer */
static char g_textBuffer[128] = {0};
static int  g_textLen = 0;
static LONG g_textStartX = 0;
static LONG g_textStartY = 0;

static BOOL synth_add_action(const SynthAction* act) {
    if (g_actionCount >= g_actionCap) {
        DWORD newCap = (g_actionCap == 0) ? 64 : g_actionCap * 2;
        SynthAction* newBuf;
        if (!g_actions) {
            newBuf = (SynthAction*)HeapAlloc(GetProcessHeap(), 0, newCap * sizeof(SynthAction));
        } else {
            newBuf = (SynthAction*)HeapReAlloc(GetProcessHeap(), 0, g_actions, newCap * sizeof(SynthAction));
        }
        if (!newBuf) return FALSE;
        g_actions = newBuf;
        g_actionCap = newCap;
    }
    g_actions[g_actionCount++] = *act;
    return TRUE;
}

static void synth_flush_text(void) {
    if (g_textLen > 0) {
        SynthAction act;
        __builtin_memset(&act, 0, sizeof(act));
        act.actionType = TTP_ACTION_TYPE_TEXT;
        act.targetMode = TTP_TARGET_COORD;
        act.origX = g_textStartX;
        act.origY = g_textStartY;
        act.destX = g_textStartX;
        act.destY = g_textStartY;
        act.timeoutMs = 3000;
        act.postDelayMs = 100;
        lstrcpynA(act.textKey, g_textBuffer, sizeof(act.textKey));
        synth_add_action(&act);

        __builtin_memset(g_textBuffer, 0, sizeof(g_textBuffer));
        g_textLen = 0;
    }
}

void ttp_synth_init(void) {
    ttp_register_timeout_dialog_class(NULL);
    ttp_synth_reset();
}

void ttp_synth_reset(void) {
    g_actionCount = 0;
    g_isLDown = FALSE;
    g_lDownX = 0;
    g_lDownY = 0;
    g_lLastX = 0;
    g_lLastY = 0;
    g_lDownTime = 0;

    g_isRDown = FALSE;
    g_rDownX = 0;
    g_rDownY = 0;
    g_rDownTime = 0;

    g_lastClickTime = 0;

    __builtin_memset(g_textBuffer, 0, sizeof(g_textBuffer));
    g_textLen = 0;
    g_textStartX = 0;
    g_textStartY = 0;
}

void ttp_synth_add_mouse_event(DWORD uMsg, LONG x, LONG y, DWORD timestamp) {
    if (uMsg == WM_MOUSEMOVE) {
        if (g_isLDown) {
            g_lLastX = x;
            g_lLastY = y;
        }
        /* Free mouse movements without buttons held are suppressed */
        return;
    }

    if (uMsg == WM_LBUTTONDOWN) {
        synth_flush_text();
        g_isLDown = TRUE;
        g_lDownX = x;
        g_lDownY = y;
        g_lLastX = x;
        g_lLastY = y;
        g_lDownTime = timestamp;
        return;
    }

    if (uMsg == WM_LBUTTONUP) {
        if (!g_isLDown) return;
        g_isLDown = FALSE;
        synth_flush_text();

        double dist = ttp_calc_euclidean_dist(g_lDownX, g_lDownY, x, y);
        int sysDrag = GetSystemMetrics(SM_CXDRAG);
        double dragThresh = (sysDrag > 0 && sysDrag * 3 > 20) ? (double)(sysDrag * 3) : 20.0;
        if (dist > dragThresh) {
            /* Drag threshold exceeded -> TTP_ACTION_DRAG */
            SynthAction act;
            __builtin_memset(&act, 0, sizeof(act));
            act.actionType = TTP_ACTION_DRAG;
            act.targetMode = TTP_TARGET_COORD;
            act.origX = g_lDownX;
            act.origY = g_lDownY;
            act.destX = x;
            act.destY = y;
            act.timeoutMs = 3000;
            act.postDelayMs = 100;
            act.timestamp = timestamp;
            synth_add_action(&act);
            g_lastClickTime = 0;
        } else {
            /* Click or Double Click */
            UINT dblClickTime = GetDoubleClickTime();
            if (dblClickTime == 0) dblClickTime = 400;

            int sysDbl = GetSystemMetrics(SM_CXDOUBLECLK);
            double dblDistThresh = (sysDbl > 0 && sysDbl * 2 > 16) ? (double)(sysDbl * 2) : 16.0;

            BOOL isDbl = FALSE;
            if (g_actionCount > 0) {
                SynthAction* lastAct = &g_actions[g_actionCount - 1];
                if (lastAct->actionType == TTP_ACTION_CLICK) {
                    double clickDist = ttp_calc_euclidean_dist(lastAct->origX, lastAct->origY, g_lDownX, g_lDownY);
                    if (clickDist <= dblDistThresh && (timestamp >= lastAct->timestamp) && (timestamp - lastAct->timestamp <= dblClickTime)) {
                        /* Upgrade preceding click to DblClick */
                        lastAct->actionType = TTP_ACTION_DBLCLICK;
                        lastAct->timestamp = timestamp;
                        isDbl = TRUE;
                        g_lastClickTime = 0;
                    }
                }
            }

            if (!isDbl) {
                SynthAction act;
                __builtin_memset(&act, 0, sizeof(act));
                act.actionType = TTP_ACTION_CLICK;
                act.targetMode = TTP_TARGET_COORD;
                act.origX = g_lDownX;
                act.origY = g_lDownY;
                act.destX = g_lDownX;
                act.destY = g_lDownY;
                act.timeoutMs = 3000;
                act.postDelayMs = 100;
                act.timestamp = timestamp;
                synth_add_action(&act);
                g_lastClickTime = timestamp;
            }
        }
        return;
    }

    if (uMsg == WM_RBUTTONDOWN) {
        synth_flush_text();
        g_isRDown = TRUE;
        g_rDownX = x;
        g_rDownY = y;
        g_rDownTime = timestamp;
        return;
    }

    if (uMsg == WM_RBUTTONUP) {
        if (!g_isRDown) return;
        g_isRDown = FALSE;
        synth_flush_text();

        SynthAction act;
        __builtin_memset(&act, 0, sizeof(act));
        act.actionType = TTP_ACTION_RCLICK;
        act.targetMode = TTP_TARGET_COORD;
        act.origX = g_rDownX;
        act.origY = g_rDownY;
        act.destX = x;
        act.destY = y;
        act.timeoutMs = 3000;
        act.postDelayMs = 100;
        act.timestamp = timestamp;
        synth_add_action(&act);
        g_lastClickTime = 0;
        return;
    }

    if (uMsg == WM_LBUTTONDBLCLK) {
        synth_flush_text();
        if (g_actionCount > 0 && g_actions[g_actionCount - 1].actionType == TTP_ACTION_CLICK) {
            g_actions[g_actionCount - 1].actionType = TTP_ACTION_DBLCLICK;
        } else {
            SynthAction act;
            __builtin_memset(&act, 0, sizeof(act));
            act.actionType = TTP_ACTION_DBLCLICK;
            act.targetMode = TTP_TARGET_COORD;
            act.origX = x;
            act.origY = y;
            act.destX = x;
            act.destY = y;
            act.timeoutMs = 3000;
            act.postDelayMs = 100;
            act.timestamp = timestamp;
            synth_add_action(&act);
        }
        g_lastClickTime = 0;
        return;
    }
}

static BOOL is_non_printable_hotkey(DWORD vkCode) {
    if (vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL) return TRUE;
    if (vkCode == VK_MENU || vkCode == VK_LMENU || vkCode == VK_RMENU) return TRUE;
    if (vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT) return TRUE;
    if (vkCode == VK_RETURN) return TRUE;
    if (vkCode == VK_TAB) return TRUE;
    if (vkCode == VK_ESCAPE) return TRUE;
    if (vkCode == VK_BACK) return TRUE;
    if (vkCode == VK_CAPITAL) return TRUE;
    if (vkCode == VK_PAUSE) return TRUE;
    if (vkCode == VK_SNAPSHOT) return TRUE;
    if (vkCode == VK_INSERT || vkCode == VK_DELETE) return TRUE;
    if (vkCode == VK_HOME || vkCode == VK_END || vkCode == VK_PRIOR || vkCode == VK_NEXT) return TRUE;
    if (vkCode == VK_LWIN || vkCode == VK_RWIN) return TRUE;
    if (vkCode < 32) return TRUE;
    return FALSE;
}

void ttp_synth_add_key_event(DWORD vkCode, BOOL isDown, DWORD timestamp) {
    /* Only key down events trigger action transitions / character entry */
    if (!isDown) {
        return;
    }

    if (is_non_printable_hotkey(vkCode)) {
        /* Non-printable key / modifier -> Flush text and record HOTKEY */
        synth_flush_text();

        SynthAction act;
        __builtin_memset(&act, 0, sizeof(act));
        act.actionType = TTP_ACTION_HOTKEY;
        act.targetMode = TTP_TARGET_COORD;
        act.origX = (LONG)vkCode;
        act.destX = (LONG)vkCode;
        act.timeoutMs = 3000;
        act.postDelayMs = 100;
        act.timestamp = timestamp;
        wsprintfA(act.textKey, "%lu", (unsigned long)vkCode);
        synth_add_action(&act);
    } else {
        /* Printable key -> aggregate into typing buffer */
        if (g_textLen >= 127) {
            synth_flush_text();
        }

        if (g_textLen == 0) {
            POINT pt;
            if (GetCursorPos(&pt)) {
                g_textStartX = pt.x;
                g_textStartY = pt.y;
            }
        }

        g_textBuffer[g_textLen++] = (char)(unsigned char)vkCode;
        g_textBuffer[g_textLen] = '\0';
    }
}

DWORD ttp_synth_finalize(TTPStep* outSteps, DWORD maxSteps) {
    if (g_isLDown) {
        ttp_synth_add_mouse_event(WM_LBUTTONUP, g_lLastX, g_lLastY, GetTickCount());
    }
    if (g_isRDown) {
        ttp_synth_add_mouse_event(WM_RBUTTONUP, g_rDownX, g_rDownY, GetTickCount());
    }
    synth_flush_text();

    DWORD total = g_actionCount;
    if (outSteps && maxSteps > 0) {
        DWORD copyCount = (total < maxSteps) ? total : maxSteps;
        for (DWORD i = 0; i < copyCount; i++) {
            __builtin_memset(&outSteps[i], 0, sizeof(TTPStep));
            outSteps[i].stepId = i + 1;
            outSteps[i].actionType = g_actions[i].actionType;
            outSteps[i].targetMode = g_actions[i].targetMode ? g_actions[i].targetMode : TTP_TARGET_COORD;
            outSteps[i].origX = g_actions[i].origX;
            outSteps[i].origY = g_actions[i].origY;
            outSteps[i].destX = g_actions[i].destX;
            outSteps[i].destY = g_actions[i].destY;
            outSteps[i].timeoutMs = g_actions[i].timeoutMs ? g_actions[i].timeoutMs : 3000;
            outSteps[i].postDelayMs = g_actions[i].postDelayMs ? g_actions[i].postDelayMs : 100;
            lstrcpynA(outSteps[i].textKey, g_actions[i].textKey, sizeof(outSteps[i].textKey));
        }
        return copyCount;
    }
    return total;
}

/* =========================================================================
 * 2. Timeout Prompt Native Modal Dialog
 * ========================================================================= */

static BOOL s_timeoutRunning = FALSE;
static int  s_timeoutResult = TTP_TIMEOUT_STOP;

static LRESULT CALLBACK TimeoutDlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        if (id == 1001) {
            s_timeoutResult = TTP_TIMEOUT_RETRY;
            s_timeoutRunning = FALSE;
            DestroyWindow(hwnd);
            return 0;
        } else if (id == 1002) {
            s_timeoutResult = TTP_TIMEOUT_USE_RECORDED;
            s_timeoutRunning = FALSE;
            DestroyWindow(hwnd);
            return 0;
        } else if (id == 1003) {
            s_timeoutResult = TTP_TIMEOUT_SKIP;
            s_timeoutRunning = FALSE;
            DestroyWindow(hwnd);
            return 0;
        } else if (id == 1004 || id == IDCANCEL) {
            s_timeoutResult = TTP_TIMEOUT_STOP;
            s_timeoutRunning = FALSE;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE: {
        s_timeoutResult = TTP_TIMEOUT_STOP;
        s_timeoutRunning = FALSE;
        DestroyWindow(hwnd);
        return 0;
    }
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

void ttp_register_timeout_dialog_class(HINSTANCE hInst) {
    if (!hInst) hInst = GetModuleHandleA(NULL);
    WNDCLASSEXA wc;
    __builtin_memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    if (!GetClassInfoExA(hInst, "TTPTimeoutDialog", &wc)) {
        __builtin_memset(&wc, 0, sizeof(wc));
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = TimeoutDlgProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "TTPTimeoutDialog";
        RegisterClassExA(&wc);
    }
}

int ttp_show_timeout_dialog(HWND hParent, const TTPStep* step) {
    if (!step) return TTP_TIMEOUT_STOP;

    HINSTANCE hInst = GetModuleHandleA(NULL);
    ttp_register_timeout_dialog_class(hInst);

    int posX = 100, posY = 100;
    if (hParent && IsWindow(hParent)) {
        RECT rc;
        GetWindowRect(hParent, &rc);
        posX = rc.left + (rc.right - rc.left - 380) / 2;
        posY = rc.top + (rc.bottom - rc.top - 155) / 2;
    } else {
        posX = (GetSystemMetrics(SM_CXSCREEN) - 380) / 2;
        posY = (GetSystemMetrics(SM_CYSCREEN) - 155) / 2;
    }
    if (posX < 0) posX = 100;
    if (posY < 0) posY = 100;

    s_timeoutResult = TTP_TIMEOUT_STOP;
    s_timeoutRunning = TRUE;

    HWND hDlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "TTPTimeoutDialog",
        "TinyTask Pro - Timeout",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        posX, posY, 380, 155,
        hParent, NULL, hInst, NULL
    );
    if (!hDlg) return TTP_TIMEOUT_STOP;

    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    char msgText[256];
    const char* modeStr = (step->targetMode == TTP_TARGET_TEXT) ? "Text" :
                          (step->targetMode == TTP_TARGET_IMAGE) ? "Image" : "Coordinate";
    wsprintfA(msgText,
             "Step %lu (%s) timed out searching for target.\nRecorded pos: (%ld, %ld). Choose action:",
             (unsigned long)step->stepId, modeStr, step->origX, step->origY);

    HWND hStatic = CreateWindowExA(0, "STATIC", msgText,
        WS_CHILD | WS_VISIBLE, 15, 12, 350, 42, hDlg, NULL, hInst, NULL);
    SendMessageA(hStatic, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hBtn1 = CreateWindowExA(0, "BUTTON", "&Retry",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        15, 68, 70, 26, hDlg, (HMENU)(INT_PTR)1001, hInst, NULL);
    SendMessageA(hBtn1, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hBtn2 = CreateWindowExA(0, "BUTTON", "&Recorded Pos",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        90, 68, 110, 26, hDlg, (HMENU)(INT_PTR)1002, hInst, NULL);
    SendMessageA(hBtn2, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hBtn3 = CreateWindowExA(0, "BUTTON", "&Skip",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        205, 68, 70, 26, hDlg, (HMENU)(INT_PTR)1003, hInst, NULL);
    SendMessageA(hBtn3, WM_SETFONT, (WPARAM)hFont, TRUE);

    HWND hBtn4 = CreateWindowExA(0, "BUTTON", "S&top",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        280, 68, 70, 26, hDlg, (HMENU)(INT_PTR)1004, hInst, NULL);
    SendMessageA(hBtn4, WM_SETFONT, (WPARAM)hFont, TRUE);

    if (hParent && IsWindow(hParent)) {
        EnableWindow(hParent, FALSE);
    }

    MSG msg;
    while (s_timeoutRunning && GetMessageA(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            s_timeoutResult = TTP_TIMEOUT_STOP;
            s_timeoutRunning = FALSE;
            DestroyWindow(hDlg);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (hParent && IsWindow(hParent)) {
        EnableWindow(hParent, TRUE);
        SetActiveWindow(hParent);
    }

    return s_timeoutResult;
}

/* =========================================================================
 * 3. Playback Engine Implementation
 * ========================================================================= */

static TTPTimeoutCallback g_timeoutCallback = NULL;
static void* g_timeoutUserData = NULL;

void ttp_engine_set_timeout_callback(TTPTimeoutCallback cb, void* userData) {
    g_timeoutCallback = cb;
    g_timeoutUserData = userData;
}

static HDC s_hdcScreenOverride = NULL;

void ttp_engine_set_screen_dc_override(HDC hdcOverride) {
    s_hdcScreenOverride = hdcOverride;
}

static void get_screen_or_dc_dimensions(HDC hdc, int* pW, int* pH) {
    if (s_hdcScreenOverride) {
        HBITMAP hBmp = (HBITMAP)GetCurrentObject(hdc, OBJ_BITMAP);
        BITMAP bm;
        if (hBmp && GetObjectA(hBmp, sizeof(bm), &bm) > 0 && bm.bmWidth > 0 && bm.bmHeight > 0) {
            *pW = bm.bmWidth;
            *pH = bm.bmHeight;
            return;
        }
    }
    *pW = GetSystemMetrics(SM_CXSCREEN);
    *pH = GetSystemMetrics(SM_CYSCREEN);
}

static inline int score_to_int(double s) {
    return (int)s;
}

static inline int score_to_frac(double s) {
    double d = (s >= 0.0) ? s : -s;
    int frac = (int)((d - (int)d) * 10000.0 + 0.5);
    return (frac >= 10000) ? 9999 : frac;
}

void ttp_diag_log(const char* fmt, ...) {
    char msgBuf[512];
    va_list args;
    va_start(args, fmt);
    wvsprintfA(msgBuf, fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char fullLog[600];
    wsprintfA(fullLog, "[%02d:%02d:%02d.%03d] %s\r\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msgBuf);

    OutputDebugStringA(fullLog);

    HANDLE hFile = CreateFileA("tinytask_debug.log", FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        DWORD len = (DWORD)lstrlenA(fullLog);
        WriteFile(hFile, fullLog, len, &written, NULL);
        CloseHandle(hFile);
    }
}

static int handle_timeout_choice(const TTPStep* step, HWND hParent) {
    if (g_timeoutCallback) {
        return g_timeoutCallback(step, g_timeoutUserData);
    }
    int presetAction = TTP_GET_TIMEOUT_ACTION(step->targetMode);
    if (presetAction == TTP_TIMEOUT_ACT_RETRY) return TTP_TIMEOUT_RETRY;
    if (presetAction == TTP_TIMEOUT_ACT_USE_RECORDED) return TTP_TIMEOUT_USE_RECORDED;
    if (presetAction == TTP_TIMEOUT_ACT_SKIP) return TTP_TIMEOUT_SKIP;
    if (presetAction == TTP_TIMEOUT_ACT_STOP) return TTP_TIMEOUT_STOP;

    return ttp_show_timeout_dialog(hParent, step);
}

static const char* ttp_strstr(const char* haystack, const char* needle) {
    if (!haystack || !needle) return NULL;
    int nlen = lstrlenA(needle);
    if (nlen == 0) return haystack;
    int hlen = lstrlenA(haystack);
    if (hlen < nlen) return NULL;
    for (int i = 0; i <= hlen - nlen; i++) {
        BOOL match = TRUE;
        for (int j = 0; j < nlen; j++) {
            if (haystack[i + j] != needle[j]) {
                match = FALSE;
                break;
            }
        }
        if (match) return &haystack[i];
    }
    return NULL;
}

static BOOL ttp_match_visual_target_adaptive(const TTPStep* step, const BYTE* bmpData, DWORD bmpSize, double baseMinScore, POINT* outTargetPos) {
    if (!step || !bmpData || bmpSize == 0) return FALSE;

    HDC hdcScreen = s_hdcScreenOverride ? s_hdcScreenOverride : GetDC(NULL);
    int screenW = 0, screenH = 0;
    get_screen_or_dc_dimensions(hdcScreen, &screenW, &screenH);

    // Tier 1A: Localized ROI Fast Search (radius = 200px around recorded position)
    POINT matchPos = { step->origX, step->origY };
    double score = 0.0;
    BOOL matched = FALSE;
    ttp_match_template_ncc_roi(hdcScreen, step->origX, step->origY, 200, bmpData, bmpSize, 0.45, &matchPos, &score);
    if (score >= baseMinScore) {
        matched = TRUE;
    }
    ttp_diag_log("  [VISUAL] Tier 1 ROI: matched=%d, score=%d.%04d, pos=(%ld, %ld)",
        matched, score_to_int(score), score_to_frac(score), matchPos.x, matchPos.y);

    // Plan A + Plan D: If score is in marginal range (0.45 <= score < baseMinScore), trigger Speculative Pre-Hover Probe & Spatial Adaptive Gate
    if (!matched && score >= 0.45) {
        int dx = (int)(matchPos.x - step->origX);
        int dy = (int)(matchPos.y - step->origY);
        int distSq = dx * dx + dy * dy;

        // Plan D Spatial Gate: If strictly in near-origin neighborhood (dist <= 20px, distSq <= 400) and score >= 0.55, directly accept
        if (distSq <= 400 && score >= 0.55) {
            matched = TRUE;
            ttp_diag_log("  [VISUAL] Plan D Spatial Gate: Near-origin (%d px) score=%d.%04d accepted!",
                (int)ttp_isqrt((unsigned long long)distSq), score_to_int(score), score_to_frac(score));
        } else if (!s_hdcScreenOverride && distSq <= 22500) { // within 150px
            // Plan A Hover Probe: Move cursor to candidate position to trigger Hover state in host application
            SetCursorPos(matchPos.x, matchPos.y);
            Sleep(35); // Allow host UI thread to paint hover highlight

            // Re-capture and re-test ROI
            ReleaseDC(NULL, hdcScreen);
            hdcScreen = GetDC(NULL);

            POINT hoverMatchPos = matchPos;
            double hoverScore = 0.0;
            BOOL hoverMatched = ttp_match_template_ncc_roi(hdcScreen, matchPos.x, matchPos.y, 80, bmpData, bmpSize, 0.65, &hoverMatchPos, &hoverScore);
            ttp_diag_log("  [VISUAL] Plan A Hover Probe: hoverMatched=%d, hoverScore=%d.%04d at (%ld, %ld)",
                hoverMatched, score_to_int(hoverScore), score_to_frac(hoverScore), hoverMatchPos.x, hoverMatchPos.y);

            if (hoverMatched || hoverScore >= 0.65) {
                matched = TRUE;
                matchPos = hoverMatchPos;
            } else if (distSq <= 400 && hoverScore >= 0.55) { // Plan D fallback after hover probe
                matched = TRUE;
                matchPos = hoverMatchPos;
            }
        }
    }

    // Tier 2: Fall back to full-screen pyramid search if Tier 1 misses (target moved far away)
    if (!matched) {
        matched = ttp_match_template_ncc(hdcScreen, screenW, screenH, bmpData, bmpSize, baseMinScore, &matchPos, &score);
        ttp_diag_log("  [VISUAL] Tier 2 Full: matched=%d, score=%d.%04d, pos=(%ld, %ld)",
            matched, score_to_int(score), score_to_frac(score), matchPos.x, matchPos.y);
    }

    if (!s_hdcScreenOverride) ReleaseDC(NULL, hdcScreen);

    if (matched && outTargetPos) {
        *outTargetPos = matchPos;
    }
    return matched;
}

BOOL ttp_playback_step(const TTPStep* step, const BYTE* bmpData, DWORD bmpSize, HWND hParentForModal) {
    if (!step) return FALSE;

    LONG targetX = step->origX;
    LONG targetY = step->origY;
    BOOL targetFound = FALSE;
    DWORD baseMode = TTP_GET_BASE_TARGET_MODE(step->targetMode);

    ttp_diag_log("[STEP_START] Step %lu: action=%lu, targetMode=%lu (base=%lu), orig=(%ld,%ld), text=\"%s\", bmpSize=%lu",
        step->stepId, step->actionType, step->targetMode, baseMode, step->origX, step->origY, step->textKey, bmpSize);

    if (baseMode == TTP_TARGET_COORD || baseMode == 0) {
        targetX = step->origX;
        targetY = step->origY;
        targetFound = TRUE;
        ttp_diag_log("  [COORD] Mode is COORD, using orig: (%ld, %ld)", targetX, targetY);
    } else if (baseMode == TTP_TARGET_TEXT) {
        DWORD startTick = GetTickCount();
        DWORD timeout = (step->timeoutMs > 0) ? step->timeoutMs : 3000;

        while (!targetFound) {
            if (step->textKey[0] != '\0') {
                /* Fast check: does accessible object at orig pos match? */
                char accName[128] = {0};
                POINT origPt = { step->origX, step->origY };
                BOOL accHit = ttp_get_accessible_name_at_point(origPt, accName, sizeof(accName));
                ttp_diag_log("  [TEXT] OrigPt (%ld, %ld) Accessible: \"%s\" (hit=%d)",
                    origPt.x, origPt.y, accName, accHit);
                if (accHit && ttp_strstr(accName, step->textKey)) {
                    targetX = step->origX;
                    targetY = step->origY;
                    targetFound = TRUE;
                    ttp_diag_log("  [TEXT] Fast-check MATCHED at orig! target=(%ld, %ld)", targetX, targetY);
                    break;
                }

                POINT candidates[64];
                int count = ttp_find_elements_by_text(step->textKey, candidates, 64);
                ttp_diag_log("  [TEXT] find_elements_by_text(\"%s\"): count=%d", step->textKey, count);
                for (int k = 0; k < count && k < 5; k++) {
                    ttp_diag_log("    candidate[%d] = (%ld, %ld)", k, candidates[k].x, candidates[k].y);
                }
                if (count > 0) {
                    for (int candTry = 0; candTry < count; candTry++) {
                        int best = ttp_pick_nearest_candidate(step->origX, step->origY, candidates, count);
                        if (best < 0) break;
                        if (candidates[best].x <= -90000) break; // All remaining candidates invalidated

                        POINT candPt = candidates[best];
                        BOOL verified = TRUE;

                        /* Visual Cross-Validation:
                         * If visual template bmpData is present, verify that the pattern actually matches near this candidate!
                         * If candidate is a false positive (e.g. container title, background text),
                         * reject it and fall back to full-screen visual search! */
                        if (bmpData && bmpSize > 0) {
                            HDC hdcScr = s_hdcScreenOverride ? s_hdcScreenOverride : GetDC(NULL);
                            POINT roiPt = candPt;
                            double roiScore = 0.0;
                            BOOL roiOk = ttp_match_template_ncc_roi(hdcScr, candPt.x, candPt.y, 80, bmpData, bmpSize, 0.60, &roiPt, &roiScore);
                            if (!s_hdcScreenOverride) ReleaseDC(NULL, hdcScr);

                            ttp_diag_log("  [TEXT_CROSS_VALIDATE] Candidate %d at (%ld, %ld): roiOk=%d, score=%d.%04d",
                                best, candPt.x, candPt.y, roiOk, score_to_int(roiScore), score_to_frac(roiScore));

                            if (!roiOk) {
                                verified = FALSE;
                                candidates[best].x = -99999;
                                candidates[best].y = -99999;
                            } else {
                                candPt = roiPt; // refine to exact visual button center
                            }
                        }

                        if (verified) {
                            targetX = candPt.x;
                            targetY = candPt.y;
                            targetFound = TRUE;
                            ttp_diag_log("  [TEXT] Picked verified candidate %d: target=(%ld, %ld)", best, targetX, targetY);
                            break;
                        }
                    }
                }
            }

            /* Dual-Engine Fallback: If accessible text was not found or failed visual verification, attempt visual NCC match using bmpData */
            if (!targetFound && bmpData && bmpSize > 0) {
                POINT targetPt = {0};
                if (ttp_match_visual_target_adaptive(step, bmpData, bmpSize, 0.70, &targetPt)) {
                    targetX = targetPt.x;
                    targetY = targetPt.y;
                    targetFound = TRUE;
                    ttp_diag_log("  [VISUAL] Visual MATCHED! target=(%ld, %ld)", targetX, targetY);
                    break;
                }
            }

            if (GetTickCount() - startTick >= timeout) {
                ttp_diag_log("  [TIMEOUT] Expired (%lu ms)! Calling handle_timeout_choice...",
                    GetTickCount() - startTick);
                int choice = handle_timeout_choice(step, hParentForModal);
                ttp_diag_log("  [TIMEOUT] choice returned: %d", choice);
                if (choice == TTP_TIMEOUT_RETRY) {
                    startTick = GetTickCount();
                    continue;
                } else if (choice == TTP_TIMEOUT_USE_RECORDED) {
                    targetX = step->origX;
                    targetY = step->origY;
                    targetFound = TRUE;
                    ttp_diag_log("  [TIMEOUT] Using recorded pos: (%ld, %ld)", targetX, targetY);
                    break;
                } else if (choice == TTP_TIMEOUT_SKIP) {
                    ttp_diag_log("  [TIMEOUT] Skipping step %lu", step->stepId);
                    return TRUE;
                } else {
                    ttp_diag_log("  [TIMEOUT] Stopping macro playback");
                    return FALSE;
                }
            }

            Sleep(50);
        }
    } else if (baseMode == TTP_TARGET_IMAGE) {
        DWORD startTick = GetTickCount();
        DWORD timeout = (step->timeoutMs > 0) ? step->timeoutMs : 3000;

        while (!targetFound) {
            if (bmpData && bmpSize > 0) {
                POINT targetPt = {0};
                if (ttp_match_visual_target_adaptive(step, bmpData, bmpSize, 0.75, &targetPt)) {
                    targetX = targetPt.x;
                    targetY = targetPt.y;
                    targetFound = TRUE;
                    ttp_diag_log("  [IMAGE] Image MATCHED! target=(%ld, %ld)", targetX, targetY);
                    break;
                }
            }

            if (GetTickCount() - startTick >= timeout) {
                ttp_diag_log("  [TIMEOUT] Expired (%lu ms)! Calling handle_timeout_choice...",
                    GetTickCount() - startTick);
                int choice = handle_timeout_choice(step, hParentForModal);
                ttp_diag_log("  [TIMEOUT] choice returned: %d", choice);
                if (choice == TTP_TIMEOUT_RETRY) {
                    startTick = GetTickCount();
                    continue;
                } else if (choice == TTP_TIMEOUT_USE_RECORDED) {
                    targetX = step->origX;
                    targetY = step->origY;
                    targetFound = TRUE;
                    ttp_diag_log("  [TIMEOUT] Using recorded pos: (%ld, %ld)", targetX, targetY);
                    break;
                } else if (choice == TTP_TIMEOUT_SKIP) {
                    ttp_diag_log("  [TIMEOUT] Skipping step %lu", step->stepId);
                    return TRUE;
                } else {
                    ttp_diag_log("  [TIMEOUT] Stopping macro playback");
                    return FALSE;
                }
            }

            Sleep(50);
        }
    } else {
        targetX = step->origX;
        targetY = step->origY;
        targetFound = TRUE;
    }

    int scrW = 0, scrH = 0;
    get_screen_or_dc_dimensions(s_hdcScreenOverride, &scrW, &scrH);

    /* Strict screen bounds guard: reject off-screen coordinates to prevent wrapping/clamping to (0,0) */
    if (targetX < 0 || targetY < 0 || targetX >= scrW || targetY >= scrH) {
        ttp_diag_log("  [GUARD] Target coordinate (%ld, %ld) is off-screen (screen: %dx%d)! Aborting injection.",
            targetX, targetY, scrW, scrH);
        return FALSE;
    }

    DWORD absX = (DWORD)((targetX * 65535) / (scrW > 1 ? scrW - 1 : 1));
    DWORD absY = (DWORD)((targetY * 65535) / (scrH > 1 ? scrH - 1 : 1));

    ttp_diag_log("  [INJECT] action=%lu, target=(%ld, %ld), abs=(%lu, %lu)",
        step->actionType, targetX, targetY, absX, absY);

    /* Input event execution: mouse_event dispatched first, SetCursorPos hard-locks physical pixel (matching tinytask.c) */
    switch (step->actionType) {
    case TTP_ACTION_CLICK:
        mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, absX, absY, 0, 0);
        SetCursorPos(targetX, targetY);
        Sleep(15);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        Sleep(15);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        break;

    case TTP_ACTION_DBLCLICK:
        mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, absX, absY, 0, 0);
        SetCursorPos(targetX, targetY);
        Sleep(15);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        Sleep(50);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        break;

    case TTP_ACTION_RCLICK:
        mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, absX, absY, 0, 0);
        SetCursorPos(targetX, targetY);
        Sleep(15);
        mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
        Sleep(15);
        mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
        break;

    case TTP_ACTION_DRAG: {
        LONG startX = targetX;
        LONG startY = targetY;
        LONG deltaX = targetX - step->origX;
        LONG deltaY = targetY - step->origY;
        LONG endX = step->destX + deltaX;
        LONG endY = step->destY + deltaY;

        mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, absX, absY, 0, 0);
        SetCursorPos(startX, startY);
        Sleep(15);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        Sleep(15);
        const int steps = 15;
        for (int i = 1; i <= steps; i++) {
            LONG cx = startX + (endX - startX) * i / steps;
            LONG cy = startY + (endY - startY) * i / steps;
            DWORD acx = (DWORD)((cx * 65535ULL) / (scrW > 1 ? scrW - 1 : 1));
            DWORD acy = (DWORD)((cy * 65535ULL) / (scrH > 1 ? scrH - 1 : 1));
            mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, acx, acy, 0, 0);
            SetCursorPos(cx, cy);
            Sleep(10);
        }
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        break;
    }

    case TTP_ACTION_TYPE_TEXT: {
        if (targetX != 0 || targetY != 0) {
            SetCursorPos(targetX, targetY);
        }
        int len = lstrlenA(step->textKey);
        for (int i = 0; i < len; i++) {
            char ch = step->textKey[i];
            SHORT vk = VkKeyScanA(ch);
            if (vk != -1) {
                BYTE vkCode = LOBYTE(vk);
                BYTE shift = HIBYTE(vk);
                if (shift & 1) keybd_event(VK_SHIFT, 0, 0, 0);
                if (shift & 2) keybd_event(VK_CONTROL, 0, 0, 0);
                if (shift & 4) keybd_event(VK_MENU, 0, 0, 0);

                keybd_event(vkCode, 0, 0, 0);
                keybd_event(vkCode, 0, KEYEVENTF_KEYUP, 0);

                if (shift & 4) keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
                if (shift & 2) keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
                if (shift & 1) keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
            } else {
                INPUT inp[2] = {0};
                inp[0].type = INPUT_KEYBOARD;
                inp[0].ki.wScan = (WCHAR)(unsigned char)ch;
                inp[0].ki.dwFlags = KEYEVENTF_UNICODE;
                inp[1].type = INPUT_KEYBOARD;
                inp[1].ki.wScan = (WCHAR)(unsigned char)ch;
                inp[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                SendInput(2, inp, sizeof(INPUT));
            }
            Sleep(10);
        }
        break;
    }

    case TTP_ACTION_HOTKEY: {
        BYTE vk = (BYTE)step->origX;
        if (vk != 0) {
            keybd_event(vk, 0, 0, 0);
            Sleep(10);
            keybd_event(vk, 0, KEYEVENTF_KEYUP, 0);
        }
        break;
    }

    default:
        break;
    }

    ttp_diag_log("  [INJECT_DONE] Step %lu finished, sleeping postDelayMs=%lu",
        step->stepId, step->postDelayMs);

    if (step->postDelayMs > 0) {
        Sleep(step->postDelayMs);
    }

    return TRUE;
}
