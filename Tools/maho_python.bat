@echo off
setlocal EnableExtensions
rem Resolve Maho local Python (Tools/python) and run a script. Never uses system Python.
rem Usage: maho_python.bat Tools\foo.py [args...]

set "MAHO_TOOLS=%~dp0"
set "MAHO_PYTHON=%MAHO_TOOLS%python\python.exe"

if not exist "%MAHO_PYTHON%" (
	echo [ERROR] Local Python not found:
	echo         %MAHO_PYTHON%
	echo [ERROR] From the Maho engine root, run setup.bat first.
	echo [ERROR] Do not use a system-installed Python for Maho tools.
	exit /b 1
)

rem Pin interpreter explicitly; never fall back to `python` on PATH.
"%MAHO_PYTHON%" %*
exit /b %ERRORLEVEL%
