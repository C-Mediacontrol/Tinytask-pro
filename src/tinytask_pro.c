#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ttp_core.h"
#include "ttp_storage.h"
#include "ttp_vision.h"
#include "ttp_engine.h"

/* Command IDs matching TinyTask Pro specification */
#define ID_PRO_OPEN      0x9000
#define ID_PRO_SAVE      0x9001
#define ID_PRO_REC       0x9002
#define ID_PRO_PLAY      0x9003
#define ID_PRO_STEPS     0x9004
#define ID_PRO_OPTIONS   0x9005

/* Drawer & Control IDs */
#define ID_LV_STEPS      0x9100
#define ID_BTN_ADD_STEP  0x9101
#define ID_BTN_DEL_STEP  0x9102
#define ID_BTN_MOVE_UP   0x9103
#define ID_BTN_MOVE_DOWN 0x9104
#define ID_BTN_STEP_RUN  0x9105
#define ID_EDIT_TIMEOUT  0x9106

/* Options menu IDs: Speed modes */
#define ID_OPT_SPEED_HALF    0x9200
#define ID_OPT_SPEED_1X      0x9201
#define ID_OPT_SPEED_2X      0x9202
#define ID_OPT_SPEED_100X    0x9203
#define ID_OPT_SPEED_CUSTOM  0x9204
#define ID_OPT_SET_SPEED     0x9205

/* Options menu IDs: Playback loops */
#define ID_OPT_CONT          0x9206
#define ID_OPT_SET_LOOPS     0x9207

/* Options menu IDs: Recording Hotkeys */
#define ID_REC_HOTKEY_STD    0x9210 /* Ctrl+Shift+Alt+R */
#define ID_REC_HOTKEY_PRTSC  0x9211 /* PrintScreen */
#define ID_REC_HOTKEY_F8     0x9212 /* F8 */
#define ID_REC_HOTKEY_F12    0x9213 /* F12 */
#define ID_REC_HOTKEY_CUSTOM_ACTIVE 0x9214
#define ID_REC_HOTKEY_CUSTOM_SET    0x9215

/* Options menu IDs: Playback Hotkeys */
#define ID_PLAY_HOTKEY_STD   0x9220 /* Ctrl+Shift+Alt+P */
#define ID_PLAY_HOTKEY_PRTSC 0x9221 /* PrintScreen */
#define ID_PLAY_HOTKEY_F8    0x9222 /* F8 */
#define ID_PLAY_HOTKEY_F12   0x9223 /* F12 */
#define ID_PLAY_HOTKEY_CUSTOM_ACTIVE 0x9224
#define ID_PLAY_HOTKEY_CUSTOM_SET    0x9225

/* Options menu IDs: Views, Themes & Pro Settings */
#define ID_OPT_TOPMOST          0x9230
#define ID_OPT_SHOW_CAPTIONS    0x9231
#define ID_OPT_TOOLBAR_CUSTOM   0x9232
#define ID_OPT_TOOLBAR_DEFAULT  0x9233
#define ID_OPT_DEFAULT_TIMEOUT  0x9234
#define ID_OPT_WEBSITE          0x9235
#define ID_OPT_ABOUT            0x9236

/* Speed mode constants matching TinyTask engine */
#define SPEED_HALF   0
#define SPEED_1X     1
#define SPEED_2X     2
#define SPEED_100X   100
#define SPEED_CUSTOM 999

/* Custom Messages */
#define WM_USER_PLAY_UPDATE (WM_USER + 10)
#define WM_USER_PLAY_DONE   (WM_USER + 11)

/* Dimensions */
#define BUTTON_WIDTH         38
#define BUTTON_HEIGHT        44
#define TOOLBAR_PADDING      5
#define NUM_BUTTONS          6
#define CLIENT_COLLAPSED_W   263
#define CLIENT_COLLAPSED_H   54
#define CLIENT_EXPANDED_W    380
#define CLIENT_EXPANDED_H    360

/* States */
#define STATE_IDLE      0
#define STATE_RECORDING 1
#define STATE_PLAYING   2

/* Timers */
#define TIMER_REC    1001
#define TIMER_HOTKEY 1005

/* Hotkey structure matching classic TinyTask */
typedef struct {
    int  mode;           /* 0=Standard, 1=PrtSc, 8=F8, 12=F12, 99=Custom */
    BOOL customSet;
    UINT customVk;
    UINT customMod;     /* MOD_CONTROL, MOD_ALT, MOD_SHIFT */
    char customName[64];
} HotkeyState;

/* Globals */
static HINSTANCE g_hInstance = NULL;
static HWND g_hMainWnd = NULL;
static HWND g_hListView = NULL;
static HWND g_hBtnAdd = NULL;
static HWND g_hBtnDel = NULL;
static HWND g_hBtnUp = NULL;
static HWND g_hBtnDown = NULL;
static HWND g_hBtnRun = NULL;
static HWND g_hInPlaceEdit = NULL;
static int  g_InPlaceEditItem = -1;
static WNDPROC g_OldEditProc = NULL;
static HFONT g_hGuiFont = NULL;

/* Toolbar Resources */
static HBITMAP g_hBmpToolbar = NULL;
static HBITMAP g_hBmpMask = NULL;
static BOOL    g_HasCustomToolbar = FALSE;
static char    g_CustomToolbarPath[MAX_PATH] = "";
static int     g_HideCaptionsOffset = 0; /* 0 = show captions (44px), 12 = hide captions (32px) */

/* Window Positioning */
static int  g_WindowX = -9999;
static int  g_WindowY = -9999;

/* Application State */
static int  g_State = STATE_IDLE;
static BOOL g_DrawerExpanded = FALSE;
static BOOL g_AlwaysOnTop = FALSE;
static BOOL g_Continuous = FALSE;
static int  g_SpeedMode = SPEED_1X;
static int  g_CustomSpeed = 5;
static DWORD g_PlayLoopTotal = 1;
static DWORD g_DefaultTimeoutSec = 3;
static char g_CurrentFileName[MAX_PATH] = "";
static DWORD g_RecStartTime = 0;
static DWORD g_PlayStartTime = 0;

/* Hotkey configurations */
static HotkeyState g_RecHotkey = { 0, FALSE, 0, 0, "" };
static HotkeyState g_PlayHotkey = { 0, FALSE, 0, 0, "" };

/* Dynamic Step Storage */
static TTPStep* g_steps = NULL;
static BYTE**   g_bmpBuffers = NULL;
static DWORD*   g_bmpSizes = NULL;
static DWORD    g_stepCount = 0;
static DWORD    g_stepCap = 0;

/* Playback thread controls */
static HANDLE g_hPlayThread = NULL;
static volatile BOOL g_StopPlaybackRequested = FALSE;
static DWORD g_CurrentPlayStep = 0;
static DWORD g_CurrentPlayLoop = 0;

/* Recorded Click Templates for Vision integration */
typedef struct {
    LONG x;
    LONG y;
    DWORD timestamp;
    BYTE* bmpData;
    DWORD bmpSize;
    char text[128];
} RecordedClick;

static RecordedClick* g_recClicks = NULL;
static DWORD g_recClickCount = 0;
static DWORD g_recClickCap = 0;

/* Polling input tracking */
static BYTE g_LastKeyState[256];
static POINT g_LastMousePos;

/* Forward declarations */
static void RefreshListView(void);
static void UpdateTitle(void);
static void SetDrawerState(BOOL expanded);
static void CommitInPlaceEdit(BOOL save);
static void StartPlayback(void);
static void StopPlayback(void);
static void StartRecording(void);
static void StopRecording(void);
static void CreateToolbarBitmaps(void);
static void LoadConfig(void);
static void SaveConfig(void);
static void ShowOptionsMenu(HWND hwnd, int x, int y);

/* =========================================================================
 * 1. Step Array Management
 * ========================================================================= */

static void StepArray_Clear(void) {
    if (g_bmpBuffers) {
        for (DWORD i = 0; i < g_stepCount; i++) {
            if (g_bmpBuffers[i]) {
                free(g_bmpBuffers[i]);
                g_bmpBuffers[i] = NULL;
            }
        }
        free(g_bmpBuffers);
        g_bmpBuffers = NULL;
    }
    if (g_bmpSizes) {
        free(g_bmpSizes);
        g_bmpSizes = NULL;
    }
    if (g_steps) {
        free(g_steps);
        g_steps = NULL;
    }
    g_stepCount = 0;
    g_stepCap = 0;
}

static BOOL StepArray_EnsureCap(DWORD needed) {
    if (needed <= g_stepCap) return TRUE;
    DWORD newCap = (g_stepCap == 0) ? 32 : g_stepCap * 2;
    if (newCap < needed) newCap = needed;

    TTPStep* newSteps = (TTPStep*)realloc(g_steps, newCap * sizeof(TTPStep));
    if (!newSteps) return FALSE;
    g_steps = newSteps;

    BYTE** newBmps = (BYTE**)realloc(g_bmpBuffers, newCap * sizeof(BYTE*));
    if (!newBmps) return FALSE;
    g_bmpBuffers = newBmps;

    DWORD* newSizes = (DWORD*)realloc(g_bmpSizes, newCap * sizeof(DWORD));
    if (!newSizes) return FALSE;
    g_bmpSizes = newSizes;

    for (DWORD i = g_stepCap; i < newCap; i++) {
        g_bmpBuffers[i] = NULL;
        g_bmpSizes[i] = 0;
    }
    g_stepCap = newCap;
    return TRUE;
}

static BOOL StepArray_Add(const TTPStep* step, const BYTE* bmpData, DWORD bmpSize) {
    if (!StepArray_EnsureCap(g_stepCount + 1)) return FALSE;
    g_steps[g_stepCount] = *step;
    g_steps[g_stepCount].stepId = g_stepCount + 1;

    if (bmpData && bmpSize > 0) {
        g_bmpBuffers[g_stepCount] = (BYTE*)malloc(bmpSize);
        if (g_bmpBuffers[g_stepCount]) {
            memcpy(g_bmpBuffers[g_stepCount], bmpData, bmpSize);
            g_bmpSizes[g_stepCount] = bmpSize;
        } else {
            g_bmpSizes[g_stepCount] = 0;
        }
    } else {
        g_bmpBuffers[g_stepCount] = NULL;
        g_bmpSizes[g_stepCount] = 0;
    }

    g_stepCount++;
    return TRUE;
}

