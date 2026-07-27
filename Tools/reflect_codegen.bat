@echo off
setlocal EnableExtensions
cd /d "%~dp0.."

rem Internal: regenerate CATTY_REFLECT_* catalogs. Not a user-facing root launcher.
call "%~dp0catty_python.bat" "%~dp0reflect_codegen.py" %*
exit /b %ERRORLEVEL%
