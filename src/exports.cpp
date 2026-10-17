// Generates, for every name in LIST_ALL:
//   - a global function pointer  fp_<name>  (filled at runtime from the real DLL)
//   - an exported stub           <name>     (a bare `jmp *fp_<name>`)
// A bare jmp keeps the caller's stack/registers untouched, so it works for any
// signature or calling convention (cdecl/stdcall/x64) without knowing prototypes.
#include "proxy_exports.h"

#ifdef _WIN64
  #define SYM(s) s
  #define JMP(s) "jmp *" s "(%rip)"
#else
  #define SYM(s) "_" s
  #define JMP(s) "jmp *" s
#endif

#define DEFINE_STUB(n)                                                        \
    extern "C" { FARPROC fp_##n = nullptr; }                                  \
    asm(".section .drectve\n\t.ascii \" -export:" #n "\"\n\t"                 \
        ".text\n\t.p2align 4\n\t.globl " SYM(#n) "\n"                        \
        SYM(#n) ":\n\t" JMP(SYM("fp_" #n)) "\n");
        
LIST_ALL(DEFINE_STUB)
