@echo off
setlocal enabledelayedexpansion

REM Neonify - Install desktop shortcut, Start Menu entry, and PATH

set "NEONIFY_DIR=%~dp0"
set "NEONIFY_EXE=%NEONIFY_DIR%neonify.exe"

if not exist "%NEONIFY_EXE%" (
    echo ERROR: neonify.exe not found at %NEONIFY_EXE%
    pause
    exit /b 1
)

echo Creating desktop shortcut...
set "SHORTCUT_PATH=%USERPROFILE%\Desktop\Neonify.lnk"

powershell -NoProfile -Command "$w = New-Object -ComObject WScript.Shell; $s = $w.CreateShortcut('%SHORTCUT_PATH%'); $s.TargetPath = '%NEONIFY_EXE%'; $s.WorkingDirectory = '%NEONIFY_DIR%'; $s.IconLocation = '%NEONIFY_EXE%,0'; $s.Description = 'Neonify'; $s.Save()"

set "STARTMENU=%APPDATA%\Microsoft\Windows\Start Menu\Programs"
if not exist "%STARTMENU%\Neonify" mkdir "%STARTMENU%\Neonify"
set "SM_PATH=%STARTMENU%\Neonify\Neonify.lnk"

powershell -NoProfile -Command "$w = New-Object -ComObject WScript.Shell; $s = $w.CreateShortcut('%SM_PATH%'); $s.TargetPath = '%NEONIFY_EXE%'; $s.WorkingDirectory = '%NEONIFY_DIR%'; $s.IconLocation = '%NEONIFY_EXE%,0'; $s.Description = 'Neonify'; $s.Save()"

echo Adding Neonify to user PATH...
set "PATH_KEY=HKCU\Environment"
set "CURRENT_PATH="

for /f "tokens=2*" %%A in ('reg query "%PATH_KEY%" /v Path 2^>nul') do set "CURRENT_PATH=%%B"

echo %CURRENT_PATH% | findstr /I /C:"%NEONIFY_DIR%" >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo Neonify directory already in PATH - skipping
) else (
    if defined CURRENT_PATH (
        reg add "%PATH_KEY%" /v Path /t REG_EXPAND_SZ /d "%CURRENT_PATH%;%NEONIFY_DIR%" /f >nul 2>&1
    ) else (
        reg add "%PATH_KEY%" /v Path /t REG_EXPAND_SZ /d "%NEONIFY_DIR%" /f >nul 2>&1
    )
    echo Added to user PATH
)

echo.
echo ============================================================
echo  Neonify installed successfully!
echo ============================================================
echo.
echo  Desktop shortcut:  %SHORTCUT_PATH%
echo  Start Menu:        %SM_PATH%
echo  PATH:              %NEONIFY_DIR%
echo.
echo  Open a NEW terminal and run: neonify info
echo.
echo ============================================================
echo.
pause
