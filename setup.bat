@echo off
setlocal EnableExtensions
cd /d "%~dp0"
python "%~dp0Tools\setup.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] setup.py failed with exit code %ERR%
	exit /b %ERR%
)
exit /b 0
