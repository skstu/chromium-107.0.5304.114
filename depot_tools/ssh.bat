@echo off
setlocal
if not defined EDITOR set EDITOR=notepad
set PATH=%~dp0bootstrap-2@3_8_10_chromium_26_bin\git\cmd;%~dp0;%PATH%
"%~dp0bootstrap-2@3_8_10_chromium_26_bin\git\usr\bin\ssh.exe" %*
