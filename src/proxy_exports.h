#pragma once
// Export tables for every DLL name this proxy can masquerade as.
// To add a new target: add a LIST_xxx macro, include it in LIST_ALL (only names
// not already present), add a Bind function + kTargets entry in main.cpp.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define LIST_VERSION(X) \
    X(GetFileVersionInfoA) X(GetFileVersionInfoByHandle) X(GetFileVersionInfoExA) \
    X(GetFileVersionInfoExW) X(GetFileVersionInfoSizeA) X(GetFileVersionInfoSizeExA) \
    X(GetFileVersionInfoSizeExW) X(GetFileVersionInfoSizeW) X(GetFileVersionInfoW) \
    X(VerFindFileA) X(VerFindFileW) X(VerInstallFileA) X(VerInstallFileW) \
    X(VerLanguageNameA) X(VerLanguageNameW) X(VerQueryValueA) X(VerQueryValueW)

#define LIST_DINPUT8(X) \
    X(DirectInput8Create) X(DllCanUnloadNow) X(DllGetClassObject) \
    X(DllRegisterServer) X(DllUnregisterServer) X(GetdfDIJoystick)

// Names unique to dsound (DllCanUnloadNow / DllGetClassObject are shared with dinput8)
#define LIST_DSOUND_ONLY(X) \
    X(DirectSoundCreate) X(DirectSoundEnumerateA) X(DirectSoundEnumerateW) \
    X(DirectSoundCaptureCreate) X(DirectSoundCaptureEnumerateA) \
    X(DirectSoundCaptureEnumerateW) X(DirectSoundCreate8) \
    X(DirectSoundCaptureCreate8) X(DirectSoundFullDuplexCreate) X(GetDeviceID)

#define LIST_DSOUND(X) LIST_DSOUND_ONLY(X) X(DllCanUnloadNow) X(DllGetClassObject)

// Union of everything: one stub + one function pointer per unique name.
#define LIST_ALL(X) LIST_VERSION(X) LIST_DINPUT8(X) LIST_DSOUND_ONLY(X)

#define DECLARE_FP(n) extern FARPROC fp_##n;
extern "C" { LIST_ALL(DECLARE_FP) }
