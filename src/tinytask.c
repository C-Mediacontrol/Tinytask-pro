#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

/* TinyTask exact 20-byte event binary structure */
#pragma pack(push, 1)
typedef struct {
    DWORD uMsg;      /* WM_MOUSEMOVE, WM_LBUTTONDOWN/UP, WM_RBUTTONDOWN/UP, WM_KEYDOWN/UP */
    DWORD param1;    /* Mouse X coord OR (scanCode << 8) | vkCode */
    DWORD param2;    /* Mouse Y coord OR scanCode/flags */
    DWORD timestamp; /* GetTickCount() milliseconds */
    DWORD hwnd;      /* Foreground HWND */
} TinyTaskEvent;
#pragma pack(pop)

/* Command IDs matching TinyTask 1.77 */
#define ID_BTN_OPEN     0x8000
#define ID_BTN_SAVE     0x8001
#define ID_BTN_REC      0x8002
#define ID_BTN_PLAY     0x8003
#define ID_BTN_COMPILE  0x8004
#define ID_BTN_OPTIONS  0x8005

#define ID_SPEED_HALF   0x8006
#define ID_SPEED_1X     0x8007
#define ID_SPEED_2X     0x8008
#define ID_SPEED_CUSTOM 0x8009
#define ID_SPEED_100X   0x800A
#define ID_SPEED_CONT   0x800B
#define ID_SET_LOOPS    0x800C

#define ID_WEBSITE      0x800D
#define ID_ABOUT        0x800E

#define ID_REC_HOTKEY_STD   0x800F /* Ctrl+Shift+Alt+R */
#define ID_REC_HOTKEY_PRTSC 0x8010 /* PrintScreen */
#define ID_REC_HOTKEY_F8    0x8011 /* F8 */
#define ID_REC_HOTKEY_F12   0x8012 /* F12 */

#define ID_PLAY_HOTKEY_STD   0x8013 /* Ctrl+Shift+Alt+P */
#define ID_PLAY_HOTKEY_PRTSC 0x8014 /* PrintScreen */
#define ID_PLAY_HOTKEY_F8    0x8015 /* F8 */
#define ID_PLAY_HOTKEY_F12   0x8016 /* F12 */

#define ID_TOPMOST          0x8017
#define ID_SHOW_CAPTIONS    0x8018
#define ID_SET_SPEED        0x8019
#define ID_TOOLBAR_CUSTOM   0x801A
#define ID_TOOLBAR_DEFAULT  0x801B

/* Custom Hotkey extensions */
#define ID_REC_HOTKEY_CUSTOM_ACTIVE  0x8020
#define ID_REC_HOTKEY_CUSTOM_SET     0x8021
#define ID_PLAY_HOTKEY_CUSTOM_ACTIVE 0x8022
#define ID_PLAY_HOTKEY_CUSTOM_SET    0x8023

/* Speed modes */
#define SPEED_HALF   0
#define SPEED_1X     1
#define SPEED_2X     2
#define SPEED_100X   100
#define SPEED_CUSTOM 999

/* Timers */
#define TIMER_REC       1001
#define TIMER_PLAY      1002
#define TIMER_HOTKEY    1005

/* Dimensions matching TinyTask 1.77 exactly */
#define BUTTON_WIDTH    38
#define BUTTON_HEIGHT   44
#define TOOLBAR_PADDING 5
#define NUM_BUTTONS     6

#define CLIENT_WIDTH    (NUM_BUTTONS * BUTTON_WIDTH + (NUM_BUTTONS + 1) * TOOLBAR_PADDING) /* 263 */
#define CLIENT_HEIGHT   (BUTTON_HEIGHT + 2 * TOOLBAR_PADDING)                              /* 54 */

/* State flags */
#define STATE_IDLE      0
#define STATE_RECORDING 1
#define STATE_PLAYING   2

/* Hotkey structure */
typedef struct {
    int  mode;           /* 0=Standard, 1=PrtSc, 8=F8, 12=F12, 99=Custom */
    BOOL customSet;
    UINT customVk;
    UINT customMod;     /* MOD_CONTROL, MOD_ALT, MOD_SHIFT bitmask combinations */
    char customName[64];
} HotkeyState;

/* Global Variables */
static HINSTANCE g_hInstance;
static HWND g_hMainWnd;
static int g_State = STATE_IDLE;
static int g_HideCaptionsOffset = 0; /* 0 = show captions (44px), 12 = hide captions (32px) */
static BOOL g_HasCustomToolbar = FALSE;
static char g_CustomToolbarPath[MAX_PATH] = "";
static int  g_WindowX = -9999;
static int  g_WindowY = -9999;

/* GDI Resources */
static HBITMAP g_hBmpToolbar = NULL;
static HBITMAP g_hBmpMask = NULL;

/* Dynamic Event Storage */
static TinyTaskEvent* g_pEvents = NULL;
static DWORD g_EventCount = 0;
static DWORD g_EventCapacity = 0;

/* Playback state */
static DWORD g_PlayIndex = 0;
static DWORD g_PlayLoopCurrent = 0;
static DWORD g_PlayLoopTotal = 1;
static BOOL  g_Continuous = FALSE;
static int   g_SpeedMode = SPEED_1X;
static int   g_CustomSpeed = 5;
static BOOL  g_AlwaysOnTop = FALSE;
static DWORD g_RecStartTime = 0;
static DWORD g_PlayStartTime = 0;
static char  g_CurrentFileName[MAX_PATH] = "";

/* Hotkey configurations */
static HotkeyState g_RecHotkey = { 0, FALSE, 0, 0, "" };
static HotkeyState g_PlayHotkey = { 0, FALSE, 0, 0, "" };

/* Polling states */
static BYTE g_LastKeyState[256];
static POINT g_LastMousePos;

/* Forward declarations */
static void StartRecording(void);
static void StopRecording(void);
static void StartPlayback(void);
static void StopPlayback(void);
static void AddEvent(DWORD uMsg, DWORD p1, DWORD p2, DWORD hwnd);
static void LoadRecFile(const char* filename);
static void SaveRecFile(const char* filename);
static void CompileToExe(const char* filename);
static void ShowOptionsMenu(HWND hwnd, int x, int y);
static void UpdateTitle(void);
static void CreateToolbarBitmaps(void);
static void LoadConfig(void);
static void SaveConfig(void);

/* String helper to avoid CRT dependency */
static char* FindLastChar(const char* s, char c) {
    const char* last = NULL;
    while (*s) {
        if (*s == c) last = s;
        s++;
    }
    return (char*)last;
}

/* Detect currently held modifier keys via GetAsyncKeyState (physical hardware state) */
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

