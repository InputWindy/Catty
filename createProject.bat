@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Launch GUI via WScript + pythonw (no Python console).
rem A brief cmd flash from this .bat is normal when double-clicked; the UI should stay open.

set "VBS=%~dp0Tools\launch_create_project.vbs"
if not exist "%VBS%" (
	echo [ERROR] Missing %VBS%
	pause
	exit /b 1
)

wscript //nologo "%VBS%"
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] Failed to launch create-project UI ^(exit %ERR%^).
	echo         Run setup.bat first if Tools\python is missing.
	pause
	exit /b %ERR%
)
exit /b 0
