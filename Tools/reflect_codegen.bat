@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Legacy name — UObject reflection moved to object_reflect_codegen.
echo [Maho] reflect_codegen.bat is deprecated; forwarding to object_reflect_codegen.bat
call "%~dp0object_reflect_codegen.bat" %*
exit /b %ERRORLEVEL%
