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
