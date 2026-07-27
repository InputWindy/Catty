@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Generate Visual Studio .sln from .cproject (or engine workspace). Requires setup.bat.
call "%~dp0Tools\catty_python.bat" "%~dp0Tools\generateProject.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] generateProject.py failed with exit code %ERR%
	exit /b %ERR%
)
exit /b 0
