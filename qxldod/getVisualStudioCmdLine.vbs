'
' Copyright 2013-2016 Red Hat, Inc.
'
' Licensed under the Apache License, Version 2.0 (the "License");
' you may not use this file except in compliance with the License.
'
' You may obtain a copy of the License at
' http://www.apache.org/licenses/LICENSE-2.0
'
'
' $1 - Visual studio version to run (10 or 11)
' $2 ... Parameters to pass

Dim strCmdLine, strTemp
Set WshShell = Wscript.CreateObject("Wscript.Shell")

On Error Resume Next
strCmdLine = WshShell.RegRead("HKLM\SOFTWARE\Microsoft\VisualStudio\" + Wscript.Arguments(0) + ".0\InstallDir")
' In case of error assume WoW64 case
If Err <> 0 Then
  On Error Goto 0
  strCmdLine = WshShell.RegRead("HKLM\SOFTWARE\Wow6432Node\Microsoft\VisualStudio\" + Wscript.Arguments(0) + ".0\InstallDir")
End If

On Error Goto 0
strCmdLine = chr(34) + strCmdLine + "devenv.com" + chr(34)
For i = 0 to (Wscript.Arguments.Count - 1)
  If i > 0 Then
  	strTemp = Wscript.Arguments(i)
  	If InStr(strTemp, " ") Or InStr(strTemp, "|") Then
  		strCmdLine = strCmdLine + " " + chr(34) + strTemp + chr(34)
  	Else
  		strCmdLine = strCmdLine + " " + strTemp
  	End If
  End If
Next

WScript.Echo strCmdLine + vbCrLf
