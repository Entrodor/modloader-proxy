#pragma once
// The export lists themselves live in exports/*.txt and are turned into
// proxy_lists_gen.h by tools/gen_exports.py (see Makefile).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "proxy_lists_gen.h"

#define DECLARE_FP(n) extern FARPROC fp_##n;
extern "C" { LIST_ALL(DECLARE_FP) }
