@echo off

SETLOCAL EnableExtensions EnableDelayedExpansion

del *.log

call buildAll_NoSign.bat

call tools\vs_run.bat qxldod.sln /Rebuild "Win10Debug|Win32" /Out build_Win10_Debug_Win32.log
if !ERRORLEVEL! NEQ 0 exit /B 1

call tools\vs_run.bat qxldod.sln /Rebuild "Win10Debug|x64" /Out build_Win10_Debug_x64.log
if !ERRORLEVEL! NEQ 0 exit /B 1

ENDLOCAL
