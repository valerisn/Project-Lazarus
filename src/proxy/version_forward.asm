; forwarding stubs, each one just jumps through lz_real_version[n].
; asm so we never need the real signatures - GetFileVersionInfoByHandle is
; undocumented and guessing it wrong would trash the stack.
; order has to match kVersionExports in dllmain.cpp

EXTERN lz_real_version:QWORD

.code

lzfwd_GetFileVersionInfoA PROC
    jmp QWORD PTR [lz_real_version + 0*8]
lzfwd_GetFileVersionInfoA ENDP

lzfwd_GetFileVersionInfoByHandle PROC
    jmp QWORD PTR [lz_real_version + 1*8]
lzfwd_GetFileVersionInfoByHandle ENDP

lzfwd_GetFileVersionInfoExA PROC
    jmp QWORD PTR [lz_real_version + 2*8]
lzfwd_GetFileVersionInfoExA ENDP

lzfwd_GetFileVersionInfoExW PROC
    jmp QWORD PTR [lz_real_version + 3*8]
lzfwd_GetFileVersionInfoExW ENDP

lzfwd_GetFileVersionInfoSizeA PROC
    jmp QWORD PTR [lz_real_version + 4*8]
lzfwd_GetFileVersionInfoSizeA ENDP

lzfwd_GetFileVersionInfoSizeExA PROC
    jmp QWORD PTR [lz_real_version + 5*8]
lzfwd_GetFileVersionInfoSizeExA ENDP

lzfwd_GetFileVersionInfoSizeExW PROC
    jmp QWORD PTR [lz_real_version + 6*8]
lzfwd_GetFileVersionInfoSizeExW ENDP

lzfwd_GetFileVersionInfoSizeW PROC
    jmp QWORD PTR [lz_real_version + 7*8]
lzfwd_GetFileVersionInfoSizeW ENDP

lzfwd_GetFileVersionInfoW PROC
    jmp QWORD PTR [lz_real_version + 8*8]
lzfwd_GetFileVersionInfoW ENDP

lzfwd_VerFindFileA PROC
    jmp QWORD PTR [lz_real_version + 9*8]
lzfwd_VerFindFileA ENDP

lzfwd_VerFindFileW PROC
    jmp QWORD PTR [lz_real_version + 10*8]
lzfwd_VerFindFileW ENDP

lzfwd_VerInstallFileA PROC
    jmp QWORD PTR [lz_real_version + 11*8]
lzfwd_VerInstallFileA ENDP

lzfwd_VerInstallFileW PROC
    jmp QWORD PTR [lz_real_version + 12*8]
lzfwd_VerInstallFileW ENDP

lzfwd_VerLanguageNameA PROC
    jmp QWORD PTR [lz_real_version + 13*8]
lzfwd_VerLanguageNameA ENDP

lzfwd_VerLanguageNameW PROC
    jmp QWORD PTR [lz_real_version + 14*8]
lzfwd_VerLanguageNameW ENDP

lzfwd_VerQueryValueA PROC
    jmp QWORD PTR [lz_real_version + 15*8]
lzfwd_VerQueryValueA ENDP

lzfwd_VerQueryValueW PROC
    jmp QWORD PTR [lz_real_version + 16*8]
lzfwd_VerQueryValueW ENDP

END
