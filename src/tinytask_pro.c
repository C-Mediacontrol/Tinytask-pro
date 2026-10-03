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

/* Options menu IDs */
#define ID_OPT_SPEED_HALF 0x9200
#define ID_OPT_SPEED_1X   0x9201
#define ID_OPT_SPEED_2X   0x9202
#define ID_OPT_CONT       0x9203
#define ID_OPT_TOPMOST    0x9204
#define ID_OPT_ABOUT      0x9205

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

/* Application State */
static int  g_State = STATE_IDLE;
static BOOL g_DrawerExpanded = FALSE;
static BOOL g_AlwaysOnTop = FALSE;
static BOOL g_Continuous = FALSE;
static int  g_SpeedMode = 1; /* 0 = 0.5x, 1 = 1x, 2 = 2x */
static char g_CurrentFileName[MAX_PATH] = "";
static DWORD g_RecStartTime = 0;
static DWORD g_PlayStartTime = 0;

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
    DWORD idx = g_stepCount;
    memcpy(&g_steps[idx], step, sizeof(TTPStep));
    g_steps[idx].stepId = idx + 1;

    if (bmpData && bmpSize > 0) {
        g_bmpBuffers[idx] = (BYTE*)malloc(bmpSize);
        if (g_bmpBuffers[idx]) {
            memcpy(g_bmpBuffers[idx], bmpData, bmpSize);
            g_bmpSizes[idx] = bmpSize;
            g_steps[idx].imageSize = bmpSize;
        } else {
            g_bmpBuffers[idx] = NULL;
            g_bmpSizes[idx] = 0;
            g_steps[idx].imageSize = 0;
        }
    } else {
        g_bmpBuffers[idx] = NULL;
        g_bmpSizes[idx] = 0;
        g_steps[idx].imageSize = 0;
    }
    g_stepCount++;
    return TRUE;
}

static BOOL StepArray_MoveUp(DWORD index) {
    if (index == 0 || index >= g_stepCount) return FALSE;
    DWORD prev = index - 1;

    TTPStep tempStep = g_steps[index];
    BYTE* tempBmp = g_bmpBuffers[index];
    DWORD tempSz = g_bmpSizes[index];

    g_steps[index] = g_steps[prev];
    g_bmpBuffers[index] = g_bmpBuffers[prev];
    g_bmpSizes[index] = g_bmpSizes[prev];

    g_steps[prev] = tempStep;
    g_bmpBuffers[prev] = tempBmp;
    g_bmpSizes[prev] = tempSz;

    g_steps[prev].stepId = prev + 1;
    g_steps[index].stepId = index + 1;
    return TRUE;
}

static BOOL StepArray_MoveDown(DWORD index) {
    if (index + 1 >= g_stepCount) return FALSE;
    DWORD next = index + 1;

    TTPStep tempStep = g_steps[index];
    BYTE* tempBmp = g_bmpBuffers[index];
    DWORD tempSz = g_bmpSizes[index];

    g_steps[index] = g_steps[next];
    g_bmpBuffers[index] = g_bmpBuffers[next];
    g_bmpSizes[index] = g_bmpSizes[next];

    g_steps[next] = tempStep;
    g_bmpBuffers[next] = tempBmp;
    g_bmpSizes[next] = tempSz;

    g_steps[index].stepId = index + 1;
    g_steps[next].stepId = next + 1;
    return TRUE;
}

static BOOL StepArray_Delete(DWORD index) {
    if (index >= g_stepCount) return FALSE;
    if (g_bmpBuffers[index]) {
        free(g_bmpBuffers[index]);
        g_bmpBuffers[index] = NULL;
    }
    for (DWORD i = index; i + 1 < g_stepCount; i++) {
        g_steps[i] = g_steps[i + 1];
        g_bmpBuffers[i] = g_bmpBuffers[i + 1];
        g_bmpSizes[i] = g_bmpSizes[i + 1];
        g_steps[i].stepId = i + 1;
    }
    g_stepCount--;
    if (g_stepCount < g_stepCap) {
        g_bmpBuffers[g_stepCount] = NULL;
        g_bmpSizes[g_stepCount] = 0;
    }
    return TRUE;
}

/* =========================================================================
 * 2. Formatting & Parsing Helpers
 * ========================================================================= */

static DWORD ParseTimeoutString(const char* str) {
    if (!str || !*str) return 3000;
    while (*str == ' ' || *str == '\t') str++;
    double val = atof(str);
    if (val <= 0.0) return 3000;
    /* User enters seconds (e.g. 5.0 -> 5000ms), min 0.1s (100ms), max 3600s */
    if (val < 0.1) val = 0.1;
    if (val > 3600.0) val = 3600.0;
    return (DWORD)(val * 1000.0 + 0.5);
}

