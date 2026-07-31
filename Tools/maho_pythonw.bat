@echo off
setlocal EnableExtensions
rem GUI helper: run a script with Tools/python/pythonw.exe only (never system Python).
rem Usage: maho_pythonw.bat Tools\create_project.py [args...]

set "MAHO_TOOLS=%~dp0"
set "MAHO_PYTHONW=%MAHO_TOOLS%python\pythonw.exe"
set "MAHO_PYTHON=%MAHO_TOOLS%python\python.exe"

if exist "%MAHO_PYTHONW%" (
	"%MAHO_PYTHONW%" %*
	exit /b %ERRORLEVEL%
)

if exist "%MAHO_PYTHON%" (
	echo [WARN] pythonw.exe not found under Tools\python; using local python.exe
	"%MAHO_PYTHON%" %*
	exit /b %ERRORLEVEL%
)

echo [ERROR] Local Python not found:
echo         %MAHO_PYTHONW%
echo [ERROR] From the Maho engine root, run setup.bat first.
echo [ERROR] Do not use a system-installed Python for Maho tools.
exit /b 1
