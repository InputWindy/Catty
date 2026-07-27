@echo off
setlocal EnableExtensions
cd /d "%~dp0"

rem Bootstrap Catty local Python under Tools\python (not added to PATH).
rem Usage:
rem   setup.bat           install if missing
rem   setup.bat --force   wipe and reinstall

set "PY_DIR=%~dp0Tools\python"
set "PY_EXE=%PY_DIR%\python.exe"
set "CACHE=%~dp0Tools\_cache"
set "PY_VER=3.12.8"
set "INSTALLER=%CACHE%\python-%PY_VER%-amd64.exe"
set "URL=https://www.python.org/ftp/python/%PY_VER%/python-%PY_VER%-amd64.exe"

if /I "%~1"=="--force" (
	echo [Catty] --force: removing "%PY_DIR%"
	if exist "%PY_DIR%" rmdir /s /q "%PY_DIR%"
)

if exist "%PY_EXE%" (
	"%PY_EXE%" -c "import tkinter, sys; print('[Catty] Local Python', sys.version.split()[0], '(tkinter OK)')" 2>nul
	if not errorlevel 1 (
		echo [Catty] Ready: %PY_EXE%
		echo [Catty] This interpreter is private to Tools\; root bats call it via Tools\catty_python.bat
		exit /b 0
	)
	echo [Catty] Existing install looks broken; reinstalling...
	if exist "%PY_DIR%" rmdir /s /q "%PY_DIR%"
)

if not exist "%CACHE%" mkdir "%CACHE%"

if not exist "%INSTALLER%" (
	echo [Catty] Downloading Python %PY_VER% ...
	echo         %URL%
	where curl >nul 2>&1
	if errorlevel 1 (
		powershell -NoProfile -ExecutionPolicy Bypass -Command ^
			"Invoke-WebRequest -Uri '%URL%' -OutFile '%INSTALLER%'"
	) else (
		curl.exe -L --fail -o "%INSTALLER%" "%URL%"
	)
	if errorlevel 1 (
		echo [ERROR] Download failed.
		exit /b 1
	)
	if not exist "%INSTALLER%" (
		echo [ERROR] Installer missing after download: %INSTALLER%
		exit /b 1
	)
)

echo [Catty] Installing Python %PY_VER% → Tools\python
echo [Catty] ^(PrependPath=0 — will not modify your system PATH^)
"%INSTALLER%" /quiet InstallAllUsers=0 TargetDir="%PY_DIR%" PrependPath=0 Include_launcher=0 Include_test=0 Include_doc=0 Shortcuts=0 AssociateFiles=0 Include_pip=1 Include_tcltk=1

if not exist "%PY_EXE%" (
	echo [ERROR] Install finished but python.exe not found:
	echo         %PY_EXE%
	exit /b 1
)

echo catty-local> "%PY_DIR%\.catty_managed"

"%PY_EXE%" -c "import tkinter, sys; print('[Catty] Verified', sys.version.split()[0], 'tkinter OK')"
if errorlevel 1 (
	echo [ERROR] tkinter import failed — GUI tools need Tcl/Tk. Re-run: setup.bat --force
	exit /b 1
)

echo [Catty] Setup complete.
echo [Catty] Next: createProject.bat  ^|  generateProject.bat  ^|  package.bat
exit /b 0
