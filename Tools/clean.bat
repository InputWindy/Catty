@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

rem Internal: wipe Intermediate/Binaries/Packaged/Cached/Saved (and generated .sln etc.).
rem Does not remove Tools/python. Prefer running from Tools\; not a root user entry.
call "%~dp0catty_python.bat" "%~dp0clean.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] clean.py failed with exit code %ERR%
	exit /b %ERR%
)
exit /b 0
