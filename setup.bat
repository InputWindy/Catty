@echo off
setlocal EnableExtensions EnableDelayedExpansion
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
set "ERR=0"
set "STATUS=OK"
set "ALREADY=0"

if /I "%~1"=="--force" (
	echo [Catty] --force: removing existing install...
	echo         %PY_DIR%
	if exist "%PY_DIR%" rmdir /s /q "%PY_DIR%"
)

if exist "%PY_EXE%" (
	"%PY_EXE%" -c "import tkinter, sys; print(sys.version.split()[0])" 2>nul
	if not errorlevel 1 (
		set "ALREADY=1"
		goto :success
	)
	echo [Catty] Existing Tools\python looks broken ^(python.exe present but tkinter check failed^).
	echo [Catty] Removing and reinstalling...
	if exist "%PY_DIR%" rmdir /s /q "%PY_DIR%"
)

if not exist "%CACHE%" mkdir "%CACHE%"
if errorlevel 1 (
	set "STATUS=FAILED"
	set "ERR=1"
	echo.
	echo ======================================================================
	echo  [FAILED] Could not create cache directory:
	echo           %CACHE%
	echo  Reason : mkdir failed ^(check permissions / disk space^).
	echo ======================================================================
	goto :finish
)

if not exist "%INSTALLER%" (
	echo [Catty] Downloading Python %PY_VER% installer...
	echo         %URL%
	echo         -^> %INSTALLER%
	where curl >nul 2>&1
	if errorlevel 1 (
		powershell -NoProfile -ExecutionPolicy Bypass -Command ^
			"Invoke-WebRequest -Uri '%URL%' -OutFile '%INSTALLER%'"
	) else (
		curl.exe -L --fail -o "%INSTALLER%" "%URL%"
	)
	if errorlevel 1 (
		set "STATUS=FAILED"
		set "ERR=1"
		echo.
		echo ======================================================================
		echo  [FAILED] Download Python installer failed.
		echo  URL    : %URL%
		echo  Target : %INSTALLER%
		echo  Reason : curl/powershell exited with error ^(network / firewall / URL^).
		echo  Tip    : Check network, then re-run setup.bat
		echo ======================================================================
		goto :finish
	)
	if not exist "%INSTALLER%" (
		set "STATUS=FAILED"
		set "ERR=1"
		echo.
		echo ======================================================================
		echo  [FAILED] Download reported OK but installer file is missing.
		echo  Expected: %INSTALLER%
		echo ======================================================================
		goto :finish
	)
	echo [Catty] Download OK.
)

echo [Catty] Installing Python %PY_VER% into:
echo         %PY_DIR%
echo [Catty] Options: PrependPath=0 ^(will NOT modify system PATH^), Include_tcltk=1, Include_pip=1
"%INSTALLER%" /quiet InstallAllUsers=0 TargetDir="%PY_DIR%" PrependPath=0 Include_launcher=0 Include_test=0 Include_doc=0 Shortcuts=0 AssociateFiles=0 Include_pip=1 Include_tcltk=1
set "INST_EC=%ERRORLEVEL%"
if not "%INST_EC%"=="0" (
	set "STATUS=FAILED"
	set "ERR=1"
	echo.
	echo ======================================================================
	echo  [FAILED] Python installer returned error code %INST_EC%.
	echo  Installer: %INSTALLER%
	echo  Target   : %PY_DIR%
	echo  Reason   : Silent install failed ^(permissions, antivirus, or corrupt installer^).
	echo  Tip      : Delete Tools\_cache and re-run setup.bat --force
	echo             Or run the installer EXE manually to see its UI error.
	echo ======================================================================
	goto :finish
)

if not exist "%PY_EXE%" (
	set "STATUS=FAILED"
	set "ERR=1"
	echo.
	echo ======================================================================
	echo  [FAILED] Installer exited 0 but python.exe was not created.
	echo  Expected: %PY_EXE%
	echo  Reason  : TargetDir may be wrong, or install was blocked after exit.
	echo  Tip     : Re-run setup.bat --force ; check folder permissions.
	echo ======================================================================
	goto :finish
)

echo catty-local> "%PY_DIR%\.catty_managed"

"%PY_EXE%" -c "import tkinter, sys; print(sys.version.split()[0])" 2>nul
if errorlevel 1 (
	set "STATUS=FAILED"
	set "ERR=1"
	echo.
	echo ======================================================================
	echo  [FAILED] python.exe exists but verification failed.
	echo  Path   : %PY_EXE%
	echo  Reason : Cannot import tkinter ^(Tcl/Tk missing or broken install^).
	echo           createProject.bat / package UI need tkinter.
	echo  Tip    : setup.bat --force
	echo ======================================================================
	goto :finish
)

:success
echo.
echo ======================================================================
if "!ALREADY!"=="1" (
	echo  [SUCCESS] Catty local Python is already installed and verified.
) else (
	echo  [SUCCESS] Catty local Python installed and verified successfully.
)
echo ----------------------------------------------------------------------
echo  Version : Python %PY_VER%  ^(tkinter OK^)
echo  Path    : %PY_EXE%
echo  Note    : Private to this engine — NOT added to system PATH.
echo  Next    : Run createProject.bat to create a game project.
echo ======================================================================
set "STATUS=OK"
set "ERR=0"

:finish
echo.
if "!STATUS!"=="FAILED" (
	echo [Catty] Setup did NOT succeed. See [FAILED] details above.
) else (
	echo [Catty] Setup finished successfully. You can close this window.
)
pause
exit /b !ERR!
