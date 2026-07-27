@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Create a new Catty game project (UI). Requires local Python from setup.bat.
call "%~dp0Tools\catty_python.bat" "%~dp0Tools\create_project.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] create_project.py failed with exit code %ERR%
	exit /b %ERR%
)
exit /b 0
