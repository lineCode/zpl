@echo off
SETLOCAL

if not exist bin mkdir bin

pushd bin
set WARN=-Wall -Wextra
set CMN=-g --std=c11
set OPTS=
set LIBS=user32.lib gdi32.lib winmm.lib opengl32.lib kernel32.lib
set SUBSYSTEM=console

python.exe w:\zpl\code\zpl\build.py

ctime -begin quick.ctm
clang ..\%1 %WARN% %OPTS% %CMN% -I..\..\code
ctime -end quick.ctm
popd