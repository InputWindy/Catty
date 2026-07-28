@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

rem Internal: regenerate FObject CATTY_OBJECT reflection tables.
call "%~dp0catty_python.bat" "%~dp0object_reflect_codegen.py" %*
set "ERR=%ERRORLEVEL%"
if not "%ERR%"=="0" (
	echo [ERROR] object_reflect_codegen.py failed with exit code %ERR%
	exit /b %ERR%
)
exit /b 0
