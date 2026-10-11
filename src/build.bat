@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cd /d "%~dp0"
cl /nologo /MT /O2 /GS- /W3 /D_CRT_SECURE_NO_WARNINGS /LD xinput1_3.c /Fe:xinput1_3.dll /link /DEF:xinput1_3.def user32.lib kernel32.lib || exit /b 1
cl /nologo /MT /O2 /GS- /W3 /EHsc /D_CRT_SECURE_NO_WARNINGS /LD dinput8.cpp /Fe:dinput8.dll /link /DEF:dinput8.def dxguid.lib kernel32.lib || exit /b 1