/* Format hotkey into human-readable text (e.g. "Ctrl + Alt + R", "Ctrl + Shift + F9") */
static void FormatHotkeyText(UINT vk, UINT mod, char* out, int maxLen) {
    out[0] = '\0';
    if (mod & MOD_CONTROL) lstrcatA(out, "Ctrl + ");
    if (mod & MOD_ALT)     lstrcatA(out, "Alt + ");
    if (mod & MOD_SHIFT)   lstrcatA(out, "Shift + ");

    if (vk == 0) {
        if (mod != 0) {
            lstrcatA(out, "...");
        } else {
            lstrcatA(out, "[ Press any key... ]");
        }
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

/* Ensure event buffer has space */
static void EnsureCapacity(DWORD needed) {
    if (needed <= g_EventCapacity) return;
    DWORD newCap = g_EventCapacity == 0 ? 1000 : g_EventCapacity + 1000;
    if (newCap < needed) newCap = needed;
    TinyTaskEvent* newBuf = (TinyTaskEvent*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, newCap * sizeof(TinyTaskEvent));
    if (g_pEvents) {
        CopyMemory(newBuf, g_pEvents, g_EventCount * sizeof(TinyTaskEvent));
        HeapFree(GetProcessHeap(), 0, g_pEvents);
    }
    g_pEvents = newBuf;
    g_EventCapacity = newCap;
}

static void AddEvent(DWORD uMsg, DWORD p1, DWORD p2, DWORD hwnd) {
    EnsureCapacity(g_EventCount + 1);
    g_pEvents[g_EventCount].uMsg = uMsg;
    g_pEvents[g_EventCount].param1 = p1;
    g_pEvents[g_EventCount].param2 = p2;
    g_pEvents[g_EventCount].timestamp = GetTickCount();
    g_pEvents[g_EventCount].hwnd = hwnd;
    g_EventCount++;
}

/* Clear pressed keys/buttons to prevent stuck input */
static void ReleaseAllInputs(void) {
    mouse_event(MOUSEEVENTF_LEFTUP | MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
    for (int i = 1; i < 256; i++) {
        if (i == VK_PAUSE || i == VK_SCROLL || i == VK_CAPITAL || i == VK_NUMLOCK) continue;
        if (GetAsyncKeyState(i) & 0x8000) {
            DWORD scan = MapVirtualKeyA(i, 0);
            keybd_event(i, (BYTE)scan, KEYEVENTF_KEYUP, 0);
        }
    }
}

/* Update Window Title bar with accurate status text */
static void UpdateTitle(void) {
    if (g_State == STATE_RECORDING) {
        DWORD elapsed = (GetTickCount() - g_RecStartTime) / 1000;
        char title[64];
        wsprintfA(title, "REC %02d:%02d", elapsed / 60, elapsed % 60);
        SetWindowTextA(g_hMainWnd, title);
    } else if (g_State == STATE_PLAYING) {
        DWORD elapsed = (GetTickCount() - g_PlayStartTime) / 1000;
        char title[64];
        if (g_Continuous) {
            wsprintfA(title, "%02d:%02d (Loop %d)", elapsed / 60, elapsed % 60, g_PlayLoopCurrent + 1);
        } else {
            wsprintfA(title, "%02d:%02d (%d/%d)", elapsed / 60, elapsed % 60, g_PlayLoopCurrent + 1, g_PlayLoopTotal);
        }
        SetWindowTextA(g_hMainWnd, title);
    } else {
        if (g_CurrentFileName[0]) {
            const char* slash = FindLastChar(g_CurrentFileName, '\\');
            const char* name = slash ? slash + 1 : g_CurrentFileName;
            char title[MAX_PATH + 32];
            wsprintfA(title, "TinyTask - %s", name);
            SetWindowTextA(g_hMainWnd, title);
        } else {
            SetWindowTextA(g_hMainWnd, "TinyTask");
        }
    }
}

/* Create the transparency mask for pixel-perfect blitting */
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

    /* Background color of toolbar image is top-left pixel RGB(232, 232, 232) */
    COLORREF crTransparent = GetPixel(hdcSrc, 0, 0);
    SetBkColor(hdcSrc, crTransparent);

    /* Blit from color DC to monochrome DC creates exact 1-bit transparency mask:
     * transparent background is 1 (white), opaque sprite pixels are 0 (black). */
    BitBlt(hdcMask, 0, 0, w, h, hdcSrc, 0, 0, SRCCOPY);

    /* Zero out transparent background pixels via SRCINVERT (XOR) matching original TinyTask (0x4038f0) */
    BitBlt(hdcSrc, 0, 0, w, h, hdcMask, 0, 0, SRCINVERT);

    SelectObject(hdcSrc, hOldSrc);
    SelectObject(hdcMask, hOldMask);
    DeleteDC(hdcSrc);
    DeleteDC(hdcMask);
    ReleaseDC(NULL, hdcScreen);
}

static BOOL LoadCustomToolbarFile(const char* bmpFile) {
    HBITMAP hNewBmp = (HBITMAP)LoadImageA(NULL, bmpFile, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (hNewBmp) {
        BITMAP bm;
        GetObjectA(hNewBmp, sizeof(bm), &bm);
        if (bm.bmWidth >= BUTTON_WIDTH * NUM_BUTTONS && bm.bmHeight >= 20) {
            if (g_hBmpToolbar) DeleteObject(g_hBmpToolbar);
            g_hBmpToolbar = hNewBmp;
            CreateToolbarMask();
            return TRUE;
        } else {
            DeleteObject(hNewBmp);
        }
    }
    return FALSE;
}

static void CreateToolbarBitmaps(void) {
    if (!g_HasCustomToolbar) {
        if (g_hBmpToolbar) { DeleteObject(g_hBmpToolbar); g_hBmpToolbar = NULL; }
        g_hBmpToolbar = (HBITMAP)LoadImageA(g_hInstance, MAKEINTRESOURCEA(4002), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    }
    CreateToolbarMask();
}

static void LoadCustomToolbar(HWND hwnd) {
    char bmpFile[MAX_PATH] = "";
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = "Bitmap Files (*.bmp)\0*.bmp\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = bmpFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) {
        if (LoadCustomToolbarFile(bmpFile)) {
            g_HasCustomToolbar = TRUE;
            lstrcpynA(g_CustomToolbarPath, bmpFile, MAX_PATH);
            InvalidateRect(hwnd, NULL, TRUE);
            SaveConfig();
        } else {
            MessageBoxA(hwnd, "Invalid toolbar BMP", "TinyTask", MB_ICONWARNING);
        }
    }
}

static void RestoreDefaultToolbar(HWND hwnd) {
    if (g_HasCustomToolbar) {
        g_HasCustomToolbar = FALSE;
        g_CustomToolbarPath[0] = '\0';
        CreateToolbarBitmaps();
        InvalidateRect(hwnd, NULL, TRUE);
        SaveConfig();
    }
}

/* Recording Timer Callback (10ms polling) */
static void CALLBACK RecTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (g_State != STATE_RECORDING) return;

    HWND hFore = GetForegroundWindow();
    POINT pt;
    GetCursorPos(&pt);

    /* Check mouse movement */
    if (pt.x != g_LastMousePos.x || pt.y != g_LastMousePos.y) {
        g_LastMousePos = pt;
        AddEvent(WM_MOUSEMOVE, (DWORD)pt.x, (DWORD)pt.y, (DWORD)(DWORD_PTR)hFore);
    }

    /* Check virtual keys and mouse buttons */
    for (int vk = 1; vk < 256; vk++) {
        SHORT state = GetAsyncKeyState(vk);
        BYTE isDown = (state & 0x8000) ? 1 : 0;
        if (isDown != g_LastKeyState[vk]) {
            g_LastKeyState[vk] = isDown;
            if (vk == VK_LBUTTON) {
                AddEvent(isDown ? WM_LBUTTONDOWN : WM_LBUTTONUP, (DWORD)pt.x, (DWORD)pt.y, (DWORD)(DWORD_PTR)hFore);
            } else if (vk == VK_RBUTTON) {
                AddEvent(isDown ? WM_RBUTTONDOWN : WM_RBUTTONUP, (DWORD)pt.x, (DWORD)pt.y, (DWORD)(DWORD_PTR)hFore);
            } else if (vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU) {
                DWORD scan = MapVirtualKeyA(vk, 0);
                AddEvent(isDown ? WM_KEYDOWN : WM_KEYUP, (scan << 8) | (BYTE)vk, scan, (DWORD)(DWORD_PTR)hFore);
            }
        }
    }

    UpdateTitle();
}

/* Playback Timer Callback */
static void CALLBACK PlayTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (g_State != STATE_PLAYING) return;

    /* Emergency stop check */
    if ((GetAsyncKeyState(VK_PAUSE) & 0x8000) || (GetAsyncKeyState(VK_SCROLL) & 0x8000)) {
        StopPlayback();
        return;
    }

    if (g_PlayIndex >= g_EventCount) {
        g_PlayLoopCurrent++;
        if (g_Continuous || g_PlayLoopCurrent < g_PlayLoopTotal) {
            g_PlayIndex = 0;
            SetTimer(g_hMainWnd, TIMER_PLAY, 50, PlayTimerProc);
            return;
        } else {
            StopPlayback();
            return;
        }
    }

    /* Execute current event */
    TinyTaskEvent* ev = &g_pEvents[g_PlayIndex];
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    if (ev->uMsg == WM_MOUSEMOVE) {
        DWORD dx = (DWORD)((ev->param1 * 65535ULL) / (screenW > 0 ? screenW : 1));
        DWORD dy = (DWORD)((ev->param2 * 65535ULL) / (screenH > 0 ? screenH : 1));
        mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE, dx, dy, 0, 0);
        SetCursorPos(ev->param1, ev->param2);
    } else if (ev->uMsg == WM_LBUTTONDOWN) {
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
    } else if (ev->uMsg == WM_LBUTTONUP) {
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    } else if (ev->uMsg == WM_RBUTTONDOWN) {
        mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
    } else if (ev->uMsg == WM_RBUTTONUP) {
        mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
    } else if (ev->uMsg == WM_KEYDOWN) {
        BYTE vk = (BYTE)(ev->param1 & 0xFF);
        BYTE scan = (BYTE)((ev->param1 >> 8) & 0xFF);
        keybd_event(vk, scan, 0, 0);
    } else if (ev->uMsg == WM_KEYUP) {
        BYTE vk = (BYTE)(ev->param1 & 0xFF);
        BYTE scan = (BYTE)((ev->param1 >> 8) & 0xFF);
        keybd_event(vk, scan, KEYEVENTF_KEYUP, 0);
    }

    g_PlayIndex++;

    /* Calculate delay to next event based on exact TinyTask speed math */
    DWORD delay = 10;
    if (g_PlayIndex < g_EventCount) {
        DWORD dt = g_pEvents[g_PlayIndex].timestamp - ev->timestamp;
        if (g_SpeedMode == SPEED_HALF) {
            delay = dt * 2; /* 0.5x speed: double interval */
        } else if (g_SpeedMode == SPEED_2X) {
            delay = dt / 2;
        } else if (g_SpeedMode == SPEED_100X) {
            delay = dt / 100;
        } else if (g_SpeedMode == SPEED_CUSTOM && g_CustomSpeed > 0) {
            delay = dt / g_CustomSpeed;
        } else {
            delay = dt; /* 1x normal speed */
        }
        if (delay < 1) delay = 1;
        if (delay > 5000) delay = 5000;
    }

    UpdateTitle();
    SetTimer(g_hMainWnd, TIMER_PLAY, delay, PlayTimerProc);
}

