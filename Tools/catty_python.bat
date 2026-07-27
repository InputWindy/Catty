@echo off
setlocal EnableExtensions
rem Resolve Catty local Python (Tools/python) and run a script. Not on PATH.
rem Usage: catty_python.bat Tools\foo.py [args...]

set "CATTY_TOOLS=%~dp0"
set "CATTY_PYTHON=%CATTY_TOOLS%python\python.exe"

if not exist "%CATTY_PYTHON%" (
	echo [ERROR] Local Python not found:
	echo         %CATTY_PYTHON%
	echo [ERROR] From the Catty engine root, run setup.bat first.
	exit /b 1
)

"%CATTY_PYTHON%" %*
exit /b %ERRORLEVEL%
