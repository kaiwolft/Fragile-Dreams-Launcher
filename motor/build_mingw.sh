#!/bin/bash
# Compila el motor (fd_motor.exe) con MinGW-w64 desde Linux (lo usa GitHub Actions).
# En la PC del autor se compila con clang + lld (mismas opciones). Si esta compilacion falla, el workflow usa motor/bin/fd_motor.exe.
set -e
cd "$(dirname "$0")"
x86_64-w64-mingw32-windres fd_motor.rc -O coff -o fd_motor_res.o
x86_64-w64-mingw32-gcc -O2 -std=c11 -Wall -Wno-unused-function \
  -nostdlib -nostartfiles -mno-stack-arg-probe -fno-builtin-printf -I. \
  -D_WIN32_WINNT=0x0A00 -DWIN32_LEAN_AND_MEAN -D__USE_MINGW_ANSI_STDIO=0 -DUNICODE -D_UNICODE \
  fd_motor.c fd_motor_res.o -lmsvcrt -lkernel32 -luser32 -lgdi32 -lopengl32 -lwinmm -lxinput1_4 -lshell32 -lole32 -ladvapi32 -ldwmapi -lgcc \
  -Wl,--entry=fd_entry -Wl,--subsystem,windows -o fd_motor.exe
ls -la fd_motor.exe
