@echo off
REM DibaVault flasher launcher (Windows) -- dibachain
setlocal
cd /d "%~dp0"
where py >nul 2>nul
if %errorlevel%==0 (
    py dibavault_flash.py %*
    goto :end
)
where python >nul 2>nul
if %errorlevel%==0 (
    python dibavault_flash.py %*
    goto :end
)
echo Python 3 is required. Install it from https://python.org (tick "Add to PATH").
pause
:end