/* Check if a hotkey combination is pressed */
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

/* Hotkey polling timer (50ms) */
static void CALLBACK HotkeyTimerProc(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    if (IsHotkeyTriggered(&g_RecHotkey, 'R')) {
        Sleep(150);
        PostMessageA(g_hMainWnd, WM_COMMAND, ID_BTN_REC, 0);
        return;
    }

    if (IsHotkeyTriggered(&g_PlayHotkey, 'P')) {
        Sleep(150);
        PostMessageA(g_hMainWnd, WM_COMMAND, ID_BTN_PLAY, 0);
    }
}

static void RemoveTrailingHotkeyEvents(const HotkeyState* hk, char stdKey) {
    UINT targetVk = 0;
    if (hk->mode == 0) targetVk = (UINT)stdKey;
    else if (hk->mode == 1) targetVk = VK_SNAPSHOT;
    else if (hk->mode == 8) targetVk = VK_F8;
    else if (hk->mode == 12) targetVk = VK_F12;
    else if (hk->mode == 99 && hk->customSet) targetVk = hk->customVk;

    if (targetVk == 0) return;

    while (g_EventCount > 0) {
        TinyTaskEvent* last = &g_pEvents[g_EventCount - 1];
        if ((last->uMsg == WM_KEYDOWN || last->uMsg == WM_KEYUP) && (BYTE)(last->param1 & 0xFF) == (BYTE)targetVk) {
            g_EventCount--;
        } else {
            break;
        }
    }
}

