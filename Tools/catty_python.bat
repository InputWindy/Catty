@echo off
setlocal EnableExtensions
rem Resolve Catty local Python (Tools/python) and run a script. Never uses system Python.
rem Usage: catty_python.bat Tools\foo.py [args...]

set "CATTY_TOOLS=%~dp0"
set "CATTY_PYTHON=%CATTY_TOOLS%python\python.exe"

if not exist "%CATTY_PYTHON%" (
	echo [ERROR] Local Python not found:
	echo         %CATTY_PYTHON%
	echo [ERROR] From the Catty engine root, run setup.bat first.
	echo [ERROR] Do not use a system-installed Python for Catty tools.
	exit /b 1
)

rem Pin interpreter explicitly; never fall back to `python` on PATH.
"%CATTY_PYTHON%" %*
exit /b %ERRORLEVEL%
