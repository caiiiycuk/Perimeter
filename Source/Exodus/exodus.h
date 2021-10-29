#pragma once
#ifndef PERIMETER_EXODUS_H
#define PERIMETER_EXODUS_H

#include <cstdint>
#include <cstdlib>
#include <unistd.h>
#include "types.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//FPU control
//TODO we should handle FPU config stuff on non Win32?

#define _MCW_EM 0
#define _MCW_PC 0
#define _PC_24 0

#define _clearfp()
unsigned int _controlfp(unsigned int newval, unsigned int mask);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Window/UI related

//These seem to be used as mask for storing state in game code
#define MK_LBUTTON  0b1
#define MK_RBUTTON 0b10

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

uint32_t GetPrivateProfileString(const char* section,const char* key,const char* defaultVal,
                              char* returnBuffer, uint32_t bufferSize, const char* filePath);

uint32_t WritePrivateProfileString(const char* section,const char* key,const char* value, const char* filePath);

void Sleep(uint32_t millis);

char* _strlwr(char* str);
char* _strupr(char* str);

int __iscsym(int c);

#define strlwr _strlwr
#define strupr _strupr

#define IsCharAlphaNumeric isalnum


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//Event/Thread stuff

#include <pevents.h>

#define INFINITE neosmart::WAIT_INFINITE

#define WAIT_OBJECT_0 0

HANDLE CreateEvent(int, bool manualReset, bool initialState, int);

void DestroyEvent(HANDLE event);

void SetEvent(HANDLE event);

void ResetEvent(HANDLE event);

uint32_t WaitForSingleObject(HANDLE event, uint64_t milliseconds);

uint32_t WaitForMultipleObjects(int count, HANDLE* events, bool waitAll, uint64_t milliseconds);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif //PERIMETER_EXODUS_H