static void StepArray_Renumber(void) {
    for (DWORD i = 0; i < g_stepCount; i++) {
        g_steps[i].stepId = i + 1;
    }
}

static BOOL StepArray_Delete(DWORD index) {
    if (index >= g_stepCount) return FALSE;
    if (g_bmpBuffers[index]) {
        free(g_bmpBuffers[index]);
    }
    for (DWORD i = index; i + 1 < g_stepCount; i++) {
        g_steps[i] = g_steps[i + 1];
        g_bmpBuffers[i] = g_bmpBuffers[i + 1];
        g_bmpSizes[i] = g_bmpSizes[i + 1];
    }
    g_bmpBuffers[g_stepCount - 1] = NULL;
    g_bmpSizes[g_stepCount - 1] = 0;
    g_stepCount--;
    StepArray_Renumber();
    return TRUE;
}

static BOOL StepArray_MoveUp(DWORD index) {
    if (index == 0 || index >= g_stepCount) return FALSE;
    TTPStep tmpStep = g_steps[index - 1];
    BYTE* tmpBmp = g_bmpBuffers[index - 1];
    DWORD tmpSize = g_bmpSizes[index - 1];

    g_steps[index - 1] = g_steps[index];
    g_bmpBuffers[index - 1] = g_bmpBuffers[index];
    g_bmpSizes[index - 1] = g_bmpSizes[index];

    g_steps[index] = tmpStep;
    g_bmpBuffers[index] = tmpBmp;
    g_bmpSizes[index] = tmpSize;

    StepArray_Renumber();
    return TRUE;
}

static BOOL StepArray_MoveDown(DWORD index) {
    if (index + 1 >= g_stepCount) return FALSE;
    return StepArray_MoveUp(index + 1);
}

/* =========================================================================
 * 2. Recorded Clicks Buffer
 * ========================================================================= */

static void ClearRecordedClicks(void) {
    if (g_recClicks) {
        for (DWORD i = 0; i < g_recClickCount; i++) {
            if (g_recClicks[i].bmpData) {
                free(g_recClicks[i].bmpData);
            }
        }
        free(g_recClicks);
        g_recClicks = NULL;
    }
    g_recClickCount = 0;
    g_recClickCap = 0;
}

static void AddRecordedClick(LONG x, LONG y, DWORD timestamp, BYTE* bmpData, DWORD bmpSize, const char* text) {
    if (g_recClickCount >= g_recClickCap) {
        DWORD newCap = (g_recClickCap == 0) ? 16 : g_recClickCap * 2;
        RecordedClick* newArr = (RecordedClick*)realloc(g_recClicks, newCap * sizeof(RecordedClick));
        if (!newArr) return;
        g_recClicks = newArr;
        g_recClickCap = newCap;
    }
    RecordedClick* rc = &g_recClicks[g_recClickCount++];
    rc->x = x;
    rc->y = y;
    rc->timestamp = timestamp;
    rc->bmpData = bmpData;
    rc->bmpSize = bmpSize;
    if (text) {
        strncpy(rc->text, text, sizeof(rc->text) - 1);
        rc->text[sizeof(rc->text) - 1] = '\0';
    } else {
        rc->text[0] = '\0';
    }
}

/* =========================================================================
 * 3. String & Formatting Utilities
 * ========================================================================= */

static const char* GetActionName(DWORD actionType) {
    switch (actionType) {
        case TTP_ACTION_CLICK:     return "Click";
        case TTP_ACTION_DBLCLICK:  return "DblClick";
        case TTP_ACTION_RCLICK:    return "RClick";
        case TTP_ACTION_DRAG:      return "Drag";
        case TTP_ACTION_TYPE_TEXT: return "Type";
        case TTP_ACTION_HOTKEY:    return "Hotkey";
        default:                   return "Action";
    }
}

static void GetTargetDescription(const TTPStep* step, char* buf, size_t bufSize) {
    if (step->actionType == TTP_ACTION_TYPE_TEXT) {
        snprintf(buf, bufSize, "\"%s\"", step->textKey);
        return;
    }
    if (step->actionType == TTP_ACTION_DRAG) {
        snprintf(buf, bufSize, "(%ld,%ld)->(%ld,%ld)", step->origX, step->origY, step->destX, step->destY);
        return;
    }
    if (step->targetMode == TTP_TARGET_TEXT && step->textKey[0] != '\0') {
        snprintf(buf, bufSize, "[%s] (Text)", step->textKey);
        return;
    }
    if (step->targetMode == TTP_TARGET_IMAGE) {
        snprintf(buf, bufSize, "Image Anchor (%ld,%ld)", step->origX, step->origY);
        return;
    }
    snprintf(buf, bufSize, "(%ld,%ld)", step->origX, step->origY);
}

static void FormatTimeoutString(DWORD timeoutMs, char* buf, size_t bufSize) {
    double sec = (double)timeoutMs / 1000.0;
    snprintf(buf, bufSize, "%.1fs", sec);
}

static DWORD ParseTimeoutString(const char* str) {
    while (*str == ' ' || *str == '\t') str++;
    double sec = atof(str);
    if (sec <= 0.05) sec = 0.1;
    if (sec > 300.0) sec = 300.0;
    return (DWORD)(sec * 1000.0 + 0.5);
}

static char* FindLastChar(const char* s, char c) {
    const char* last = NULL;
    while (*s) {
        if (*s == c) last = s;
        s++;
    }
    return (char*)last;
}

/* =========================================================================
 * 4. Modal Dialogs: Prompt Dialog & Hotkey Capture Dialog
 * ========================================================================= */

static const char* g_PromptLabel = "";
static int  g_PromptDefault = 0;
static int  g_PromptMin = 0;
static int  g_PromptMax = 0;
static int  g_PromptResult = 0;
static BOOL g_PromptDlgRunning = FALSE;
static HWND g_hPromptEdit = NULL;

