@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Engine root launcher → packaging UI (optional: pass a .cproject path)
python "%~dp0Tools\package_ui.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] package_ui.py failed with exit code %ERR%
	pause
	exit /b %ERR%
)
exit /b 0
