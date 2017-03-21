@echo off

SETLOCAL EnableExtensions EnableDelayedExpansion

del *.log

call tools\vs_run.bat qxldod.sln /Rebuild "Win10Debug_NoSign|Win32" /Out build_Win10_Debug_NoSign_Win32.log
if !ERRORLEVEL! NEQ 0 exit /B 1

call tools\vs_run.bat qxldod.sln /Rebuild "Win10Release|Win32" /Out build_Win10_Release_Win32.log
if !ERRORLEVEL! NEQ 0 exit /B 1

call tools\vs_run.bat qxldod.sln /Rebuild "Win10Debug_NoSign|x64" /Out build_Win10_Debug_NoSign_x64.log
if !ERRORLEVEL! NEQ 0 exit /B 1

call tools\vs_run.bat qxldod.sln /Rebuild "Win10Release|x64" /Out build_Win10_Release_x64.log
if !ERRORLEVEL! NEQ 0 exit /B 1

ENDLOCAL