static void FormatTimeoutString(DWORD ms, char* out, int maxLen) {
    double sec = (double)ms / 1000.0;
    snprintf(out, maxLen, "%.1fs", sec);
}

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

static void GetTargetDescription(const TTPStep* step, char* out, int maxLen) {
    if (!step || !out || maxLen <= 0) return;
    if (step->targetMode == TTP_TARGET_TEXT) {
        snprintf(out, maxLen, "Text: \"%s\"", step->textKey);
    } else if (step->targetMode == TTP_TARGET_IMAGE) {
        snprintf(out, maxLen, "Image Match");
    } else {
        if (step->actionType == TTP_ACTION_DRAG) {
            snprintf(out, maxLen, "(%ld,%ld)->(%ld,%ld)", step->origX, step->origY, step->destX, step->destY);
        } else if (step->actionType == TTP_ACTION_TYPE_TEXT) {
            snprintf(out, maxLen, "\"%s\"", step->textKey);
        } else if (step->actionType == TTP_ACTION_HOTKEY) {
            snprintf(out, maxLen, "Key: %s", step->textKey);
        } else {
            snprintf(out, maxLen, "(%ld, %ld)", step->origX, step->origY);
        }
    }
}

static void FormatTitleRec(DWORD elapsedSec, DWORD stepCount, char* out, int maxLen) {
    snprintf(out, maxLen, "REC %02lu:%02lu (%lu steps)",
        (unsigned long)(elapsedSec / 60),
        (unsigned long)(elapsedSec % 60),
        (unsigned long)stepCount);
}

static void FormatTitlePlay(DWORD elapsedSec, DWORD currentStep, DWORD totalSteps, char* out, int maxLen) {
    snprintf(out, maxLen, "PLAY %02lu:%02lu (Step %lu/%lu)",
        (unsigned long)(elapsedSec / 60),
        (unsigned long)(elapsedSec % 60),
        (unsigned long)currentStep,
        (unsigned long)totalSteps);
}

static void FormatTitleIdle(const char* filename, char* out, int maxLen) {
    if (filename && filename[0]) {
        const char* slash = strrchr(filename, '\\');
        const char* fName = slash ? slash + 1 : filename;
        snprintf(out, maxLen, "TinyTask Pro - %s", fName);
    } else {
        snprintf(out, maxLen, "TinyTask Pro");
    }
}

static void UpdateTitle(void) {
    if (!g_hMainWnd) return;
    char title[128];
    if (g_State == STATE_RECORDING) {
        DWORD elapsed = (GetTickCount() - g_RecStartTime) / 1000;
        FormatTitleRec(elapsed, g_recClickCount, title, sizeof(title));
        SetWindowTextA(g_hMainWnd, title);
    } else if (g_State == STATE_PLAYING) {
        DWORD elapsed = (GetTickCount() - g_PlayStartTime) / 1000;
        FormatTitlePlay(elapsed, g_CurrentPlayStep + 1, g_stepCount, title, sizeof(title));
        SetWindowTextA(g_hMainWnd, title);
    } else {
        FormatTitleIdle(g_CurrentFileName, title, sizeof(title));
        SetWindowTextA(g_hMainWnd, title);
    }
}

/* =========================================================================
 * 3. File Loading & Saving
 * ========================================================================= */

