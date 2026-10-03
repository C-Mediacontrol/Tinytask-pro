#ifndef TTP_CORE_H
#define TTP_CORE_H

#include <windows.h>

/* Action types */
#define TTP_ACTION_CLICK     1
#define TTP_ACTION_DBLCLICK  2
#define TTP_ACTION_RCLICK    3
#define TTP_ACTION_DRAG      4
#define TTP_ACTION_TYPE_TEXT 5
#define TTP_ACTION_HOTKEY    6

/* Target modes */
#define TTP_TARGET_TEXT      1
#define TTP_TARGET_IMAGE     2
#define TTP_TARGET_COORD     3

/* Magic and version */
#define TTP_MAGIC "TTP1"
#define TTP_VERSION 1

/* Timeout fallback actions (stored in targetMode high 16-bits) */
#define TTP_TIMEOUT_ACT_DEFAULT       0 /* Show modal prompt */
#define TTP_TIMEOUT_ACT_RETRY         1 /* Retry until found */
#define TTP_TIMEOUT_ACT_USE_RECORDED  2 /* Click recorded coordinate */
#define TTP_TIMEOUT_ACT_SKIP          3 /* Skip step */
#define TTP_TIMEOUT_ACT_STOP          4 /* Stop playback */

#define TTP_GET_BASE_TARGET_MODE(m)     ((DWORD)((m) & 0x0000FFFF))
#define TTP_GET_TIMEOUT_ACTION(m)       ((int)(((m) >> 16) & 0x0000FFFF))
#define TTP_MAKE_TARGET_MODE(base, act) (((DWORD)(base) & 0x0000FFFF) | (((DWORD)(act) & 0x0000FFFF) << 16))

#pragma pack(push, 1)

typedef struct {
    char magic[4];      /* "TTP1" */
    DWORD version;      /* 1 */
    DWORD stepCount;
    DWORD flags;
} TTPHeader;

typedef struct {
    DWORD stepId;
    DWORD actionType;
    DWORD targetMode;
    LONG origX;
    LONG origY;
    LONG destX;
    LONG destY;
    DWORD timeoutMs;
    DWORD postDelayMs;
    char textKey[128];
    DWORD imageOffset;
    DWORD imageSize;
} TTPStep;

#pragma pack(pop)

#endif /* TTP_CORE_H */
