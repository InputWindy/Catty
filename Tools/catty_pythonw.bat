@echo off
setlocal EnableExtensions
rem GUI helper: run a script with Tools/python/pythonw.exe (no console).
rem Usage: catty_pythonw.bat Tools\create_project.py [args...]

set "CATTY_TOOLS=%~dp0"
set "CATTY_PYTHONW=%CATTY_TOOLS%python\pythonw.exe"
set "CATTY_PYTHON=%CATTY_TOOLS%python\python.exe"

if exist "%CATTY_PYTHONW%" (
	"%CATTY_PYTHONW%" %*
	exit /b %ERRORLEVEL%
)

if exist "%CATTY_PYTHON%" (
	echo [WARN] pythonw.exe not found; using python.exe
	"%CATTY_PYTHON%" %*
	exit /b %ERRORLEVEL%
)

echo [ERROR] Local Python not found:
echo         %CATTY_PYTHONW%
echo [ERROR] From the Catty engine root, run setup.bat first.
exit /b 1
