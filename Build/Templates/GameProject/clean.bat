@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Project one-click clean → engine Tools/clean.py (via local Tools helper)

where python >nul 2>&1
if errorlevel 1 (
	echo [ERROR] python not found in PATH.
	pause
	exit /b 1
)

python "%~dp0Tools\clean_invoke.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] Clean failed with exit code %ERR%
	pause
	exit /b %ERR%
)
exit /b 0