static LRESULT CALLBACK PromptDlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND hStatic = CreateWindowExA(0, "STATIC", g_PromptLabel,
            WS_CHILD | WS_VISIBLE, 15, 12, 300, 20, hwnd, NULL, g_hInstance, NULL);
        SendMessageA(hStatic, WM_SETFONT, (WPARAM)hFont, TRUE);

        char numStr[32];
        wsprintfA(numStr, "%d", g_PromptDefault);
        g_hPromptEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", numStr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
            15, 36, 300, 22, hwnd, (HMENU)101, g_hInstance, NULL);
        SendMessageA(g_hPromptEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnOk = CreateWindowExA(0, "BUTTON", "OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            85, 68, 75, 24, hwnd, (HMENU)IDOK, g_hInstance, NULL);
        SendMessageA(hBtnOk, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnCancel = CreateWindowExA(0, "BUTTON", "Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            175, 68, 75, 24, hwnd, (HMENU)IDCANCEL, g_hInstance, NULL);
        SendMessageA(hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

        SetFocus(g_hPromptEdit);
        SendMessageA(g_hPromptEdit, EM_SETSEL, 0, -1);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        if (id == IDOK) {
            char buf[32] = "";
            GetWindowTextA(g_hPromptEdit, buf, sizeof(buf));
            int val = 0;
            for (int i = 0; buf[i]; i++) {
                if (buf[i] >= '0' && buf[i] <= '9') val = val * 10 + (buf[i] - '0');
            }
            if (val < g_PromptMin) val = g_PromptMin;
            if (val > g_PromptMax) val = g_PromptMax;
            g_PromptResult = val;
            g_PromptDlgRunning = FALSE;
            DestroyWindow(hwnd);
        } else if (id == IDCANCEL) {
            g_PromptResult = g_PromptDefault;
            g_PromptDlgRunning = FALSE;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_CLOSE:
        g_PromptResult = g_PromptDefault;
        g_PromptDlgRunning = FALSE;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

static int PromptNumber(HWND hParent, const char* title, const char* label, int defVal, int minVal, int maxVal) {
    static BOOL s_registered = FALSE;
    if (!s_registered) {
        WNDCLASSEXA wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = PromptDlgProc;
        wc.hInstance = g_hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "TTP_PromptDlg";
        RegisterClassExA(&wc);
        s_registered = TRUE;
    }

    g_PromptLabel = label;
    g_PromptDefault = defVal;
    g_PromptMin = minVal;
    g_PromptMax = maxVal;
    g_PromptResult = defVal;
    g_PromptDlgRunning = TRUE;

    RECT rcParent;
    GetWindowRect(hParent, &rcParent);
    int posX = rcParent.left + (rcParent.right - rcParent.left - 340) / 2;
    int posY = rcParent.top + (rcParent.bottom - rcParent.top - 145) / 2;
    if (posX < 0) posX = 100;
    if (posY < 0) posY = 100;

    HWND hDlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "TTP_PromptDlg", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        posX, posY, 340, 145,
        hParent, NULL, g_hInstance, NULL
    );

    EnableWindow(hParent, FALSE);
    MSG msg;
    while (g_PromptDlgRunning && GetMessageA(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            SendMessageA(hDlg, WM_COMMAND, IDCANCEL, 0);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN) {
            SendMessageA(hDlg, WM_COMMAND, IDOK, 0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    EnableWindow(hParent, TRUE);
    SetActiveWindow(hParent);
    return g_PromptResult;
}

/* Modal Hotkey capture dialog */
static BOOL g_HotkeyDlgRunning = FALSE;
static BOOL g_HotkeyAccepted = FALSE;
static UINT g_CapturedVk = 0;
static UINT g_CapturedMod = 0;
static char g_CapturedName[64] = "";
static HWND g_hHotkeyStaticDisplay = NULL;

static UINT GetCurrentModifiers(void) {
    UINT mod = 0;
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ||
        (GetAsyncKeyState(VK_LCONTROL) & 0x8000) ||
        (GetAsyncKeyState(VK_RCONTROL) & 0x8000)) {
        mod |= MOD_CONTROL;
    }
    if ((GetAsyncKeyState(VK_MENU) & 0x8000) ||
        (GetAsyncKeyState(VK_LMENU) & 0x8000) ||
        (GetAsyncKeyState(VK_RMENU) & 0x8000)) {
        mod |= MOD_ALT;
    }
    if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ||
        (GetAsyncKeyState(VK_LSHIFT) & 0x8000) ||
        (GetAsyncKeyState(VK_RSHIFT) & 0x8000)) {
        mod |= MOD_SHIFT;
    }
    return mod;
}

static void FormatHotkeyText(UINT vk, UINT mod, char* out, int maxLen) {
    out[0] = '\0';
    if (mod & MOD_CONTROL) lstrcatA(out, "Ctrl + ");
    if (mod & MOD_ALT)     lstrcatA(out, "Alt + ");
    if (mod & MOD_SHIFT)   lstrcatA(out, "Shift + ");

    if (vk == 0) {
        if (mod != 0) lstrcatA(out, "...");
        else lstrcatA(out, "[ Press any key... ]");
        return;
    }

    if (vk >= VK_F1 && vk <= VK_F24) {
        char fStr[16];
        wsprintfA(fStr, "F%d", vk - VK_F1 + 1);
        lstrcatA(out, fStr);
    } else if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        char kStr[2] = { (char)vk, '\0' };
        lstrcatA(out, kStr);
    } else {
        DWORD scan = MapVirtualKeyA(vk, 0);
        DWORD lp = (scan << 16);
        if (vk == VK_INSERT || vk == VK_DELETE || vk == VK_HOME || vk == VK_END ||
            vk == VK_PRIOR || vk == VK_NEXT || vk == VK_LEFT || vk == VK_UP ||
            vk == VK_RIGHT || vk == VK_DOWN || vk == VK_SNAPSHOT) {
            lp |= (1 << 24);
        }
        char name[32] = "";
        if (GetKeyNameTextA(lp, name, sizeof(name)) > 0) {
            lstrcatA(out, name);
        } else {
            char codeStr[16];
            wsprintfA(codeStr, "Key %d", vk);
            lstrcatA(out, codeStr);
        }
    }
}

static LRESULT CALLBACK HotkeyDlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND hLbl = CreateWindowExA(0, "STATIC", "Press your desired key combination:",
            WS_CHILD | WS_VISIBLE, 15, 12, 320, 18, hwnd, NULL, g_hInstance, NULL);
        SendMessageA(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hHotkeyStaticDisplay = CreateWindowExA(WS_EX_CLIENTEDGE, "STATIC",
            g_CapturedName[0] ? g_CapturedName : "[ Press any key... ]",
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
            15, 34, 320, 28, hwnd, (HMENU)101, g_hInstance, NULL);
        SendMessageA(g_hHotkeyStaticDisplay, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hHint = CreateWindowExA(0, "STATIC", "Supports combinations like Ctrl + Alt + R, F8, etc.",
            WS_CHILD | WS_VISIBLE | SS_CENTER, 15, 68, 320, 32, hwnd, NULL, g_hInstance, NULL);
        SendMessageA(hHint, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnOk = CreateWindowExA(0, "BUTTON", "OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            95, 106, 75, 26, hwnd, (HMENU)IDOK, g_hInstance, NULL);
        SendMessageA(hBtnOk, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnCancel = CreateWindowExA(0, "BUTTON", "Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            185, 106, 75, 26, hwnd, (HMENU)IDCANCEL, g_hInstance, NULL);
        SendMessageA(hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

        SetFocus(hwnd);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        UINT vk = (UINT)wParam;
        UINT mod = GetCurrentModifiers();

        if (vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
            vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) {
            FormatHotkeyText(0, mod, g_CapturedName, sizeof(g_CapturedName));
            SetWindowTextA(g_hHotkeyStaticDisplay, g_CapturedName);
            return 0;
        }

        g_CapturedVk = vk;
        g_CapturedMod = mod;
        FormatHotkeyText(vk, mod, g_CapturedName, sizeof(g_CapturedName));
        SetWindowTextA(g_hHotkeyStaticDisplay, g_CapturedName);
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        if (id == IDOK) {
            if (g_CapturedVk != 0) {
                g_HotkeyAccepted = TRUE;
                g_HotkeyDlgRunning = FALSE;
                DestroyWindow(hwnd);
            }
        } else if (id == IDCANCEL) {
            g_HotkeyAccepted = FALSE;
            g_HotkeyDlgRunning = FALSE;
            DestroyWindow(hwnd);
        }
        return 0;
    }
    case WM_CLOSE:
        g_HotkeyAccepted = FALSE;
        g_HotkeyDlgRunning = FALSE;
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

static BOOL CaptureHotkey(HWND hParent, const char* title, HotkeyState* pHk) {
    static BOOL s_registered = FALSE;
    if (!s_registered) {
        WNDCLASSEXA wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = HotkeyDlgProc;
        wc.hInstance = g_hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "TTP_HotkeyDlg";
        RegisterClassExA(&wc);
        s_registered = TRUE;
    }

    g_CapturedVk = pHk->customVk;
    g_CapturedMod = pHk->customMod;
    if (pHk->customSet && pHk->customName[0]) {
        lstrcpynA(g_CapturedName, pHk->customName, sizeof(g_CapturedName));
    } else {
        g_CapturedName[0] = '\0';
    }
    g_HotkeyAccepted = FALSE;
    g_HotkeyDlgRunning = TRUE;

    RECT rcParent;
    GetWindowRect(hParent, &rcParent);
    int posX = rcParent.left + (rcParent.right - rcParent.left - 360) / 2;
    int posY = rcParent.top + (rcParent.bottom - rcParent.top - 180) / 2;
    if (posX < 0) posX = 100;
    if (posY < 0) posY = 100;

    HWND hDlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "TTP_HotkeyDlg", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        posX, posY, 360, 180,
        hParent, NULL, g_hInstance, NULL
    );

    EnableWindow(hParent, FALSE);
    MSG msg;
    while (g_HotkeyDlgRunning && GetMessageA(&msg, NULL, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && GetCurrentModifiers() == 0) {
            SendMessageA(hDlg, WM_COMMAND, IDCANCEL, 0);
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && GetCurrentModifiers() == 0 && g_CapturedVk != 0) {
            SendMessageA(hDlg, WM_COMMAND, IDOK, 0);
            continue;
        }
        if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN ||
            msg.message == WM_KEYUP || msg.message == WM_SYSKEYUP) {
            SendMessageA(hDlg, msg.message, msg.wParam, msg.lParam);
            if (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN) {
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    EnableWindow(hParent, TRUE);
    SetActiveWindow(hParent);

    if (g_HotkeyAccepted && g_CapturedVk != 0) {
        pHk->customVk = g_CapturedVk;
        pHk->customMod = g_CapturedMod;
        lstrcpynA(pHk->customName, g_CapturedName, sizeof(pHk->customName));
        pHk->customSet = TRUE;
        pHk->mode = 99;
        return TRUE;
    }
    return FALSE;
}

/* Config persistence */
static void GetIniPath(char* buf, int maxLen) {
    GetModuleFileNameA(NULL, buf, maxLen);
    char* dot = FindLastChar(buf, '.');
    if (dot) lstrcpynA(dot, ".ini", maxLen - (int)(dot - buf));
    else lstrcatA(buf, ".ini");
}

static void WriteIniInt(const char* key, int val, const char* ini) {
    char buf[32];
    wsprintfA(buf, "%d", val);
    WritePrivateProfileStringA("TinyTaskPro", key, buf, ini);
}

static void LoadConfig(void) {
    char ini[MAX_PATH];
    GetIniPath(ini, sizeof(ini));

    g_WindowX = GetPrivateProfileIntA("TinyTaskPro", "window_x", -9999, ini);
    g_WindowY = GetPrivateProfileIntA("TinyTaskPro", "window_y", -9999, ini);
    g_AlwaysOnTop = GetPrivateProfileIntA("TinyTaskPro", "topmost", 0, ini);
    g_HideCaptionsOffset = GetPrivateProfileIntA("TinyTaskPro", "hide_captions", 0, ini) ? 12 : 0;
    g_CustomSpeed = GetPrivateProfileIntA("TinyTaskPro", "speed_custom", 5, ini);
    if (g_CustomSpeed < 1) g_CustomSpeed = 1;
    if (g_CustomSpeed > 100) g_CustomSpeed = 100;

    int spd = GetPrivateProfileIntA("TinyTaskPro", "speed", 1, ini);
    if (spd == 0) g_SpeedMode = SPEED_HALF;
    else if (spd == 1) g_SpeedMode = SPEED_1X;
    else if (spd == 2) g_SpeedMode = SPEED_2X;
    else if (spd == 100) g_SpeedMode = SPEED_100X;
    else { g_SpeedMode = SPEED_CUSTOM; g_CustomSpeed = spd; }

    g_Continuous = GetPrivateProfileIntA("TinyTaskPro", "continuous", 0, ini);
    g_PlayLoopTotal = GetPrivateProfileIntA("TinyTaskPro", "loops", 1, ini);
    if (g_PlayLoopTotal < 1) g_PlayLoopTotal = 1;

    g_DefaultTimeoutSec = GetPrivateProfileIntA("TinyTaskPro", "default_timeout", 3, ini);
    if (g_DefaultTimeoutSec < 1) g_DefaultTimeoutSec = 3;

    g_RecHotkey.mode = GetPrivateProfileIntA("TinyTaskPro", "record_key", 0, ini);
    g_PlayHotkey.mode = GetPrivateProfileIntA("TinyTaskPro", "play_key", 0, ini);

    g_RecHotkey.customVk = GetPrivateProfileIntA("TinyTaskPro", "custom_rec_vk", 0, ini);
    g_RecHotkey.customMod = GetPrivateProfileIntA("TinyTaskPro", "custom_rec_mod", 0, ini);
    GetPrivateProfileStringA("TinyTaskPro", "custom_rec_name", "", g_RecHotkey.customName, sizeof(g_RecHotkey.customName), ini);
    if (g_RecHotkey.customVk != 0) g_RecHotkey.customSet = TRUE;

    g_PlayHotkey.customVk = GetPrivateProfileIntA("TinyTaskPro", "custom_play_vk", 0, ini);
    g_PlayHotkey.customMod = GetPrivateProfileIntA("TinyTaskPro", "custom_play_mod", 0, ini);
    GetPrivateProfileStringA("TinyTaskPro", "custom_play_name", "", g_PlayHotkey.customName, sizeof(g_PlayHotkey.customName), ini);
    if (g_PlayHotkey.customVk != 0) g_PlayHotkey.customSet = TRUE;

    GetPrivateProfileStringA("TinyTaskPro", "toolbar_image", "", g_CustomToolbarPath, sizeof(g_CustomToolbarPath), ini);
    if (g_CustomToolbarPath[0]) g_HasCustomToolbar = TRUE;
}

static void SaveConfig(void) {
    char ini[MAX_PATH];
    GetIniPath(ini, sizeof(ini));

    WriteIniInt("window_x", g_WindowX, ini);
    WriteIniInt("window_y", g_WindowY, ini);
    WriteIniInt("topmost", g_AlwaysOnTop ? 1 : 0, ini);
    WriteIniInt("hide_captions", g_HideCaptionsOffset ? 1 : 0, ini);

    int spdVal = 1;
    if (g_SpeedMode == SPEED_HALF) spdVal = 0;
    else if (g_SpeedMode == SPEED_1X) spdVal = 1;
    else if (g_SpeedMode == SPEED_2X) spdVal = 2;
    else if (g_SpeedMode == SPEED_100X) spdVal = 100;
    else if (g_SpeedMode == SPEED_CUSTOM) spdVal = g_CustomSpeed;
    WriteIniInt("speed", spdVal, ini);
    WriteIniInt("speed_custom", g_CustomSpeed, ini);

    WriteIniInt("continuous", g_Continuous ? 1 : 0, ini);
    WriteIniInt("loops", g_PlayLoopTotal, ini);
    WriteIniInt("default_timeout", g_DefaultTimeoutSec, ini);

    WriteIniInt("record_key", g_RecHotkey.mode, ini);
    WriteIniInt("play_key", g_PlayHotkey.mode, ini);

    if (g_RecHotkey.customSet) {
        WriteIniInt("custom_rec_vk", g_RecHotkey.customVk, ini);
        WriteIniInt("custom_rec_mod", g_RecHotkey.customMod, ini);
        WritePrivateProfileStringA("TinyTaskPro", "custom_rec_name", g_RecHotkey.customName, ini);
    }

    if (g_PlayHotkey.customSet) {
        WriteIniInt("custom_play_vk", g_PlayHotkey.customVk, ini);
        WriteIniInt("custom_play_mod", g_PlayHotkey.customMod, ini);
        WritePrivateProfileStringA("TinyTaskPro", "custom_play_name", g_PlayHotkey.customName, ini);
    }

    WritePrivateProfileStringA("TinyTaskPro", "toolbar_image", g_HasCustomToolbar ? g_CustomToolbarPath : "", ini);
}

static BOOL CheckHotkeyConflict(HWND hwnd, int newMode, UINT newVk, UINT newMod, BOOL isRecording) {
    const HotkeyState* other = isRecording ? &g_PlayHotkey : &g_RecHotkey;
    if (newMode != 0) {
        if (newMode == other->mode && newMode != 99) {
            MessageBoxA(hwnd, "Hotkey Conflict", "TinyTask Pro", MB_ICONINFORMATION);
            return TRUE;
        }
        if (newMode == 99 && other->mode == 99 && newVk == other->customVk && newMod == other->customMod) {
            MessageBoxA(hwnd, "Hotkey Conflict", "TinyTask Pro", MB_ICONINFORMATION);
            return TRUE;
        }
    }
    return FALSE;
}

static void SetPresetHotkey(HWND hwnd, HotkeyState* hk, int mode, UINT vk, BOOL isRec) {
    if (mode == 0 || !CheckHotkeyConflict(hwnd, mode, vk, 0, isRec)) {
        hk->mode = mode;
        SaveConfig();
    }
}

static BOOL IsHotkeyTriggered(const HotkeyState* hk, char stdKey) {
    if (hk->mode == 0) {
        return ((GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
                (GetAsyncKeyState(VK_MENU) & 0x8000) &&
                (GetAsyncKeyState(stdKey) & 0x8000));
    }
    if (hk->mode == 1)  return (GetAsyncKeyState(VK_SNAPSHOT) & 0x8000) != 0;
    if (hk->mode == 8)  return (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    if (hk->mode == 12) return (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
    if (hk->mode == 99 && hk->customSet && hk->customVk != 0) {
        UINT mod = GetCurrentModifiers();
        if (mod == hk->customMod) {
            return (GetAsyncKeyState(hk->customVk) & 0x8000) != 0;
        }
    }
    return FALSE;
}

/* =========================================================================
 * 5. Toolbar Bitmaps & GDI Rendering
 * ========================================================================= */

static void CreateToolbarMask(void) {
    if (!g_hBmpToolbar) return;
    if (g_hBmpMask) { DeleteObject(g_hBmpMask); g_hBmpMask = NULL; }

    BITMAP bm;
    GetObjectA(g_hBmpToolbar, sizeof(bm), &bm);
    int w = bm.bmWidth;
    int h = bm.bmHeight;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcSrc = CreateCompatibleDC(hdcScreen);
    HDC hdcMask = CreateCompatibleDC(hdcScreen);

    HBITMAP hOldSrc = (HBITMAP)SelectObject(hdcSrc, g_hBmpToolbar);
    g_hBmpMask = CreateBitmap(w, h, 1, 1, NULL);
    HBITMAP hOldMask = (HBITMAP)SelectObject(hdcMask, g_hBmpMask);

    COLORREF crTransparent = GetPixel(hdcSrc, 0, 0);
    SetBkColor(hdcSrc, crTransparent);

    BitBlt(hdcMask, 0, 0, w, h, hdcSrc, 0, 0, SRCCOPY);
    BitBlt(hdcSrc, 0, 0, w, h, hdcMask, 0, 0, SRCINVERT);

    SelectObject(hdcSrc, hOldSrc);
    SelectObject(hdcMask, hOldMask);
    DeleteDC(hdcSrc);
    DeleteDC(hdcMask);
    ReleaseDC(NULL, hdcScreen);
}

static void CreateToolbarBitmaps(void) {
    if (g_hBmpToolbar) { DeleteObject(g_hBmpToolbar); g_hBmpToolbar = NULL; }
    if (g_HasCustomToolbar && g_CustomToolbarPath[0]) {
        g_hBmpToolbar = (HBITMAP)LoadImageA(NULL, g_CustomToolbarPath, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    }
    if (!g_hBmpToolbar) {
        g_hBmpToolbar = (HBITMAP)LoadImageA(g_hInstance, MAKEINTRESOURCEA(4002), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
        g_HasCustomToolbar = FALSE;
    }
    CreateToolbarMask();
}

/* =========================================================================
 * 6. ListView & In-Place Editing
 * ========================================================================= */

static void RefreshListView(void) {
    if (!g_hListView) return;
    SendMessageA(g_hListView, WM_SETREDRAW, FALSE, 0);

    int prevSel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
    ListView_DeleteAllItems(g_hListView);

    for (DWORD i = 0; i < g_stepCount; i++) {
        char numStr[16];
        snprintf(numStr, sizeof(numStr), "%lu", (unsigned long)(i + 1));

        LVITEMA lvi;
        memset(&lvi, 0, sizeof(lvi));
        lvi.mask = LVIF_TEXT;
        lvi.iItem = i;
        lvi.iSubItem = 0;
        lvi.pszText = numStr;
        ListView_InsertItem(g_hListView, &lvi);

        ListView_SetItemText(g_hListView, i, 1, (LPSTR)GetActionName(g_steps[i].actionType));

        char targetDesc[128];
        GetTargetDescription(&g_steps[i], targetDesc, sizeof(targetDesc));
        ListView_SetItemText(g_hListView, i, 2, targetDesc);

        char timeoutStr[32];
        FormatTimeoutString(g_steps[i].timeoutMs, timeoutStr, sizeof(timeoutStr));
        ListView_SetItemText(g_hListView, i, 3, timeoutStr);

        char assetStr[32];
        if (g_bmpBuffers && g_bmpBuffers[i] && g_bmpSizes[i] > 0) {
            snprintf(assetStr, sizeof(assetStr), "[BMP]");
        } else {
            snprintf(assetStr, sizeof(assetStr), "-");
        }
        ListView_SetItemText(g_hListView, i, 4, assetStr);
    }

    if (g_stepCount > 0) {
        int sel = (prevSel >= 0 && prevSel < (int)g_stepCount) ? prevSel : 0;
        ListView_SetItemState(g_hListView, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }

    SendMessageA(g_hListView, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hListView, NULL, TRUE);
}

static void CommitInPlaceEdit(BOOL save) {
    if (!g_hInPlaceEdit) return;
    HWND hEdit = g_hInPlaceEdit;
    int item = g_InPlaceEditItem;
    g_hInPlaceEdit = NULL;
    g_InPlaceEditItem = -1;

    if (save && item >= 0 && item < (int)g_stepCount) {
        char buf[64] = {0};
        GetWindowTextA(hEdit, buf, sizeof(buf));
        DWORD newTimeout = ParseTimeoutString(buf);
        if (newTimeout > 0) {
            g_steps[item].timeoutMs = newTimeout;
            char outStr[32];
            FormatTimeoutString(newTimeout, outStr, sizeof(outStr));
            ListView_SetItemText(g_hListView, item, 3, outStr);
        }
    }
    DestroyWindow(hEdit);
    SetFocus(g_hListView);
}

static LRESULT CALLBACK InPlaceEditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_KEYDOWN:
        if (wParam == VK_RETURN) {
            CommitInPlaceEdit(TRUE);
            return 0;
        } else if (wParam == VK_ESCAPE) {
            CommitInPlaceEdit(FALSE);
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        CommitInPlaceEdit(TRUE);
        return 0;
    }
    return CallWindowProcA(g_OldEditProc, hwnd, uMsg, wParam, lParam);
}

static void StartInPlaceTimeoutEdit(int item) {
    if (g_hInPlaceEdit) {
        CommitInPlaceEdit(TRUE);
    }
    if (item < 0 || item >= (int)g_stepCount) return;
    g_InPlaceEditItem = item;

    RECT rcSub;
    ListView_GetSubItemRect(g_hListView, item, 3, LVIR_BOUNDS, &rcSub);

    char valStr[32];
    double sec = (double)g_steps[item].timeoutMs / 1000.0;
    snprintf(valStr, sizeof(valStr), "%.1f", sec);

    int editW = rcSub.right - rcSub.left;
    int editH = rcSub.bottom - rcSub.top;
    if (editH < 18) editH = 18;

    g_hInPlaceEdit = CreateWindowExA(0, "EDIT", valStr,
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        rcSub.left, rcSub.top, editW, editH,
        g_hListView, (HMENU)ID_EDIT_TIMEOUT, g_hInstance, NULL);

    if (g_hGuiFont) {
        SendMessageA(g_hInPlaceEdit, WM_SETFONT, (WPARAM)g_hGuiFont, TRUE);
    }
    g_OldEditProc = (WNDPROC)SetWindowLongPtrA(g_hInPlaceEdit, GWLP_WNDPROC, (LONG_PTR)InPlaceEditSubclassProc);
    SetFocus(g_hInPlaceEdit);
    SendMessageA(g_hInPlaceEdit, EM_SETSEL, 0, -1);
}

/* =========================================================================
 * 7. Window Layout & Collapsible Drawer
 * ========================================================================= */

static void FormatTitleRec(DWORD elapsedSec, DWORD steps, char* buf, size_t bufSize) {
    DWORD mm = elapsedSec / 60;
    DWORD ss = elapsedSec % 60;
    snprintf(buf, bufSize, "REC %02lu:%02lu (%lu steps)", (unsigned long)mm, (unsigned long)ss, (unsigned long)steps);
}

static void FormatTitlePlay(DWORD elapsedSec, DWORD stepIdx1Based, DWORD totalSteps, char* buf, size_t bufSize) {
    DWORD mm = elapsedSec / 60;
    DWORD ss = elapsedSec % 60;
    snprintf(buf, bufSize, "PLAY %02lu:%02lu (Step %lu/%lu)",
        (unsigned long)mm, (unsigned long)ss,
        (unsigned long)stepIdx1Based, (unsigned long)(totalSteps > 0 ? totalSteps : 1));
}

static void FormatTitleIdle(const char* filename, char* buf, size_t bufSize) {
    if (filename && filename[0]) {
        snprintf(buf, bufSize, "TinyTask Pro - %s", filename);
    } else {
        snprintf(buf, bufSize, "TinyTask Pro");
    }
}

static void UpdateTitle(void) {
    if (!g_hMainWnd) return;
    char title[128];
    if (g_State == STATE_RECORDING) {
        DWORD elapsed = (GetTickCount() - g_RecStartTime) / 1000;
        FormatTitleRec(elapsed, g_stepCount, title, sizeof(title));
    } else if (g_State == STATE_PLAYING) {
        DWORD elapsed = (GetTickCount() - g_PlayStartTime) / 1000;
        FormatTitlePlay(elapsed, g_CurrentPlayStep + 1, g_stepCount, title, sizeof(title));
    } else {
        const char* baseName = g_CurrentFileName[0] ? FindLastChar(g_CurrentFileName, '\\') : NULL;
        FormatTitleIdle(baseName ? baseName + 1 : (g_CurrentFileName[0] ? g_CurrentFileName : NULL), title, sizeof(title));
    }
    SetWindowTextA(g_hMainWnd, title);
}

static void SetDrawerState(BOOL expanded) {
    g_DrawerExpanded = expanded;
    int effectiveH = BUTTON_HEIGHT - g_HideCaptionsOffset;
    int clientW = expanded ? CLIENT_EXPANDED_W : CLIENT_COLLAPSED_W;
    int clientH = expanded ? CLIENT_EXPANDED_H : (effectiveH + 2 * TOOLBAR_PADDING);

    RECT rc = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

    SetWindowPos(g_hMainWnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);

    int showCmd = expanded ? SW_SHOW : SW_HIDE;
    if (g_hListView) ShowWindow(g_hListView, showCmd);
    if (g_hBtnAdd)  ShowWindow(g_hBtnAdd, showCmd);
    if (g_hBtnDel)  ShowWindow(g_hBtnDel, showCmd);
    if (g_hBtnUp)   ShowWindow(g_hBtnUp, showCmd);
    if (g_hBtnDown) ShowWindow(g_hBtnDown, showCmd);
    if (g_hBtnRun)  ShowWindow(g_hBtnRun, showCmd);

    InvalidateRect(g_hMainWnd, NULL, TRUE);
}

/* =========================================================================
 * 8. Project Storage Integration
 * ========================================================================= */

static BOOL SaveProjectFile(const char* filepath) {
    return ttp_save_project(filepath, g_steps, g_stepCount, (const BYTE**)g_bmpBuffers, g_bmpSizes);
}

static BOOL LoadProjectFile(const char* filepath) {
    TTPStep* loadedSteps = NULL;
    DWORD loadedCount = 0;
    BYTE** loadedBmps = NULL;
    DWORD* loadedSizes = NULL;

    if (ttp_load_project(filepath, &loadedSteps, &loadedCount, &loadedBmps, &loadedSizes)) {
        StepArray_Clear();
        for (DWORD i = 0; i < loadedCount; i++) {
            StepArray_Add(&loadedSteps[i], loadedBmps ? loadedBmps[i] : NULL, loadedSizes ? loadedSizes[i] : 0);
        }
        ttp_free_project(loadedSteps, loadedBmps, loadedSizes, loadedCount);
        return TRUE;
    }
    return FALSE;
}

/* =========================================================================
 * 9. Recording Engine
 * ========================================================================= */

static void CALLBACK RecTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    (void)hwnd; (void)uMsg; (void)idEvent; (void)dwTime;
    if (g_State != STATE_RECORDING) return;

    POINT pt;
    GetCursorPos(&pt);
    DWORD now = GetTickCount();

    if (pt.x != g_LastMousePos.x || pt.y != g_LastMousePos.y) {
        ttp_synth_add_mouse_event(WM_MOUSEMOVE, pt.x, pt.y, now);
        g_LastMousePos = pt;
    }

    SHORT lState = GetAsyncKeyState(VK_LBUTTON);
    BOOL lDown = (lState & 0x8000) != 0;
    BOOL prevLDown = (g_LastKeyState[VK_LBUTTON] & 0x8000) != 0;

    if (lDown && !prevLDown) {
        ttp_synth_add_mouse_event(WM_LBUTTONDOWN, pt.x, pt.y, now);

        BYTE* bmpBuf = NULL;
        DWORD bmpSize = 0;
        RECT buttonRect = {0};
        HDC hdcScreen = GetDC(NULL);
        if (ttp_adaptive_crop_button(hdcScreen, pt.x, pt.y, &buttonRect, &bmpBuf, &bmpSize)) {
            // Adaptive crop acquired
        }
        ReleaseDC(NULL, hdcScreen);

        POINT foundCenters[8];
        int count = ttp_find_elements_by_text("", foundCenters, 8);
        (void)count;

        AddRecordedClick(pt.x, pt.y, now, bmpBuf, bmpSize, "");
    } else if (!lDown && prevLDown) {
        ttp_synth_add_mouse_event(WM_LBUTTONUP, pt.x, pt.y, now);
    }
    g_LastKeyState[VK_LBUTTON] = (BYTE)(lDown ? 0x80 : 0);

    SHORT rState = GetAsyncKeyState(VK_RBUTTON);
    BOOL rDown = (rState & 0x8000) != 0;
    BOOL prevRDown = (g_LastKeyState[VK_RBUTTON] & 0x8000) != 0;

    if (rDown && !prevRDown) {
        ttp_synth_add_mouse_event(WM_RBUTTONDOWN, pt.x, pt.y, now);
    } else if (!rDown && prevRDown) {
        ttp_synth_add_mouse_event(WM_RBUTTONUP, pt.x, pt.y, now);
    }
    g_LastKeyState[VK_RBUTTON] = (BYTE)(rDown ? 0x80 : 0);

    for (int vk = 8; vk < 256; vk++) {
        if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_CANCEL) continue;
        SHORT ks = GetAsyncKeyState(vk);
        BOOL isDown = (ks & 0x8000) != 0;
        BOOL wasDown = (g_LastKeyState[vk] & 0x8000) != 0;

        if (isDown != wasDown) {
            ttp_synth_add_key_event((DWORD)vk, isDown, now);
            g_LastKeyState[vk] = (BYTE)(isDown ? 0x80 : 0);
        }
    }
    UpdateTitle();
}

static void StartRecording(void) {
    if (g_State == STATE_PLAYING) StopPlayback();
    StepArray_Clear();
    ClearRecordedClicks();
    ttp_synth_init();

    GetCursorPos(&g_LastMousePos);
    memset(g_LastKeyState, 0, sizeof(g_LastKeyState));

    g_RecStartTime = GetTickCount();
    g_State = STATE_RECORDING;
    SetTimer(g_hMainWnd, TIMER_REC, 10, RecTimerProc);
    InvalidateRect(g_hMainWnd, NULL, FALSE);
    UpdateTitle();
}

static void StopRecording(void) {
    KillTimer(g_hMainWnd, TIMER_REC);
    g_State = STATE_IDLE;

    TTPStep tempSteps[128];
    DWORD finalizedCount = ttp_synth_finalize(tempSteps, 128);

    for (DWORD i = 0; i < finalizedCount; i++) {
        if (tempSteps[i].timeoutMs == 0) {
            tempSteps[i].timeoutMs = g_DefaultTimeoutSec * 1000;
        }

        double bestDist = 999999.0;
        int bestClickIdx = -1;
        for (DWORD c = 0; c < g_recClickCount; c++) {
            double d = ttp_calc_euclidean_dist(tempSteps[i].origX, tempSteps[i].origY, g_recClicks[c].x, g_recClicks[c].y);
            if (d < bestDist) {
                bestDist = d;
                bestClickIdx = (int)c;
            }
        }

        if (bestClickIdx >= 0 && bestDist < 100.0) {
            RecordedClick* rc = &g_recClicks[bestClickIdx];
            if (rc->text[0] != '\0') {
                tempSteps[i].targetMode = TTP_TARGET_TEXT;
                strncpy(tempSteps[i].textKey, rc->text, sizeof(tempSteps[i].textKey) - 1);
            } else if (rc->bmpData != NULL && rc->bmpSize > 0) {
                tempSteps[i].targetMode = TTP_TARGET_IMAGE;
            } else {
                tempSteps[i].targetMode = TTP_TARGET_COORD;
            }
            StepArray_Add(&tempSteps[i], rc->bmpData, rc->bmpSize);
        } else {
            tempSteps[i].targetMode = TTP_TARGET_COORD;
            StepArray_Add(&tempSteps[i], NULL, 0);
        }
    }

    ClearRecordedClicks();
    RefreshListView();
    InvalidateRect(g_hMainWnd, NULL, FALSE);
    UpdateTitle();
}

/* =========================================================================
 * 10. Playback Engine
 * ========================================================================= */

static DWORD WINAPI PlaybackThreadProc(LPVOID lpParam) {
    (void)lpParam;
    DWORD loop = 0;
    while (!g_StopPlaybackRequested) {
        for (DWORD i = 0; i < g_stepCount && !g_StopPlaybackRequested; i++) {
            g_CurrentPlayStep = i;
            g_CurrentPlayLoop = loop;
            PostMessageA(g_hMainWnd, WM_USER_PLAY_UPDATE, (WPARAM)i, (LPARAM)loop);

            TTPStep step = g_steps[i];
            if (g_SpeedMode == SPEED_HALF) {
                step.postDelayMs = (DWORD)(step.postDelayMs * 2.0);
            } else if (g_SpeedMode == SPEED_2X) {
                step.postDelayMs = (DWORD)(step.postDelayMs * 0.5);
            } else if (g_SpeedMode == SPEED_100X) {
                step.postDelayMs = (DWORD)(step.postDelayMs / 100);
            } else if (g_SpeedMode == SPEED_CUSTOM && g_CustomSpeed > 0) {
                step.postDelayMs = (DWORD)(step.postDelayMs / g_CustomSpeed);
            }
            if (step.postDelayMs < 1) step.postDelayMs = 1;

            BOOL ok = ttp_playback_step(&step, g_bmpBuffers[i], g_bmpSizes[i], g_hMainWnd);
            if (!ok || g_StopPlaybackRequested) {
                goto thread_end;
            }
        }
        loop++;
        if (!g_Continuous && loop >= g_PlayLoopTotal) {
            break;
        }
    }
thread_end:
    PostMessageA(g_hMainWnd, WM_USER_PLAY_DONE, 0, 0);
    return 0;
}

static void StartPlayback(void) {
    if (g_stepCount == 0) {
        MessageBoxA(g_hMainWnd, "Nothing recorded or loaded.\nRecord some steps or open a .ttp file first.", "TinyTask Pro", MB_ICONINFORMATION);
        return;
    }
    if (g_State == STATE_RECORDING) StopRecording();

    g_State = STATE_PLAYING;
    g_StopPlaybackRequested = FALSE;
    g_PlayStartTime = GetTickCount();
    g_CurrentPlayStep = 0;
    g_CurrentPlayLoop = 0;
    InvalidateRect(g_hMainWnd, NULL, FALSE);
    UpdateTitle();

    g_hPlayThread = CreateThread(NULL, 0, PlaybackThreadProc, NULL, 0, NULL);
}

static void StopPlayback(void) {
    g_StopPlaybackRequested = TRUE;
    if (g_hPlayThread) {
        WaitForSingleObject(g_hPlayThread, 1000);
        CloseHandle(g_hPlayThread);
        g_hPlayThread = NULL;
    }
    g_State = STATE_IDLE;
    InvalidateRect(g_hMainWnd, NULL, FALSE);
    UpdateTitle();
}

static void CALLBACK HotkeyTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    (void)hwnd; (void)uMsg; (void)idEvent; (void)dwTime;
    if (IsHotkeyTriggered(&g_RecHotkey, 'R')) {
        Sleep(150);
        PostMessageA(g_hMainWnd, WM_COMMAND, ID_PRO_REC, 0);
        return;
    }
    if (IsHotkeyTriggered(&g_PlayHotkey, 'P')) {
        Sleep(150);
        PostMessageA(g_hMainWnd, WM_COMMAND, ID_PRO_PLAY, 0);
        return;
    }
    if (g_State == STATE_PLAYING) {
        if ((GetAsyncKeyState(VK_PAUSE) & 0x8000) || (GetAsyncKeyState(VK_SCROLL) & 0x8000)) {
            StopPlayback();
        }
    }
}

/* =========================================================================
 * 11. Options Menu (Full Preferences Matching TinyTask + Pro Extensions)
 * ========================================================================= */

static void ShowOptionsMenu(HWND hwnd, int x, int y) {
    HMENU hMenu = CreatePopupMenu();
    HMENU hRecHot = CreatePopupMenu();
    HMENU hPlayHot = CreatePopupMenu();

    /* Speed options */
    AppendMenuA(hMenu, MF_STRING | (g_SpeedMode == SPEED_HALF ? MF_CHECKED : 0), ID_OPT_SPEED_HALF, "Play Speed:   \xbd");
    AppendMenuA(hMenu, MF_STRING | (g_SpeedMode == SPEED_1X ? MF_CHECKED : 0), ID_OPT_SPEED_1X, "Play Speed:   &1x");
    AppendMenuA(hMenu, MF_STRING | (g_SpeedMode == SPEED_2X ? MF_CHECKED : 0), ID_OPT_SPEED_2X, "Play Speed:   &2x");
    AppendMenuA(hMenu, MF_STRING | (g_SpeedMode == SPEED_100X ? MF_CHECKED : 0), ID_OPT_SPEED_100X, "Play Speed:   100x");

    char customSpeedStr[64];
    wsprintfA(customSpeedStr, "&Play Custom Speed:  %dx", g_CustomSpeed);
    AppendMenuA(hMenu, MF_STRING | (g_SpeedMode == SPEED_CUSTOM ? MF_CHECKED : 0), ID_OPT_SPEED_CUSTOM, customSpeedStr);
    AppendMenuA(hMenu, MF_STRING, ID_OPT_SET_SPEED, "&Set Custom Speed...");

    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING | (g_Continuous ? MF_CHECKED : 0), ID_OPT_CONT, "&Continuous Playback");

    char loopStr[64];
    wsprintfA(loopStr, "&Set Playback Loops...  (%lu)", (unsigned long)g_PlayLoopTotal);
    AppendMenuA(hMenu, MF_STRING, ID_OPT_SET_LOOPS, loopStr);

    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);

    /* Recording Hotkey submenu */
    AppendMenuA(hRecHot, MF_STRING | (g_RecHotkey.mode == 0 ? MF_CHECKED : 0), ID_REC_HOTKEY_STD, "Control + Shift + Alt + R");
    AppendMenuA(hRecHot, MF_STRING | (g_RecHotkey.mode == 1 ? MF_CHECKED : 0), ID_REC_HOTKEY_PRTSC, "Print Screen");
    AppendMenuA(hRecHot, MF_STRING | (g_RecHotkey.mode == 8 ? MF_CHECKED : 0), ID_REC_HOTKEY_F8, "F8");
    AppendMenuA(hRecHot, MF_STRING | (g_RecHotkey.mode == 12 ? MF_CHECKED : 0), ID_REC_HOTKEY_F12, "F12");
    if (g_RecHotkey.customSet) {
        char customStr[80];
        wsprintfA(customStr, "Custom:  %s", g_RecHotkey.customName);
        AppendMenuA(hRecHot, MF_STRING | (g_RecHotkey.mode == 99 ? MF_CHECKED : 0), ID_REC_HOTKEY_CUSTOM_ACTIVE, customStr);
    }
    AppendMenuA(hRecHot, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hRecHot, MF_STRING, ID_REC_HOTKEY_CUSTOM_SET, "&Set Custom Hotkey...");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hRecHot, "Recording &Hotkey");

    /* Playback Hotkey submenu */
    AppendMenuA(hPlayHot, MF_STRING | (g_PlayHotkey.mode == 0 ? MF_CHECKED : 0), ID_PLAY_HOTKEY_STD, "Control + Shift + Alt + P");
    AppendMenuA(hPlayHot, MF_STRING | (g_PlayHotkey.mode == 1 ? MF_CHECKED : 0), ID_PLAY_HOTKEY_PRTSC, "Print Screen");
    AppendMenuA(hPlayHot, MF_STRING | (g_PlayHotkey.mode == 8 ? MF_CHECKED : 0), ID_PLAY_HOTKEY_F8, "F8");
    AppendMenuA(hPlayHot, MF_STRING | (g_PlayHotkey.mode == 12 ? MF_CHECKED : 0), ID_PLAY_HOTKEY_F12, "F12");
    if (g_PlayHotkey.customSet) {
        char customStr[80];
        wsprintfA(customStr, "Custom:  %s", g_PlayHotkey.customName);
        AppendMenuA(hPlayHot, MF_STRING | (g_PlayHotkey.mode == 99 ? MF_CHECKED : 0), ID_PLAY_HOTKEY_CUSTOM_ACTIVE, customStr);
    }
    AppendMenuA(hPlayHot, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hPlayHot, MF_STRING, ID_PLAY_HOTKEY_CUSTOM_SET, "&Set Custom Hotkey...");
    AppendMenuA(hPlayHot, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hPlayHot, MF_STRING | MF_GRAYED, 0, "\x95 Hint:  Press {PAUSE} or {ScrollLock} to stop playbacks");
    AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hPlayHot, "Playback Hot&key");

    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING | (g_AlwaysOnTop ? MF_CHECKED : 0), ID_OPT_TOPMOST, "Always on &Top");
    AppendMenuA(hMenu, MF_STRING | (g_HideCaptionsOffset == 0 ? MF_CHECKED : 0), ID_OPT_SHOW_CAPTIONS, "Show Captions");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, ID_OPT_TOOLBAR_CUSTOM, "Use Custom Tool&bar...");
    AppendMenuA(hMenu, MF_STRING, ID_OPT_TOOLBAR_DEFAULT, "Use &Default Toolbar");

    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    char timeoutMenuStr[64];
    wsprintfA(timeoutMenuStr, "Set Default Step &Timeout...  (%lu s)", (unsigned long)g_DefaultTimeoutSec);
    AppendMenuA(hMenu, MF_STRING, ID_OPT_DEFAULT_TIMEOUT, timeoutMenuStr);

    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, ID_OPT_WEBSITE, "TinyTask &Website");
    AppendMenuA(hMenu, MF_STRING, ID_OPT_ABOUT, "&About TinyTask Pro...");

    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, x, y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

/* =========================================================================
 * 12. Main Window Procedure
 * ========================================================================= */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        g_hMainWnd = hwnd;
        g_hGuiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        LoadConfig();
        CreateToolbarBitmaps();

        /* Collapsible SysListView32 child control */
        g_hListView = CreateWindowExA(WS_EX_CLIENTEDGE, WC_LISTVIEWA, "",
            WS_CHILD | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER | WS_VSCROLL,
            10, 56, 360, 255, hwnd, (HMENU)ID_LV_STEPS, g_hInstance, NULL);

        ListView_SetExtendedListViewStyle(g_hListView, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        SendMessageA(g_hListView, WM_SETFONT, (WPARAM)g_hGuiFont, TRUE);

        /* Add columns: #, Action, Target / Text, Timeout(s), Asset */
        LVCOLUMNA lvc;
        memset(&lvc, 0, sizeof(lvc));
        lvc.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

        lvc.iSubItem = 0; lvc.cx = 32;  lvc.pszText = "#";            ListView_InsertColumn(g_hListView, 0, &lvc);
        lvc.iSubItem = 1; lvc.cx = 65;  lvc.pszText = "Action";       ListView_InsertColumn(g_hListView, 1, &lvc);
        lvc.iSubItem = 2; lvc.cx = 125; lvc.pszText = "Target / Text"; ListView_InsertColumn(g_hListView, 2, &lvc);
        lvc.iSubItem = 3; lvc.cx = 75;  lvc.pszText = "Timeout(s)";   ListView_InsertColumn(g_hListView, 3, &lvc);
        lvc.iSubItem = 4; lvc.cx = 60;  lvc.pszText = "Asset";        ListView_InsertColumn(g_hListView, 4, &lvc);

        /* Drawer Quick Action buttons */
        g_hBtnAdd = CreateWindowExA(0, "BUTTON", "[+ Add]", WS_CHILD | BS_PUSHBUTTON,
            10, 322, 55, 26, hwnd, (HMENU)ID_BTN_ADD_STEP, g_hInstance, NULL);
        SendMessageA(g_hBtnAdd, WM_SETFONT, (WPARAM)g_hGuiFont, TRUE);

        g_hBtnDel = CreateWindowExA(0, "BUTTON", "[\xc3\x97 Del]", WS_CHILD | BS_PUSHBUTTON,
            70, 322, 55, 26, hwnd, (HMENU)ID_BTN_DEL_STEP, g_hInstance, NULL);
        SendMessageA(g_hBtnDel, WM_SETFONT, (WPARAM)g_hGuiFont, TRUE);

        g_hBtnUp = CreateWindowExA(0, "BUTTON", "[\xe2\x96\xb2 Up]", WS_CHILD | BS_PUSHBUTTON,
            130, 322, 52, 26, hwnd, (HMENU)ID_BTN_MOVE_UP, g_hInstance, NULL);
        SendMessageA(g_hBtnUp, WM_SETFONT, (WPARAM)g_hGuiFont, TRUE);

        g_hBtnDown = CreateWindowExA(0, "BUTTON", "[\xe2\x96\xbc Dn]", WS_CHILD | BS_PUSHBUTTON,
            187, 322, 52, 26, hwnd, (HMENU)ID_BTN_MOVE_DOWN, g_hInstance, NULL);
        SendMessageA(g_hBtnDown, WM_SETFONT, (WPARAM)g_hGuiFont, TRUE);

        g_hBtnRun = CreateWindowExA(0, "BUTTON", "[Step Run \xe2\x96\xb6]", WS_CHILD | BS_PUSHBUTTON,
            244, 322, 126, 26, hwnd, (HMENU)ID_BTN_STEP_RUN, g_hInstance, NULL);
        SendMessageA(g_hBtnRun, WM_SETFONT, (WPARAM)g_hGuiFont, TRUE);

        SetTimer(hwnd, TIMER_HOTKEY, 50, HotkeyTimerProc);
        UpdateTitle();
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rcClient;
        GetClientRect(hwnd, &rcClient);
        HBRUSH hbrBg = GetSysColorBrush(COLOR_BTNFACE);
        FillRect(hdc, &rcClient, hbrBg);

        int effectiveH = BUTTON_HEIGHT - g_HideCaptionsOffset;

        if (g_hBmpToolbar && g_hBmpMask) {
            HDC hdcMem = CreateCompatibleDC(hdc);

            for (int i = 0; i < NUM_BUTTONS; i++) {
                int frame = i;
                if (i == 2 && g_State == STATE_RECORDING) {
                    frame = 6; /* Stop square */
                } else if (i == 3 && g_State == STATE_PLAYING) {
                    frame = 6; /* Stop square */
                }

                int src_x = frame * BUTTON_WIDTH;
                int src_y = 0;
                int dst_x = TOOLBAR_PADDING + i * (BUTTON_WIDTH + TOOLBAR_PADDING);
                int dst_y = TOOLBAR_PADDING;

                HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, g_hBmpMask);
                BitBlt(hdc, dst_x, dst_y, BUTTON_WIDTH, effectiveH, hdcMem, src_x, src_y, SRCAND);

                SelectObject(hdcMem, g_hBmpToolbar);
                BitBlt(hdc, dst_x, dst_y, BUTTON_WIDTH, effectiveH, hdcMem, src_x, src_y, SRCPAINT);

                SelectObject(hdcMem, hOld);
            }
            DeleteDC(hdcMem);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            int effectiveH = BUTTON_HEIGHT - g_HideCaptionsOffset;
            if (pt.y >= TOOLBAR_PADDING && pt.y < TOOLBAR_PADDING + effectiveH &&
                pt.x >= TOOLBAR_PADDING && pt.x < TOOLBAR_PADDING + NUM_BUTTONS * (BUTTON_WIDTH + TOOLBAR_PADDING)) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
            SetCursor(LoadCursor(NULL, IDC_ARROW));
            return TRUE;
        }
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }

    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        int effectiveH = BUTTON_HEIGHT - g_HideCaptionsOffset;

        if (y >= TOOLBAR_PADDING && y < TOOLBAR_PADDING + effectiveH) {
            for (int i = 0; i < NUM_BUTTONS; i++) {
                int bx = TOOLBAR_PADDING + i * (BUTTON_WIDTH + TOOLBAR_PADDING);
                if (x >= bx && x < bx + BUTTON_WIDTH) {
                    SendMessageA(hwnd, WM_COMMAND, ID_PRO_OPEN + i, 0);
                    break;
                }
            }
        }
        return 0;
    }

    case WM_NOTIFY: {
        LPNMHDR pnmh = (LPNMHDR)lParam;
        if (pnmh->idFrom == ID_LV_STEPS && pnmh->code == NM_DBLCLK) {
            LPNMITEMACTIVATE pia = (LPNMITEMACTIVATE)lParam;
            if (pia->iItem >= 0 && pia->iItem < (int)g_stepCount && pia->iSubItem == 3) {
                StartInPlaceTimeoutEdit(pia->iItem);
                return 0;
            }
        }
        break;
    }

    case WM_COMMAND: {
        WORD cmd = LOWORD(wParam);
        switch (cmd) {
        case ID_PRO_OPEN: {
            char path[MAX_PATH] = "";
            OPENFILENAMEA ofn = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "TinyTask Files (*.ttp;*.rec)\0*.ttp;*.rec\0TinyTask Pro (*.ttp)\0*.ttp\0TinyTask Rec (*.rec)\0*.rec\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                if (LoadProjectFile(path)) {
                    strncpy(g_CurrentFileName, path, MAX_PATH - 1);
                    RefreshListView();
                    UpdateTitle();
                } else {
                    MessageBoxA(hwnd, "Failed to load project file.", "TinyTask Pro", MB_ICONERROR);
                }
            }
            break;
        }

        case ID_PRO_SAVE: {
            char path[MAX_PATH] = "";
            if (g_CurrentFileName[0]) {
                strncpy(path, g_CurrentFileName, MAX_PATH - 1);
            } else {
                strcpy(path, "macro.ttp");
            }
            OPENFILENAMEA ofn = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "TinyTask Pro (*.ttp)\0*.ttp\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.lpstrDefExt = "ttp";
            ofn.Flags = OFN_OVERWRITEPROMPT;
            if (GetSaveFileNameA(&ofn)) {
                if (SaveProjectFile(path)) {
                    strncpy(g_CurrentFileName, path, MAX_PATH - 1);
                    UpdateTitle();
                } else {
                    MessageBoxA(hwnd, "Failed to save project file.", "TinyTask Pro", MB_ICONERROR);
                }
            }
            break;
        }

        case ID_PRO_REC: {
            if (g_State == STATE_RECORDING) {
                StopRecording();
            } else {
                StartRecording();
            }
            break;
        }

        case ID_PRO_PLAY: {
            if (g_State == STATE_PLAYING) {
                StopPlayback();
            } else {
                StartPlayback();
            }
            break;
        }

        case ID_PRO_STEPS: {
            SetDrawerState(!g_DrawerExpanded);
            break;
        }

        case ID_PRO_OPTIONS: {
            int effectiveH = BUTTON_HEIGHT - g_HideCaptionsOffset;
            POINT pt = { TOOLBAR_PADDING + 5 * (BUTTON_WIDTH + TOOLBAR_PADDING), TOOLBAR_PADDING + effectiveH };
            ClientToScreen(hwnd, &pt);
            ShowOptionsMenu(hwnd, pt.x, pt.y);
            break;
        }

        case ID_OPT_SPEED_HALF: {
            g_SpeedMode = SPEED_HALF;
            SaveConfig();
            break;
        }
        case ID_OPT_SPEED_1X: {
            g_SpeedMode = SPEED_1X;
            SaveConfig();
            break;
        }
        case ID_OPT_SPEED_2X: {
            g_SpeedMode = SPEED_2X;
            SaveConfig();
            break;
        }
        case ID_OPT_SPEED_100X: {
            g_SpeedMode = SPEED_100X;
            SaveConfig();
            break;
        }
        case ID_OPT_SPEED_CUSTOM: {
            g_SpeedMode = SPEED_CUSTOM;
            SaveConfig();
            break;
        }
        case ID_OPT_SET_SPEED: {
            int val = PromptNumber(hwnd, "Playback Speed", "Enter playback speed multiplier (1x - 100x):", g_CustomSpeed, 1, 100);
            if (val >= 1 && val <= 100) {
                g_CustomSpeed = val;
                g_SpeedMode = SPEED_CUSTOM;
                SaveConfig();
            }
            break;
        }

        case ID_OPT_CONT: {
            g_Continuous = !g_Continuous;
            SaveConfig();
            break;
        }
        case ID_OPT_SET_LOOPS: {
            int loops = PromptNumber(hwnd, "Playback Loops", "Enter number of playback loops:", g_PlayLoopTotal, 1, 999999);
            if (loops >= 1) {
                g_PlayLoopTotal = loops;
                SaveConfig();
            }
            break;
        }

        /* Hotkey preset selections */
        case ID_REC_HOTKEY_STD:   SetPresetHotkey(hwnd, &g_RecHotkey, 0, 0, TRUE); break;
        case ID_REC_HOTKEY_PRTSC: SetPresetHotkey(hwnd, &g_RecHotkey, 1, VK_SNAPSHOT, TRUE); break;
        case ID_REC_HOTKEY_F8:    SetPresetHotkey(hwnd, &g_RecHotkey, 8, VK_F8, TRUE); break;
        case ID_REC_HOTKEY_F12:   SetPresetHotkey(hwnd, &g_RecHotkey, 12, VK_F12, TRUE); break;
        case ID_REC_HOTKEY_CUSTOM_ACTIVE: {
            if (g_RecHotkey.customSet) {
                g_RecHotkey.mode = 99;
                SaveConfig();
            }
            break;
        }
        case ID_REC_HOTKEY_CUSTOM_SET: {
            HotkeyState tempHk = g_RecHotkey;
            if (CaptureHotkey(hwnd, "Set Recording Hotkey", &tempHk)) {
                if (!CheckHotkeyConflict(hwnd, 99, tempHk.customVk, tempHk.customMod, TRUE)) {
                    g_RecHotkey = tempHk;
                    SaveConfig();
                }
            }
            break;
        }

        case ID_PLAY_HOTKEY_STD:   SetPresetHotkey(hwnd, &g_PlayHotkey, 0, 0, FALSE); break;
        case ID_PLAY_HOTKEY_PRTSC: SetPresetHotkey(hwnd, &g_PlayHotkey, 1, VK_SNAPSHOT, FALSE); break;
        case ID_PLAY_HOTKEY_F8:    SetPresetHotkey(hwnd, &g_PlayHotkey, 8, VK_F8, FALSE); break;
        case ID_PLAY_HOTKEY_F12:   SetPresetHotkey(hwnd, &g_PlayHotkey, 12, VK_F12, FALSE); break;
        case ID_PLAY_HOTKEY_CUSTOM_ACTIVE: {
            if (g_PlayHotkey.customSet) {
                g_PlayHotkey.mode = 99;
                SaveConfig();
            }
            break;
        }
        case ID_PLAY_HOTKEY_CUSTOM_SET: {
            HotkeyState tempHk = g_PlayHotkey;
            if (CaptureHotkey(hwnd, "Set Playback Hotkey", &tempHk)) {
                if (!CheckHotkeyConflict(hwnd, 99, tempHk.customVk, tempHk.customMod, FALSE)) {
                    g_PlayHotkey = tempHk;
                    SaveConfig();
                }
            }
            break;
        }

        case ID_OPT_TOPMOST: {
            g_AlwaysOnTop = !g_AlwaysOnTop;
            SetWindowPos(hwnd, g_AlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SaveConfig();
            break;
        }

        case ID_OPT_SHOW_CAPTIONS: {
            g_HideCaptionsOffset = (g_HideCaptionsOffset == 0) ? 12 : 0;
            SetDrawerState(g_DrawerExpanded);
            SaveConfig();
            break;
        }

        case ID_OPT_TOOLBAR_CUSTOM: {
            char path[MAX_PATH] = "";
            OPENFILENAMEA ofn = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "Bitmap Files (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = path;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                strncpy(g_CustomToolbarPath, path, MAX_PATH - 1);
                g_HasCustomToolbar = TRUE;
                CreateToolbarBitmaps();
                InvalidateRect(hwnd, NULL, FALSE);
                SaveConfig();
            }
            break;
        }

        case ID_OPT_TOOLBAR_DEFAULT: {
            g_HasCustomToolbar = FALSE;
            g_CustomToolbarPath[0] = '\0';
            CreateToolbarBitmaps();
            InvalidateRect(hwnd, NULL, FALSE);
            SaveConfig();
            break;
        }

        case ID_OPT_DEFAULT_TIMEOUT: {
            int toSec = PromptNumber(hwnd, "Default Step Timeout", "Enter default step search timeout in seconds:", g_DefaultTimeoutSec, 1, 60);
            if (toSec >= 1 && toSec <= 60) {
                g_DefaultTimeoutSec = toSec;
                SaveConfig();
            }
            break;
        }

        case ID_OPT_WEBSITE: {
            ShellExecuteA(NULL, "open", "https://tinytask.net", NULL, NULL, SW_SHOWNORMAL);
            break;
        }

        case ID_OPT_ABOUT: {
            MessageBoxA(hwnd,
                "TinyTask Pro 1.0 (Win32 Native)\n\n"
                "Next-Gen Ultra-Lightweight Macro Automation (< 100KB)\n"
                "Inspired by Microsoft Power Automate with Computer Vision & Text Anchors.\n\n"
                "Key Capabilities:\n"
                "* Pure-C Adaptive Edge Detection & Button Cropping\n"
                "* Dual-Track Recognition (Win32 UIA Text + NCC Template Matching)\n"
                "* Euclidean Nearest-Neighbor Disambiguation for Multiple Matches\n"
                "* Collapsible Workflow Steps Drawer (SysListView32)\n"
                "* Per-Step Configurable Timeouts with Retry/Fallback Dialog\n"
                "* Self-Contained Project Packaging (.ttp)\n\n"
                "(C) TinyTask Pro Open Architecture",
                "About TinyTask Pro", MB_ICONINFORMATION);
            break;
        }

        case ID_BTN_ADD_STEP: {
            TTPStep s;
            memset(&s, 0, sizeof(s));
            s.actionType = TTP_ACTION_CLICK;
            s.targetMode = TTP_TARGET_COORD;
            s.origX = 100;
            s.origY = 100;
            s.timeoutMs = g_DefaultTimeoutSec * 1000;
            s.postDelayMs = 100;
            StepArray_Add(&s, NULL, 0);
            RefreshListView();
            ListView_SetItemState(g_hListView, g_stepCount - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(g_hListView, g_stepCount - 1, FALSE);
            break;
        }

        case ID_BTN_DEL_STEP: {
            int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < (int)g_stepCount) {
                StepArray_Delete(sel);
                RefreshListView();
                if (sel >= (int)g_stepCount) sel = (int)g_stepCount - 1;
                if (sel >= 0) {
                    ListView_SetItemState(g_hListView, sel, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                }
            }
            break;
        }

        case ID_BTN_MOVE_UP: {
            int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (sel > 0 && sel < (int)g_stepCount) {
                StepArray_MoveUp(sel);
                RefreshListView();
                ListView_SetItemState(g_hListView, sel - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(g_hListView, sel - 1, FALSE);
            }
            break;
        }

        case ID_BTN_MOVE_DOWN: {
            int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (sel >= 0 && sel + 1 < (int)g_stepCount) {
                StepArray_MoveDown(sel);
                RefreshListView();
                ListView_SetItemState(g_hListView, sel + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(g_hListView, sel + 1, FALSE);
            }
            break;
        }

        case ID_BTN_STEP_RUN: {
            int sel = ListView_GetNextItem(g_hListView, -1, LVNI_SELECTED);
            if (sel >= 0 && sel < (int)g_stepCount) {
                ttp_playback_step(&g_steps[sel], g_bmpBuffers[sel], g_bmpSizes[sel], hwnd);
            }
            break;
        }
        }
        return 0;
    }

    case WM_USER_PLAY_UPDATE: {
        DWORD curStep = (DWORD)wParam;
        DWORD curLoop = (DWORD)lParam;
        (void)curLoop;
        UpdateTitle();
        if (g_DrawerExpanded && g_hListView && curStep < g_stepCount) {
            ListView_SetItemState(g_hListView, curStep, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(g_hListView, curStep, FALSE);
        }
        return 0;
    }

    case WM_USER_PLAY_DONE: {
        if (g_hPlayThread) {
            CloseHandle(g_hPlayThread);
            g_hPlayThread = NULL;
        }
        g_State = STATE_IDLE;
        InvalidateRect(hwnd, NULL, FALSE);
        UpdateTitle();
        return 0;
    }

    case WM_MOVE: {
        if (!IsIconic(hwnd)) {
            RECT rc;
            GetWindowRect(hwnd, &rc);
            g_WindowX = rc.left;
            g_WindowY = rc.top;
        }
        return 0;
    }

    case WM_DESTROY: {
        SaveConfig();
        KillTimer(hwnd, TIMER_HOTKEY);
        if (g_State == STATE_RECORDING) StopRecording();
        if (g_State == STATE_PLAYING) StopPlayback();
        StepArray_Clear();
        ClearRecordedClicks();
        if (g_hBmpToolbar) { DeleteObject(g_hBmpToolbar); g_hBmpToolbar = NULL; }
        if (g_hBmpMask) { DeleteObject(g_hBmpMask); g_hBmpMask = NULL; }
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProcA(hwnd, uMsg, wParam, lParam);
}

/* =========================================================================
 * 13. Application Entry Point
 * ========================================================================= */

#ifndef TTP_TEST_MODE
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance; (void)lpCmdLine;
    g_hInstance = hInstance;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(4001));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "TinyTaskProClass";
    wc.hIconSm = LoadIconA(hInstance, MAKEINTRESOURCEA(4001));

    if (!RegisterClassExA(&wc)) {
        return 1;
    }

    LoadConfig();

    int effectiveH = BUTTON_HEIGHT - g_HideCaptionsOffset;
    RECT rc = { 0, 0, CLIENT_COLLAPSED_W, effectiveH + 2 * TOOLBAR_PADDING };
    AdjustWindowRectEx(&rc, WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0);

    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int x = (g_WindowX != -9999) ? g_WindowX : (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (g_WindowY != -9999) ? g_WindowY : (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    HWND hwnd = CreateWindowExA(
        g_AlwaysOnTop ? WS_EX_TOPMOST : 0,
        "TinyTaskProClass", "TinyTask Pro",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        x, y, w, h,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
#endif
