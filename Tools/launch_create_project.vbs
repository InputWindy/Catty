Option Explicit
' Launch create_project.py with pythonw (no console of its own).
' WindowStyle MUST be 1+ — style 0 hides the Tk UI as well.

Dim fso, sh, tools, root, pyw, script, rc
Set fso = CreateObject("Scripting.FileSystemObject")
Set sh = CreateObject("WScript.Shell")

tools = fso.GetParentFolderName(WScript.ScriptFullName)
root = fso.GetParentFolderName(tools)
pyw = tools & "\python\pythonw.exe"
script = tools & "\create_project.py"

If Not fso.FileExists(pyw) Then
	MsgBox "Local Python not found." & vbCrLf & vbCrLf & _
		"Run setup.bat first in:" & vbCrLf & root, 16, "Catty"
	WScript.Quit 1
End If

If Not fso.FileExists(script) Then
	MsgBox "Missing script:" & vbCrLf & script, 16, "Catty"
	WScript.Quit 1
End If

' 1 = normal window (shows Tk). Do NOT use 0 — that hides the GUI.
rc = sh.Run("""" & pyw & """ """ & script & """", 1, False)
WScript.Quit 0