static void StartRecording(void) {
    if (g_State == STATE_PLAYING) StopPlayback();
    ReleaseAllInputs();
    g_EventCount = 0;
    GetCursorPos(&g_LastMousePos);
    ZeroMemory(g_LastKeyState, sizeof(g_LastKeyState));
    g_RecStartTime = GetTickCount();
    g_State = STATE_RECORDING;
    SetTimer(g_hMainWnd, TIMER_REC, 10, RecTimerProc);
    InvalidateRect(g_hMainWnd, NULL, FALSE);
    UpdateTitle();
}

static void StopRecording(void) {
    KillTimer(g_hMainWnd, TIMER_REC);
    RemoveTrailingHotkeyEvents(&g_RecHotkey, 'R');
    ReleaseAllInputs();
    g_State = STATE_IDLE;
    InvalidateRect(g_hMainWnd, NULL, FALSE);
    UpdateTitle();
}

static void StartPlayback(void) {
    if (g_EventCount == 0) {
        MessageBoxA(g_hMainWnd, "Nothing Recorded\nPress the blue button to start a new recording", "TinyTask", MB_ICONINFORMATION);
        return;
    }
    if (g_State == STATE_RECORDING) StopRecording();
    ReleaseAllInputs();
    g_PlayIndex = 0;
    g_PlayLoopCurrent = 0;
    g_PlayStartTime = GetTickCount();
    g_State = STATE_PLAYING;
    SetTimer(g_hMainWnd, TIMER_PLAY, 10, PlayTimerProc);
    InvalidateRect(g_hMainWnd, NULL, FALSE);
    UpdateTitle();
}

static void StopPlayback(void) {
    KillTimer(g_hMainWnd, TIMER_PLAY);
    ReleaseAllInputs();
    g_State = STATE_IDLE;
    InvalidateRect(g_hMainWnd, NULL, FALSE);
    UpdateTitle();
}

static void LoadRecFile(const char* filename) {
    HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxA(g_hMainWnd, "Unable to read file", "TinyTask", MB_ICONERROR);
        return;
    }
    DWORD fileSize = GetFileSize(hFile, NULL);
    if (fileSize < sizeof(TinyTaskEvent) || (fileSize % sizeof(TinyTaskEvent)) != 0) {
        int r = MessageBoxA(g_hMainWnd, "This file does not appear to be a valid recording.\nLoad anyway?", "TinyTask", MB_YESNO | MB_ICONWARNING);
        if (r != IDYES) {
            CloseHandle(hFile);
            return;
        }
    }
    DWORD count = fileSize / sizeof(TinyTaskEvent);
    EnsureCapacity(count);
    DWORD bytesRead = 0;
    ReadFile(hFile, g_pEvents, fileSize, &bytesRead, NULL);
    CloseHandle(hFile);
    g_EventCount = bytesRead / sizeof(TinyTaskEvent);
    lstrcpynA(g_CurrentFileName, filename, MAX_PATH);
    UpdateTitle();
}

static void SaveRecFile(const char* filename) {
    if (g_EventCount == 0) {
        MessageBoxA(g_hMainWnd, "Nothing Recorded", "TinyTask", MB_ICONINFORMATION);
        return;
    }
    HANDLE hFile = CreateFileA(filename, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        MessageBoxA(g_hMainWnd, "Unable to write file", "TinyTask", MB_ICONERROR);
        return;
    }
    DWORD bytesWritten = 0;
    WriteFile(hFile, g_pEvents, g_EventCount * sizeof(TinyTaskEvent), &bytesWritten, NULL);
    CloseHandle(hFile);
    lstrcpynA(g_CurrentFileName, filename, MAX_PATH);
    UpdateTitle();
}

/* Compile to standalone executable by cloning self and appending recording data */
static void CompileToExe(const char* filename) {
    if (g_EventCount == 0) {
        MessageBoxA(g_hMainWnd, "Nothing Recorded", "TinyTask", MB_ICONINFORMATION);
        return;
    }
    char myPath[MAX_PATH];
    GetModuleFileNameA(NULL, myPath, MAX_PATH);

    if (!CopyFileA(myPath, filename, FALSE)) {
        MessageBoxA(g_hMainWnd, "Compile Error", "TinyTask", MB_ICONERROR);
        return;
    }

    HANDLE hTarget = CreateFileA(filename, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hTarget == INVALID_HANDLE_VALUE) return;

    SetFilePointer(hTarget, 0, NULL, FILE_END);
    DWORD written = 0;
    WriteFile(hTarget, g_pEvents, g_EventCount * sizeof(TinyTaskEvent), &written, NULL);
    CloseHandle(hTarget);

    char msg[256];
    wsprintfA(msg, "Compile successful\n\"%s\"   (%d events)", filename, g_EventCount);
    MessageBoxA(g_hMainWnd, msg, "TinyTask", MB_ICONINFORMATION);
}

/* Modal number input dialog */
static BOOL g_PromptDlgRunning = FALSE;
static int  g_PromptResult = 0;
static const char* g_PromptLabel = NULL;
static int  g_PromptDefault = 0;
static int  g_PromptMin = 0;
static int  g_PromptMax = 0;
static HWND g_hPromptEdit = NULL;

