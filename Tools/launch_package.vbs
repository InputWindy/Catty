Option Explicit
' Launch package_ui.py with engine Tools\python\pythonw.exe only (never system Python).
' Optional args (e.g. path to .cproject) are forwarded to the script.
' WindowStyle MUST be 1+ — style 0 hides the Tk UI as well.

Dim fso, sh, tools, root, pyw, script, cmdline, i, rc
Set fso = CreateObject("Scripting.FileSystemObject")
Set sh = CreateObject("WScript.Shell")

tools = fso.GetParentFolderName(WScript.ScriptFullName)
root = fso.GetParentFolderName(tools)
pyw = tools & "\python\pythonw.exe"
script = tools & "\package_ui.py"

If Not fso.FileExists(pyw) Then
	MsgBox "Local Python not found." & vbCrLf & vbCrLf & _
		"Run setup.bat first in:" & vbCrLf & root, 16, "Catty"
	WScript.Quit 1
End If

If Not fso.FileExists(script) Then
	MsgBox "Missing script:" & vbCrLf & script, 16, "Catty"
	WScript.Quit 1
End If

cmdline = """" & pyw & """ """ & script & """"
For i = 0 To WScript.Arguments.Count - 1
	cmdline = cmdline & " """ & WScript.Arguments(i) & """"
Next

' 1 = normal window (shows Tk). Do NOT use 0 — that hides the GUI.
rc = sh.Run(cmdline, 1, False)
WScript.Quit 0
