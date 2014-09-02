@echo off

call clean.bat

rem WIN8_64
setlocal
if exist Install\win8\amd64 rmdir Install\win8\amd64 /s /q
call callVisualStudio.bat 12 qxldod.vcxproj /Rebuild "Win8 Release|x64" /Out buildfre_win8_amd64.log
mkdir .\Install\Win8\x64
del /Q .\Install\Win8\x64\*
copy /Y objfre_win8_amd64\amd64\qxldod.sys .\Install\Win8\x64
copy /Y objfre_win8_amd64\amd64\qxldod.inf .\Install\Win8\x64
copy /Y objfre_win8_amd64\amd64\qxldod.cat .\Install\Win8\x64
copy /Y objfre_win8_amd64\amd64\qxldod.pdb .\Install\Win8\x64
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

rem WIN8_32
setlocal
if exist Install\win8\x86 rmdir Install\win8\x86 /s /q
call callVisualStudio.bat 12 qxldod.vcxproj /Rebuild "Win8 Release|Win32" /Out buildfre_win8_x86.log
mkdir .\Install\Win8\x86
del /Q .\Install\Win8\x86\*
copy /Y objfre_win8_x86\i386\qxldod.sys .\Install\Win8\x86
copy /Y objfre_win8_x86\i386\qxldod.inf .\Install\Win8\x86
copy /Y objfre_win8_x86\i386\qxldod.cat .\Install\Win8\x86
copy /Y objfre_win8_x86\i386\qxldod.pdb .\Install\Win8\x86
endlocal
if %ERRORLEVEL% NEQ 0 goto :eof

:eof
