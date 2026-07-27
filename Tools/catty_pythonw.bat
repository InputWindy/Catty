@echo off
setlocal EnableExtensions
rem GUI helper: run a script with Tools/python/pythonw.exe only (never system Python).
rem Usage: catty_pythonw.bat Tools\create_project.py [args...]

set "CATTY_TOOLS=%~dp0"
set "CATTY_PYTHONW=%CATTY_TOOLS%python\pythonw.exe"
set "CATTY_PYTHON=%CATTY_TOOLS%python\python.exe"

if exist "%CATTY_PYTHONW%" (
	"%CATTY_PYTHONW%" %*
	exit /b %ERRORLEVEL%
)

if exist "%CATTY_PYTHON%" (
	echo [WARN] pythonw.exe not found under Tools\python; using local python.exe
	"%CATTY_PYTHON%" %*
	exit /b %ERRORLEVEL%
)

echo [ERROR] Local Python not found:
echo         %CATTY_PYTHONW%
echo [ERROR] From the Catty engine root, run setup.bat first.
echo [ERROR] Do not use a system-installed Python for Catty tools.
exit /b 1
