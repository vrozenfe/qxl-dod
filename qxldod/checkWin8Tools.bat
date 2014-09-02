@echo off

reg query "HKLM\Software\Microsoft\Windows Kits\WDK" /v WDKProductVersion >nul 2>nul
if %ERRORLEVEL% EQU 0 goto checkVS12
reg query "HKLM\Software\Wow6432Node\Microsoft\Windows Kits\WDK" /v WDKProductVersion > nul 2>nul
if %ERRORLEVEL% EQU 0 goto checkVS12
echo ERROR building Win8 drivers: Win8 WDK is not installed
exit /b 1

:checkVS12
reg query HKLM\Software\Microsoft\VisualStudio\12.0 /v InstallDir > nul 2>nul
if %ERRORLEVEL% EQU 0 exit /b 0
reg query HKLM\Software\Wow6432Node\Microsoft\VisualStudio\12.0 /v InstallDir > nul 2>nul
if %ERRORLEVEL% EQU 0 exit /b 0
echo ERROR building Win8 drivers: VS11 is not installed
exit /b 2