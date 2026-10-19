' ---------------------------------------------------------------------------
' STM32 cheatsheet launcher: opens the *web build* of the cheatsheet in Edge
' APP MODE - a plain window with no tab strip / address bar / bookmarks bar,
' so the page looks exactly like the web version but nothing blocks the top.
'
' ASCII-only on purpose: Windows Script Host reads a .vbs as ANSI unless the
' file carries a UTF-16 BOM, so Chinese literals in here would be mangled.
' Path math is done at run time from this script's own folder, so the whole
' folder can be copied to another machine and this still works.
' ---------------------------------------------------------------------------
Option Explicit

Dim fso, sh, here, f, best, bestSize, edge, url, args
Set fso = CreateObject("Scripting.FileSystemObject")
Set sh  = CreateObject("WScript.Shell")

here = fso.GetParentFolderName(WScript.ScriptFullName)

' Pick the DESKTOP web build: the .html that is small and ships with the
' sibling data_*.js files (the mobile single-file build inlines ~2.8 MB).
best = "" : bestSize = 0
For Each f In fso.GetFolder(here).Files
  If LCase(Right(f.Name, 5)) = ".html" Then
    If f.Size < 1000000 Then              ' desktop build is ~141 KB
      If f.Size > bestSize Then
        best = f.Path : bestSize = f.Size
      End If
    End If
  End If
Next
If best = "" Then
  MsgBox "cheatsheet .html not found in: " & here, 16, "STM32 lookup"
  WScript.Quit 1
End If

' Prefer Edge, then Chrome; if neither is there, hand the file to the default browser.
edge = sh.ExpandEnvironmentStrings("%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe")
If Not fso.FileExists(edge) Then
  edge = sh.ExpandEnvironmentStrings("%ProgramFiles%\Microsoft\Edge\Application\msedge.exe")
End If
If Not fso.FileExists(edge) Then
  edge = sh.ExpandEnvironmentStrings("%ProgramFiles%\Google\Chrome\Application\chrome.exe")
End If
If Not fso.FileExists(edge) Then
  sh.Run """" & best & """", 1, False
  WScript.Quit 0
End If

' The URL must be percent-encoded: with a raw CJK path the browser drops --app
' and just opens an ordinary tab (tested).
url = "file:///" & PctEncode(Replace(best, "\", "/"))
args = "--app=""" & url & """ --window-size=1180,880"
sh.Run """" & edge & """ " & args, 1, False

' UTF-8 percent-encode every byte except the ones a file URL needs literally.
Function PctEncode(s)
  Dim st, bytes, i, c, out, skip, bom
  Set st = CreateObject("ADODB.Stream")
  st.Type = 2 : st.Charset = "utf-8" : st.Open
  st.WriteText s
  st.Position = 0 : st.Type = 1
  ' ADODB writes a UTF-8 BOM (EF BB BF) by default - read past it, otherwise the
  ' URL starts with %EF%BB%BF and the browser refuses to load the file.
  skip = 0
  If st.Size >= 3 Then
    bom = st.Read(3)
    If AscB(MidB(bom, 1, 1)) = &HEF And AscB(MidB(bom, 2, 1)) = &HBB _
       And AscB(MidB(bom, 3, 1)) = &HBF Then
      skip = 3
    Else
      st.Position = 0
    End If
  End If
  bytes = st.Read
  st.Close
  out = ""
  For i = 1 To LenB(bytes)
    c = AscB(MidB(bytes, i, 1))
    If (c >= 48 And c <= 57) Or (c >= 65 And c <= 90) Or (c >= 97 And c <= 122) _
       Or c = 45 Or c = 46 Or c = 95 Or c = 126 Or c = 47 Or c = 58 Then
      out = out & Chr(c)
    Else
      out = out & "%" & Right("0" & Hex(c), 2)
    End If
  Next
  PctEncode = out
End Function