static LRESULT CALLBACK PromptDlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND hStatic = CreateWindowExA(0, "STATIC", g_PromptLabel,
            WS_CHILD | WS_VISIBLE, 15, 12, 240, 20, hwnd, NULL, g_hInstance, NULL);
        SendMessageA(hStatic, WM_SETFONT, (WPARAM)hFont, TRUE);

        char numStr[32];
        wsprintfA(numStr, "%d", g_PromptDefault);
        g_hPromptEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", numStr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_AUTOHSCROLL,
            15, 36, 240, 22, hwnd, (HMENU)101, g_hInstance, NULL);
        SendMessageA(g_hPromptEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnOk = CreateWindowExA(0, "BUTTON", "OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            60, 68, 70, 24, hwnd, (HMENU)IDOK, g_hInstance, NULL);
        SendMessageA(hBtnOk, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnCancel = CreateWindowExA(0, "BUTTON", "Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            145, 68, 70, 24, hwnd, (HMENU)IDCANCEL, g_hInstance, NULL);
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
        wc.lpszClassName = "TinyPromptDlg";
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
    int posX = rcParent.left + (rcParent.right - rcParent.left - 280) / 2;
    int posY = rcParent.top + (rcParent.bottom - rcParent.top - 135) / 2;
    if (posX < 0) posX = 100;
    if (posY < 0) posY = 100;

    HWND hDlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "TinyPromptDlg", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        posX, posY, 280, 135,
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

static LRESULT CALLBACK HotkeyDlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND hLbl = CreateWindowExA(0, "STATIC", "Press your desired key combination:",
            WS_CHILD | WS_VISIBLE, 15, 12, 270, 18, hwnd, NULL, g_hInstance, NULL);
        SendMessageA(hLbl, WM_SETFONT, (WPARAM)hFont, TRUE);

        g_hHotkeyStaticDisplay = CreateWindowExA(WS_EX_CLIENTEDGE, "STATIC",
            g_CapturedName[0] ? g_CapturedName : "[ Press any key... ]",
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
            15, 34, 270, 28, hwnd, (HMENU)101, g_hInstance, NULL);
        SendMessageA(g_hHotkeyStaticDisplay, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hHint = CreateWindowExA(0, "STATIC", "Supports combinations like Ctrl + Alt + R, F8, etc.",
            WS_CHILD | WS_VISIBLE | SS_CENTER, 15, 68, 270, 16, hwnd, NULL, g_hInstance, NULL);
        SendMessageA(hHint, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnOk = CreateWindowExA(0, "BUTTON", "OK",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            75, 92, 70, 24, hwnd, (HMENU)IDOK, g_hInstance, NULL);
        SendMessageA(hBtnOk, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hBtnCancel = CreateWindowExA(0, "BUTTON", "Cancel",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            160, 92, 70, 24, hwnd, (HMENU)IDCANCEL, g_hInstance, NULL);
        SendMessageA(hBtnCancel, WM_SETFONT, (WPARAM)hFont, TRUE);

        SetFocus(hwnd);
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        UINT vk = (UINT)wParam;
        UINT mod = GetCurrentModifiers();

        /* If user pressed modifier key alone, show live preview like "Ctrl + Alt + ..." */
        if (vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU ||
            vk == VK_LSHIFT || vk == VK_RSHIFT ||
            vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_LMENU || vk == VK_RMENU) {
            char tempName[64];
            FormatHotkeyText(0, mod, tempName, sizeof(tempName));
            SetWindowTextA(g_hHotkeyStaticDisplay, tempName);
            return 0;
        }

        /* Non-modifier key pressed! Capture VK + complete modifier set */
        g_CapturedVk = vk;
        g_CapturedMod = mod;
        FormatHotkeyText(vk, mod, g_CapturedName, sizeof(g_CapturedName));
        SetWindowTextA(g_hHotkeyStaticDisplay, g_CapturedName);
        return 0;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP: {
        /* If no key has been locked in yet, update the modifier preview */
        if (g_CapturedVk == 0) {
            UINT mod = GetCurrentModifiers();
            char tempName[64];
            FormatHotkeyText(0, mod, tempName, sizeof(tempName));
            SetWindowTextA(g_hHotkeyStaticDisplay, tempName);
        }
        return 0;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        if (id == IDOK) {
            if (g_CapturedVk != 0) {
                g_HotkeyAccepted = TRUE;
            }
            g_HotkeyDlgRunning = FALSE;
            DestroyWindow(hwnd);
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

static BOOL PromptHotkey(HWND hParent, const char* title, HotkeyState* pHk) {
    static BOOL s_registered = FALSE;
    if (!s_registered) {
        WNDCLASSEXA wc = {0};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = HotkeyDlgProc;
        wc.hInstance = g_hInstance;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "TinyHotkeyDlg";
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
    int posX = rcParent.left + (rcParent.right - rcParent.left - 310) / 2;
    int posY = rcParent.top + (rcParent.bottom - rcParent.top - 160) / 2;
    if (posX < 0) posX = 100;
    if (posY < 0) posY = 100;

    HWND hDlg = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        "TinyHotkeyDlg", title,
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        posX, posY, 310, 160,
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
    WritePrivateProfileStringA("TinyTask", key, buf, ini);
}

static void LoadConfig(void) {
    char ini[MAX_PATH];
    GetIniPath(ini, sizeof(ini));

    g_WindowX = GetPrivateProfileIntA("TinyTask", "window_x", -9999, ini);
    g_WindowY = GetPrivateProfileIntA("TinyTask", "window_y", -9999, ini);
    g_AlwaysOnTop = GetPrivateProfileIntA("TinyTask", "topmost", 0, ini);
    g_HideCaptionsOffset = GetPrivateProfileIntA("TinyTask", "hide_captions", 0, ini) ? 12 : 0;
    g_CustomSpeed = GetPrivateProfileIntA("TinyTask", "speed_custom", 5, ini);
    if (g_CustomSpeed < 1) g_CustomSpeed = 1;
    if (g_CustomSpeed > 100) g_CustomSpeed = 100;

    int spd = GetPrivateProfileIntA("TinyTask", "speed", 1, ini);
    if (spd == 0) g_SpeedMode = SPEED_HALF;
    else if (spd == 1) g_SpeedMode = SPEED_1X;
    else if (spd == 2) g_SpeedMode = SPEED_2X;
    else if (spd == 100) g_SpeedMode = SPEED_100X;
    else { g_SpeedMode = SPEED_CUSTOM; g_CustomSpeed = spd; }

    g_Continuous = GetPrivateProfileIntA("TinyTask", "continuous", 0, ini) ? TRUE : FALSE;
    g_PlayLoopTotal = GetPrivateProfileIntA("TinyTask", "loops", 1, ini);
    if (g_PlayLoopTotal < 1) g_PlayLoopTotal = 1;

    g_RecHotkey.mode = GetPrivateProfileIntA("TinyTask", "record_key", 0, ini);
    g_PlayHotkey.mode = GetPrivateProfileIntA("TinyTask", "play_key", 0, ini);

    g_RecHotkey.customVk = GetPrivateProfileIntA("TinyTask", "custom_rec_vk", 0, ini);
    g_RecHotkey.customMod = GetPrivateProfileIntA("TinyTask", "custom_rec_mod", 0, ini);
    GetPrivateProfileStringA("TinyTask", "custom_rec_name", "", g_RecHotkey.customName, sizeof(g_RecHotkey.customName), ini);
    if (g_RecHotkey.customVk != 0) g_RecHotkey.customSet = TRUE;

    g_PlayHotkey.customVk = GetPrivateProfileIntA("TinyTask", "custom_play_vk", 0, ini);
    g_PlayHotkey.customMod = GetPrivateProfileIntA("TinyTask", "custom_play_mod", 0, ini);
    GetPrivateProfileStringA("TinyTask", "custom_play_name", "", g_PlayHotkey.customName, sizeof(g_PlayHotkey.customName), ini);
    if (g_PlayHotkey.customVk != 0) g_PlayHotkey.customSet = TRUE;

    char tbImg[MAX_PATH] = "";
    GetPrivateProfileStringA("TinyTask", "toolbar_image", "", tbImg, sizeof(tbImg), ini);
    if (tbImg[0] && LoadCustomToolbarFile(tbImg)) {
        g_HasCustomToolbar = TRUE;
        lstrcpynA(g_CustomToolbarPath, tbImg, sizeof(g_CustomToolbarPath));
    }
}

static void SaveConfig(void) {
    char ini[MAX_PATH];
    GetIniPath(ini, sizeof(ini));

    if (g_hMainWnd) {
        RECT rc;
        GetWindowRect(g_hMainWnd, &rc);
        WriteIniInt("window_x", rc.left, ini);
        WriteIniInt("window_y", rc.top, ini);
    }

    WriteIniInt("topmost", g_AlwaysOnTop ? 1 : 0, ini);
    WriteIniInt("hide_captions", g_HideCaptionsOffset ? 1 : 0, ini);
    WriteIniInt("speed_custom", g_CustomSpeed, ini);

    int spd = 1;
    if (g_SpeedMode == SPEED_HALF) spd = 0;
    else if (g_SpeedMode == SPEED_1X) spd = 1;
    else if (g_SpeedMode == SPEED_2X) spd = 2;
    else if (g_SpeedMode == SPEED_100X) spd = 100;
    else if (g_SpeedMode == SPEED_CUSTOM) spd = g_CustomSpeed;
    WriteIniInt("speed", spd, ini);

    WriteIniInt("continuous", g_Continuous ? 1 : 0, ini);
    WriteIniInt("loops", g_PlayLoopTotal, ini);
    WriteIniInt("record_key", g_RecHotkey.mode, ini);
    WriteIniInt("play_key", g_PlayHotkey.mode, ini);

    if (g_RecHotkey.customSet) {
        WriteIniInt("custom_rec_vk", g_RecHotkey.customVk, ini);
        WriteIniInt("custom_rec_mod", g_RecHotkey.customMod, ini);
        WritePrivateProfileStringA("TinyTask", "custom_rec_name", g_RecHotkey.customName, ini);
    }

    if (g_PlayHotkey.customSet) {
        WriteIniInt("custom_play_vk", g_PlayHotkey.customVk, ini);
        WriteIniInt("custom_play_mod", g_PlayHotkey.customMod, ini);
        WritePrivateProfileStringA("TinyTask", "custom_play_name", g_PlayHotkey.customName, ini);
    }

    WritePrivateProfileStringA("TinyTask", "toolbar_image", g_HasCustomToolbar ? g_CustomToolbarPath : "", ini);
}

static BOOL CheckHotkeyConflict(HWND hwnd, int newMode, UINT newVk, UINT newMod, BOOL isRecording) {
    const HotkeyState* other = isRecording ? &g_PlayHotkey : &g_RecHotkey;
    if (newMode != 0) {
        if (newMode == other->mode && newMode != 99) {
            MessageBoxA(hwnd, "Hotkey Conflict", "TinyTask", MB_ICONINFORMATION);
            return TRUE;
        }
        if (newMode == 99 && other->mode == 99 && newVk == other->customVk && newMod == other->customMod) {
            MessageBoxA(hwnd, "Hotkey Conflict", "TinyTask", MB_ICONINFORMATION);
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

static void ShowOptionsMenu(HWND hwnd, int x, int y) {
    HMENU hMenu = CreatePopupMenu();
    HMENU hRecHot = CreatePopupMenu();
    HMENU hPlayHot = CreatePopupMenu();

    /* Speed options */
    AppendMenuA(hMenu, MF_STRING | (g_SpeedMode == SPEED_HALF ? MF_CHECKED : 0), ID_SPEED_HALF, "Play Speed:   \xbd");
    AppendMenuA(hMenu, MF_STRING | (g_SpeedMode == SPEED_1X ? MF_CHECKED : 0), ID_SPEED_1X, "Play Speed:   &1x");
    AppendMenuA(hMenu, MF_STRING | (g_SpeedMode == SPEED_2X ? MF_CHECKED : 0), ID_SPEED_2X, "Play Speed:   &2x");
    AppendMenuA(hMenu, MF_STRING | (g_SpeedMode == SPEED_100X ? MF_CHECKED : 0), ID_SPEED_100X, "Play Speed:   100x");

    char customSpeedStr[64];
    wsprintfA(customSpeedStr, "&Play Custom Speed:  %dx", g_CustomSpeed);
    AppendMenuA(hMenu, MF_STRING | (g_SpeedMode == SPEED_CUSTOM ? MF_CHECKED : 0), ID_SPEED_CUSTOM, customSpeedStr);
    AppendMenuA(hMenu, MF_STRING, ID_SET_SPEED, "&Set Custom Speed...");

    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING | (g_Continuous ? MF_CHECKED : 0), ID_SPEED_CONT, "&Continuous Playback");

    char loopStr[64];
    wsprintfA(loopStr, "&Set Playback Loops...  (%d)", g_PlayLoopTotal);
    AppendMenuA(hMenu, MF_STRING, ID_SET_LOOPS, loopStr);

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
    AppendMenuA(hMenu, MF_STRING | (g_AlwaysOnTop ? MF_CHECKED : 0), ID_TOPMOST, "Always on &Top");
    AppendMenuA(hMenu, MF_STRING | (g_HideCaptionsOffset == 0 ? MF_CHECKED : 0), ID_SHOW_CAPTIONS, "Show Captions");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, ID_TOOLBAR_CUSTOM, "Use Custom Tool&bar...");
    AppendMenuA(hMenu, MF_STRING, ID_TOOLBAR_DEFAULT, "Use &Default Toolbar");
    AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(hMenu, MF_STRING, ID_WEBSITE, "TinyTask &Website");
    AppendMenuA(hMenu, MF_STRING, ID_ABOUT, "&About TinyTask 1.77");

    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN, x, y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

/* Check if running as compiled standalone payload */
static void CheckSelfOverlay(void) {
    char myPath[MAX_PATH];
    GetModuleFileNameA(NULL, myPath, MAX_PATH);
    HANDLE hFile = CreateFileA(myPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD fSize = GetFileSize(hFile, NULL);
    if (fSize > 35000 && (fSize % sizeof(TinyTaskEvent)) == 0) {
        DWORD baseSize = 28160;
        if (fSize > baseSize) {
            SetFilePointer(hFile, baseSize, NULL, FILE_BEGIN);
            DWORD dataBytes = fSize - baseSize;
            DWORD count = dataBytes / sizeof(TinyTaskEvent);
            EnsureCapacity(count);
            DWORD read = 0;
            ReadFile(hFile, g_pEvents, dataBytes, &read, NULL);
            g_EventCount = read / sizeof(TinyTaskEvent);
            if (g_EventCount > 0) {
                StartPlayback();
            }
        }
    }
    CloseHandle(hFile);
}

/* Main Window Procedure */
static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        g_hMainWnd = hwnd;
        CreateToolbarBitmaps();
        SetTimer(hwnd, TIMER_HOTKEY, 50, HotkeyTimerProc);
        CheckSelfOverlay();
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
            int drawH = BUTTON_HEIGHT - g_HideCaptionsOffset;

            for (int i = 0; i < NUM_BUTTONS; i++) {
                int frame = i;
                if (i == 2 && g_State == STATE_RECORDING) {
                    frame = 6; /* Red stop square */
                } else if (i == 3 && g_State == STATE_PLAYING) {
                    frame = 6; /* Red stop square */
                }

                int src_x = frame * BUTTON_WIDTH;
                int src_y = 0;
                int dst_x = TOOLBAR_PADDING + i * (BUTTON_WIDTH + TOOLBAR_PADDING);
                int dst_y = TOOLBAR_PADDING;

                /* Pass 1: Monochrome mask with SRCAND (0x008800C6) */
                HGDIOBJ hOld = SelectObject(hdcMem, g_hBmpMask);
                BitBlt(hdc, dst_x, dst_y, BUTTON_WIDTH, drawH, hdcMem, src_x, src_y, SRCAND);

                /* Pass 2: Color bitmap with SRCPAINT (0x00EE0086) */
                SelectObject(hdcMem, g_hBmpToolbar);
                BitBlt(hdc, dst_x, dst_y, BUTTON_WIDTH, drawH, hdcMem, src_x, src_y, SRCPAINT);

                SelectObject(hdcMem, hOld);
            }

            DeleteDC(hdcMem);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE:
        SetCursor(LoadCursor(NULL, IDC_HAND));
        return 0;

    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);

    case WM_LBUTTONDOWN: {
        int x = (short)LOWORD(lParam);
        int y = (short)HIWORD(lParam);
        int drawH = BUTTON_HEIGHT - g_HideCaptionsOffset;

        if (y >= TOOLBAR_PADDING && y < TOOLBAR_PADDING + drawH) {
            for (int i = 0; i < NUM_BUTTONS; i++) {
                int bx = TOOLBAR_PADDING + i * (BUTTON_WIDTH + TOOLBAR_PADDING);
                if (x >= bx && x < bx + BUTTON_WIDTH) {
                    SendMessageA(hwnd, WM_COMMAND, ID_BTN_OPEN + i, 0);
                    break;
                }
            }
        }
        return 0;
    }

    case WM_COMMAND: {
        WORD cmd = LOWORD(wParam);
        switch (cmd) {
        case ID_BTN_OPEN: {
            char file[MAX_PATH] = "";
            OPENFILENAMEA ofn = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "Recording Files (*.rec)\0*.rec\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                LoadRecFile(file);
            }
            break;
        }
        case ID_BTN_SAVE: {
            char file[MAX_PATH] = "rec.rec";
            OPENFILENAMEA ofn = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "Recording Files (*.rec)\0*.rec\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT;
            if (GetSaveFileNameA(&ofn)) {
                SaveRecFile(file);
            }
            break;
        }
        case ID_BTN_REC:
            if (g_State == STATE_RECORDING) StopRecording();
            else StartRecording();
            break;
        case ID_BTN_PLAY:
            if (g_State == STATE_PLAYING) StopPlayback();
            else StartPlayback();
            break;
        case ID_BTN_COMPILE: {
            char file[MAX_PATH] = "task.exe";
            OPENFILENAMEA ofn = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner = hwnd;
            ofn.lpstrFilter = "Program Files (*.exe)\0*.exe\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile = file;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_OVERWRITEPROMPT;
            if (GetSaveFileNameA(&ofn)) {
                CompileToExe(file);
            }
            break;
        }
        case ID_BTN_OPTIONS: {
            POINT pt;
            pt.x = TOOLBAR_PADDING + 5 * (BUTTON_WIDTH + TOOLBAR_PADDING);
            pt.y = TOOLBAR_PADDING + (BUTTON_HEIGHT - g_HideCaptionsOffset) + 2;
            ClientToScreen(hwnd, &pt);
            ShowOptionsMenu(hwnd, pt.x, pt.y);
            break;
        }
        case ID_SHOW_CAPTIONS: {
            RECT rc;
            GetWindowRect(hwnd, &rc);
            int curW = rc.right - rc.left;
            int curH = rc.bottom - rc.top;
            if (g_HideCaptionsOffset == 0) {
                g_HideCaptionsOffset = 12;
                curH -= 12;
            } else {
                g_HideCaptionsOffset = 0;
                curH += 12;
            }
            SetWindowPos(hwnd, NULL, 0, 0, curW, curH, SWP_NOMOVE | SWP_NOZORDER);
            InvalidateRect(hwnd, NULL, TRUE);
            SaveConfig();
            break;
        }
        case ID_SPEED_HALF:
            g_SpeedMode = SPEED_HALF;
            SaveConfig();
            break;
        case ID_SPEED_1X:
            g_SpeedMode = SPEED_1X;
            SaveConfig();
            break;
        case ID_SPEED_2X:
            g_SpeedMode = SPEED_2X;
            SaveConfig();
            break;
        case ID_SPEED_100X:
            g_SpeedMode = SPEED_100X;
            SaveConfig();
            break;
        case ID_SPEED_CUSTOM:
            g_SpeedMode = SPEED_CUSTOM;
            SaveConfig();
            break;
        case ID_SET_SPEED: {
            int val = PromptNumber(hwnd, "Set Custom Speed", "   Playback speed multiplier (1-100):", g_CustomSpeed, 1, 100);
            if (val > 0) {
                g_CustomSpeed = val;
                g_SpeedMode = SPEED_CUSTOM;
                SaveConfig();
            }
            break;
        }
        case ID_SPEED_CONT:
            g_Continuous = !g_Continuous;
            SaveConfig();
            break;
        case ID_SET_LOOPS: {
            int val = PromptNumber(hwnd, "Set Playback Loops", "   Set the number of playback loops:", (int)g_PlayLoopTotal, 1, 999999);
            if (val > 0) {
                g_PlayLoopTotal = (DWORD)val;
                g_Continuous = FALSE;
                SaveConfig();
            }
            break;
        }
        case ID_REC_HOTKEY_STD:   SetPresetHotkey(hwnd, &g_RecHotkey, 0, 0, TRUE); break;
        case ID_REC_HOTKEY_PRTSC: SetPresetHotkey(hwnd, &g_RecHotkey, 1, VK_SNAPSHOT, TRUE); break;
        case ID_REC_HOTKEY_F8:    SetPresetHotkey(hwnd, &g_RecHotkey, 8, VK_F8, TRUE); break;
        case ID_REC_HOTKEY_F12:   SetPresetHotkey(hwnd, &g_RecHotkey, 12, VK_F12, TRUE); break;
        case ID_REC_HOTKEY_CUSTOM_ACTIVE:
            if (g_RecHotkey.customSet) {
                if (!CheckHotkeyConflict(hwnd, 99, g_RecHotkey.customVk, g_RecHotkey.customMod, TRUE)) {
                    g_RecHotkey.mode = 99;
                    SaveConfig();
                }
            }
            break;
        case ID_REC_HOTKEY_CUSTOM_SET:
            if (PromptHotkey(hwnd, "Set Recording Hotkey", &g_RecHotkey)) {
                if (CheckHotkeyConflict(hwnd, 99, g_RecHotkey.customVk, g_RecHotkey.customMod, TRUE)) {
                    g_RecHotkey.mode = 0;
                }
                SaveConfig();
            }
            break;
        case ID_PLAY_HOTKEY_STD:   SetPresetHotkey(hwnd, &g_PlayHotkey, 0, 0, FALSE); break;
        case ID_PLAY_HOTKEY_PRTSC: SetPresetHotkey(hwnd, &g_PlayHotkey, 1, VK_SNAPSHOT, FALSE); break;
        case ID_PLAY_HOTKEY_F8:    SetPresetHotkey(hwnd, &g_PlayHotkey, 8, VK_F8, FALSE); break;
        case ID_PLAY_HOTKEY_F12:   SetPresetHotkey(hwnd, &g_PlayHotkey, 12, VK_F12, FALSE); break;
        case ID_PLAY_HOTKEY_CUSTOM_ACTIVE:
            if (g_PlayHotkey.customSet) {
                if (!CheckHotkeyConflict(hwnd, 99, g_PlayHotkey.customVk, g_PlayHotkey.customMod, FALSE)) {
                    g_PlayHotkey.mode = 99;
                    SaveConfig();
                }
            }
            break;
        case ID_PLAY_HOTKEY_CUSTOM_SET:
            if (PromptHotkey(hwnd, "Set Playback Hotkey", &g_PlayHotkey)) {
                if (CheckHotkeyConflict(hwnd, 99, g_PlayHotkey.customVk, g_PlayHotkey.customMod, FALSE)) {
                    g_PlayHotkey.mode = 0;
                }
                SaveConfig();
            }
            break;
        case ID_TOPMOST:
            g_AlwaysOnTop = !g_AlwaysOnTop;
            SetWindowPos(hwnd, g_AlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SaveConfig();
            break;
        case ID_TOOLBAR_CUSTOM:
            LoadCustomToolbar(hwnd);
            break;
        case ID_TOOLBAR_DEFAULT:
            RestoreDefaultToolbar(hwnd);
            break;
        case ID_ABOUT:
            MessageBoxA(hwnd,
                "TinyTask 1.77\n"
                "Copyright \xa9 2019, All Rights Reserved.\n\n"
                "Build Date:  Nov  4 2019 @ 03:40:02\n\n"
                "\x95\x95\x95> If you like this software, please keep it alive by donating at the website above. Thank You!!   : )\n",
                "TinyTask", MB_ICONINFORMATION);
            break;
        case ID_WEBSITE:
            ShellExecuteA(hwnd, "open", "https://www.tinytask.net", NULL, NULL, SW_SHOWNORMAL);
            break;
        }
        return 0;
    }

    case WM_DESTROY:
        SaveConfig();
        KillTimer(hwnd, TIMER_HOTKEY);
        if (g_hBmpToolbar) { DeleteObject(g_hBmpToolbar); g_hBmpToolbar = NULL; }
        if (g_hBmpMask) { DeleteObject(g_hBmpMask); g_hBmpMask = NULL; }
        if (g_pEvents) {
            HeapFree(GetProcessHeap(), 0, g_pEvents);
            g_pEvents = NULL;
        }
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcA(hwnd, uMsg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hInstance = hInstance;

    LoadConfig();

    WNDCLASSEXA wc = {0};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIconA(hInstance, MAKEINTRESOURCEA(4001));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "TinyTaskClass";

    if (!RegisterClassExA(&wc)) {
        MessageBoxA(NULL, "Startup Failure: RegisterClass", "TinyTask", MB_ICONERROR);
        return 1;
    }

    int frame = GetSystemMetrics(SM_CXFIXEDFRAME);
    int caption = GetSystemMetrics(SM_CYCAPTION);
    int winW = NUM_BUTTONS * (BUTTON_WIDTH + TOOLBAR_PADDING) + TOOLBAR_PADDING + 2 * frame;
    int winH = (BUTTON_HEIGHT - g_HideCaptionsOffset) + 2 * TOOLBAR_PADDING + caption + 2 * frame;

    int scrW = GetSystemMetrics(SM_CXSCREEN);
    int scrH = GetSystemMetrics(SM_CYSCREEN);
    int posX = g_WindowX;
    int posY = g_WindowY;
    if (posX < -50 || posX > scrW - 50) posX = (scrW - winW) / 2;
    if (posY < -50 || posY > scrH - 50) posY = (scrH - winH) / 2;

    DWORD dwStyle = 0x86CA0000; /* WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX */
    DWORD dwExStyle = WS_EX_APPWINDOW;
    if (g_AlwaysOnTop) {
        dwExStyle |= WS_EX_TOPMOST;
    }

    g_hMainWnd = CreateWindowExA(
        dwExStyle,
        "TinyTaskClass",
        "TinyTask",
        dwStyle,
        posX, posY, winW, winH,
        NULL, NULL, hInstance, NULL
    );

    if (!g_hMainWnd) {
        MessageBoxA(NULL, "Startup Failure: CreateWindow", "TinyTask", MB_ICONERROR);
        return 1;
    }

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}

void WinMainCRTStartup(void) {
    HINSTANCE hInst = GetModuleHandleA(NULL);
    int ret = WinMain(hInst, NULL, GetCommandLineA(), SW_SHOWDEFAULT);
    ExitProcess((UINT)ret);
}
