@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

rem Internal: packaging UI for engine workspace / arbitrary .cproject.
rem Game projects use their own root package.bat → invoke_engine.ps1 instead.
call "%~dp0catty_python.bat" "%~dp0package_ui.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] package_ui.py failed with exit code %ERR%
	pause
	exit /b %ERR%
)
exit /b 0
