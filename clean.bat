@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Wipe Intermediate/Binaries/Packaged/Cached/Saved (and generated .sln etc.).
rem Does not remove Tools/python. Requires setup.bat once.
call "%~dp0Tools\maho_python.bat" "%~dp0Tools\clean.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] clean.py failed with exit code %ERR%
	exit /b %ERR%
)
exit /b 0