static BOOL LoadLegacyRecFile(const char* filepath) {
    HANDLE hFile = CreateFileA(filepath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;
    DWORD fSize = GetFileSize(hFile, NULL);
    if (fSize < 20 || (fSize % 20) != 0) {
        CloseHandle(hFile);
        return FALSE;
    }
    BYTE* buf = (BYTE*)malloc(fSize);
    if (!buf) {
        CloseHandle(hFile);
        return FALSE;
    }
    DWORD read = 0;
    ReadFile(hFile, buf, fSize, &read, NULL);
    CloseHandle(hFile);

    ttp_synth_reset();
    DWORD eventCount = read / 20;
    for (DWORD i = 0; i < eventCount; i++) {
        BYTE* pEv = buf + i * 20;
        DWORD uMsg = *(DWORD*)(pEv + 0);
        DWORD p1   = *(DWORD*)(pEv + 4);
        DWORD p2   = *(DWORD*)(pEv + 8);
        DWORD ts   = *(DWORD*)(pEv + 12);
        if (uMsg == WM_MOUSEMOVE || uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONUP ||
            uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONUP || uMsg == WM_LBUTTONDBLCLK) {
            ttp_synth_add_mouse_event(uMsg, (LONG)p1, (LONG)p2, ts);
        } else if (uMsg == WM_KEYDOWN || uMsg == WM_KEYUP) {
            BYTE vk = (BYTE)(p1 & 0xFF);
            ttp_synth_add_key_event(vk, (uMsg == WM_KEYDOWN), ts);
        }
    }
    free(buf);

    TTPStep synthSteps[1024];
    DWORD numSteps = ttp_synth_finalize(synthSteps, 1024);
    StepArray_Clear();
    for (DWORD i = 0; i < numSteps; i++) {
        StepArray_Add(&synthSteps[i], NULL, 0);
    }
    return TRUE;
}

static BOOL SaveProjectFile(const char* filepath) {
    return ttp_save_project(filepath, g_steps, g_stepCount, (const BYTE**)g_bmpBuffers, g_bmpSizes);
}

static BOOL LoadProjectFile(const char* filepath) {
    if (!filepath || !filepath[0]) return FALSE;
    const char* ext = strrchr(filepath, '.');
    if (ext && _stricmp(ext, ".rec") == 0) {
        return LoadLegacyRecFile(filepath);
    }

    TTPStep* newSteps = NULL;
    DWORD count = 0;
    BYTE** newBmps = NULL;
    DWORD* newSizes = NULL;

    if (!ttp_load_project(filepath, &newSteps, &count, &newBmps, &newSizes)) {
        return FALSE;
    }

    StepArray_Clear();
    g_steps = newSteps;
    g_stepCount = count;
    g_stepCap = count;
    g_bmpBuffers = newBmps;
    g_bmpSizes = newSizes;
    return TRUE;
}

/* =========================================================================
 * 4. Toolbar Mask & Painting
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
    g_hBmpToolbar = (HBITMAP)LoadImageA(g_hInstance, MAKEINTRESOURCEA(4002), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    CreateToolbarMask();
}

/* =========================================================================
 * 5. ListView & In-Place Editing
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
    SendMessageA(g_hInPlaceEdit, EM_SETSEL, 0, -1);
    SetFocus(g_hInPlaceEdit);
}

static void SetDrawerState(BOOL expanded) {
    g_DrawerExpanded = expanded;
    int clientW = expanded ? CLIENT_EXPANDED_W : CLIENT_COLLAPSED_W;
    int clientH = expanded ? CLIENT_EXPANDED_H : CLIENT_COLLAPSED_H;
    RECT rc = { 0, 0, clientW, clientH };
    DWORD style = GetWindowLong(g_hMainWnd, GWL_STYLE);
    DWORD exStyle = GetWindowLong(g_hMainWnd, GWL_EXSTYLE);
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);

    SetWindowPos(g_hMainWnd, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    int showCmd = expanded ? SW_SHOW : SW_HIDE;
    if (g_hListView) ShowWindow(g_hListView, showCmd);
    if (g_hBtnAdd)   ShowWindow(g_hBtnAdd, showCmd);
    if (g_hBtnDel)   ShowWindow(g_hBtnDel, showCmd);
    if (g_hBtnUp)    ShowWindow(g_hBtnUp, showCmd);
    if (g_hBtnDown)  ShowWindow(g_hBtnDown, showCmd);
    if (g_hBtnRun)   ShowWindow(g_hBtnRun, showCmd);

    if (expanded) {
        RefreshListView();
    }
    InvalidateRect(g_hMainWnd, NULL, TRUE);
}

/* =========================================================================
 * 6. Recording & Synthesizer Integration
 * ========================================================================= */

static void AddRecordedClick(LONG x, LONG y, DWORD timestamp, BYTE* bmpData, DWORD bmpSize, const char* text) {
    if (g_recClickCount >= g_recClickCap) {
        DWORD newCap = (g_recClickCap == 0) ? 16 : g_recClickCap * 2;
        RecordedClick* newBuf = (RecordedClick*)realloc(g_recClicks, newCap * sizeof(RecordedClick));
        if (!newBuf) return;
        g_recClicks = newBuf;
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

static void ClearRecordedClicks(void) {
    if (g_recClicks) {
        for (DWORD i = 0; i < g_recClickCount; i++) {
            if (g_recClicks[i].bmpData) {
                ttp_free_bmp_buffer(g_recClicks[i].bmpData);
                g_recClicks[i].bmpData = NULL;
            }
        }
        free(g_recClicks);
        g_recClicks = NULL;
    }
    g_recClickCount = 0;
    g_recClickCap = 0;
}

static void CALLBACK RecTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (g_State != STATE_RECORDING) return;

    POINT pt;
    GetCursorPos(&pt);
    DWORD tick = GetTickCount();

    if (pt.x != g_LastMousePos.x || pt.y != g_LastMousePos.y) {
        g_LastMousePos = pt;
        ttp_synth_add_mouse_event(WM_MOUSEMOVE, pt.x, pt.y, tick);
    }

    for (int vk = 1; vk < 256; vk++) {
        SHORT state = GetAsyncKeyState(vk);
        BYTE isDown = (state & 0x8000) ? 1 : 0;
        if (isDown != g_LastKeyState[vk]) {
            g_LastKeyState[vk] = isDown;
            if (vk == VK_LBUTTON) {
                if (isDown) {
                    HWND hUnder = WindowFromPoint(pt);
                    /* Ignore clicks on TinyTask window */
                    if (hUnder != g_hMainWnd && GetAncestor(hUnder, GA_ROOT) != g_hMainWnd) {
                        HDC hdcScreen = GetDC(NULL);
                        RECT cropRect;
                        BYTE* bmpData = NULL;
                        DWORD bmpSize = 0;
                        ttp_adaptive_crop_button(hdcScreen, pt.x, pt.y, &cropRect, &bmpData, &bmpSize);
                        ReleaseDC(NULL, hdcScreen);

                        char textBuf[128] = {0};
                        if (hUnder) {
                            if (GetWindowTextA(hUnder, textBuf, sizeof(textBuf)) <= 0) {
                                SendMessageTimeoutA(hUnder, WM_GETTEXT, sizeof(textBuf), (LPARAM)textBuf, SMTO_ABORTIFHUNG, 50, NULL);
                            }
                        }
                        AddRecordedClick(pt.x, pt.y, tick, bmpData, bmpSize, textBuf);
                    }
                    ttp_synth_add_mouse_event(WM_LBUTTONDOWN, pt.x, pt.y, tick);
                } else {
                    ttp_synth_add_mouse_event(WM_LBUTTONUP, pt.x, pt.y, tick);
                }
            } else if (vk == VK_RBUTTON) {
                ttp_synth_add_mouse_event(isDown ? WM_RBUTTONDOWN : WM_RBUTTONUP, pt.x, pt.y, tick);
            } else if (vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU) {
                ttp_synth_add_key_event(vk, isDown, tick);
            }
        }
    }

    UpdateTitle();
}

static void StartRecording(void) {
    if (g_State == STATE_PLAYING) StopPlayback();
    g_State = STATE_RECORDING;
    ttp_synth_reset();
    ClearRecordedClicks();
    GetCursorPos(&g_LastMousePos);
    ZeroMemory(g_LastKeyState, sizeof(g_LastKeyState));
    g_RecStartTime = GetTickCount();
    SetTimer(g_hMainWnd, TIMER_REC, 10, RecTimerProc);
    InvalidateRect(g_hMainWnd, NULL, FALSE);
    UpdateTitle();
}

static void StopRecording(void) {
    KillTimer(g_hMainWnd, TIMER_REC);
    g_State = STATE_IDLE;

    TTPStep tempSteps[512];
    DWORD numFinal = ttp_synth_finalize(tempSteps, 512);

    StepArray_Clear();
    for (DWORD i = 0; i < numFinal; i++) {
        int bestClickIdx = -1;
        double bestDist = 1000000.0;
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
 * 7. Playback Engine
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
            if (g_SpeedMode == 0) {
                step.postDelayMs = (DWORD)(step.postDelayMs * 2.0);
            } else if (g_SpeedMode == 2) {
                step.postDelayMs = (DWORD)(step.postDelayMs * 0.5);
            }

            BOOL ok = ttp_playback_step(&step, g_bmpBuffers[i], g_bmpSizes[i], g_hMainWnd);
            if (!ok || g_StopPlaybackRequested) {
                goto thread_end;
            }
        }
        loop++;
        if (!g_Continuous) {
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
    if (g_State == STATE_PLAYING) {
        if ((GetAsyncKeyState(VK_PAUSE) & 0x8000) || (GetAsyncKeyState(VK_SCROLL) & 0x8000)) {
            StopPlayback();
        }
    }
}

/* =========================================================================
 * 8. Options Menu
 * ========================================================================= */

static void ShowOptionsMenu(HWND hwnd, int x, int y) {
    HMENU hMenu = CreatePopupMenu();
    AppendMenuA(hMenu, MF_STRING | (g_SpeedMode == 0 ? MF_CHECKED : 0), ID_OPT_SPEED_HALF, "Play Speed:   \xbd");
    AppendMenuA(hMenu, MF_STRING | (g_SpeedMode == 1 ? MF_CHECKED : 0), ID_OPT_SPEED_1X, "Play Speed:   &1x");
    AppendMenuA(hMenu, MF_STRING | (g_SpeedMode == 2 ? MF_CHECKED : 0), ID_OPT_SPEED_2X, "Play Speed:   &2x");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING | (g_Continuous ? MF_CHECKED : 0), ID_OPT_CONT, "&Continuous Playback");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING | (g_AlwaysOnTop ? MF_CHECKED : 0), ID_OPT_TOPMOST, "Always on &Top");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, ID_OPT_ABOUT, "&About TinyTask Pro...");

    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, x, y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

/* =========================================================================
 * 9. Main Window Procedure
 * ========================================================================= */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        g_hMainWnd = hwnd;
        g_hGuiFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
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

                HGDIOBJ hOld = SelectObject(hdcMem, g_hBmpMask);
                BitBlt(hdc, dst_x, dst_y, BUTTON_WIDTH, BUTTON_HEIGHT, hdcMem, src_x, src_y, SRCAND);

                SelectObject(hdcMem, g_hBmpToolbar);
                BitBlt(hdc, dst_x, dst_y, BUTTON_WIDTH, BUTTON_HEIGHT, hdcMem, src_x, src_y, SRCPAINT);

                SelectObject(hdcMem, hOld);

                if (i == 4 && g_DrawerExpanded) {
                    RECT rcBtn = { dst_x - 1, dst_y - 1, dst_x + BUTTON_WIDTH + 1, dst_y + BUTTON_HEIGHT + 1 };
                    DrawEdge(hdc, &rcBtn, BDR_SUNKENOUTER, BF_RECT);
                }
            }

            DeleteDC(hdcMem);
        }

        if (g_DrawerExpanded) {
            RECT rcSep = { 0, 51, rcClient.right, 53 };
            DrawEdge(hdc, &rcSep, EDGE_ETCHED, BF_TOP);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (pt.y >= TOOLBAR_PADDING && pt.y < TOOLBAR_PADDING + BUTTON_HEIGHT &&
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

        if (y >= TOOLBAR_PADDING && y < TOOLBAR_PADDING + BUTTON_HEIGHT) {
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
            POINT pt = { TOOLBAR_PADDING + 5 * (BUTTON_WIDTH + TOOLBAR_PADDING), TOOLBAR_PADDING + BUTTON_HEIGHT };
            ClientToScreen(hwnd, &pt);
            ShowOptionsMenu(hwnd, pt.x, pt.y);
            break;
        }

        case ID_OPT_SPEED_HALF: {
            g_SpeedMode = 0;
            break;
        }
        case ID_OPT_SPEED_1X: {
            g_SpeedMode = 1;
            break;
        }
        case ID_OPT_SPEED_2X: {
            g_SpeedMode = 2;
            break;
        }
        case ID_OPT_CONT: {
            g_Continuous = !g_Continuous;
            break;
        }
        case ID_OPT_TOPMOST: {
            g_AlwaysOnTop = !g_AlwaysOnTop;
            SetWindowPos(hwnd, g_AlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            break;
        }
        case ID_OPT_ABOUT: {
            MessageBoxA(hwnd,
                "TinyTask Pro 1.0\n\nNext-Gen Macro Automation with Computer Vision & Text Anchors.\nEngineered for Pixel-Perfect Reliability.",
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
            s.timeoutMs = 3000;
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

    case WM_DESTROY: {
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
 * 10. Application Entry Point
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
    wc.hIcon = (HICON)LoadImageA(hInstance, MAKEINTRESOURCEA(4001), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "TinyTaskProWnd";
    RegisterClassExA(&wc);

    RECT rc = { 0, 0, CLIENT_COLLAPSED_W, CLIENT_COLLAPSED_H };
    DWORD dwStyle = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectEx(&rc, dwStyle, FALSE, 0);

    int posX = (GetSystemMetrics(SM_CXSCREEN) - (rc.right - rc.left)) / 2;
    int posY = (GetSystemMetrics(SM_CYSCREEN) - (rc.bottom - rc.top)) / 2;

    HWND hwnd = CreateWindowExA(0, "TinyTaskProWnd", "TinyTask Pro",
        dwStyle, posX, posY, rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        if (g_hInPlaceEdit && IsDialogMessageA(g_hInPlaceEdit, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
#endif
