@echo off
setlocal EnableExtensions EnableDelayedExpansion

chcp 65001 >nul
cd /d "%~dp0"

set "APP_NAME=wvd"
set "BUILD_VENV=.venv-build"
set "REQUIREMENTS_FILE=requirements-build.txt"
set "CONFIG_BACKUP=%TEMP%\%APP_NAME%_config_backup_%RANDOM%_%RANDOM%.json"
set "CONFIG_WAS_BACKED_UP=0"
set "FINAL_DIST=dist\%APP_NAME%"
set "PYI_DIST=%TEMP%\%APP_NAME%_pyinstaller_dist_%RANDOM%_%RANDOM%"
set "PYI_BUILD=%TEMP%\%APP_NAME%_pyinstaller_build_%RANDOM%_%RANDOM%"

echo [INFO] Project dir: %CD%

rem Only the executable in this output directory can block this build.
powershell -NoProfile -Command "$target = [IO.Path]::GetFullPath('dist\wvd\wvd.exe'); if (Get-Process wvd -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $target }) { Write-Error 'Close dist\wvd\wvd.exe before packaging.'; exit 1 }"
if errorlevel 1 goto :fail

if not exist "requirements.txt" (
    echo [ERROR] requirements.txt not found. Run this script from the project root.
    goto :fail
)

if not exist "%REQUIREMENTS_FILE%" set "REQUIREMENTS_FILE=requirements.txt"

if not exist "%BUILD_VENV%\Scripts\python.exe" (
    echo [INFO] Creating local packaging venv: %BUILD_VENV%
    py -3.10 -m venv "%BUILD_VENV%" 2>nul
    if errorlevel 1 (
        echo [WARN] Python 3.10 not found. Trying Python 3.12...
        py -3.12 -m venv "%BUILD_VENV%" 2>nul
    )
    if errorlevel 1 (
        echo [WARN] py launcher failed. Trying current python...
        python -m venv "%BUILD_VENV%"
    )
    if errorlevel 1 (
        echo [ERROR] Failed to create venv. Install Python 3.10 or 3.12 and retry.
        goto :fail
    )
)

call "%BUILD_VENV%\Scripts\activate.bat"
if errorlevel 1 (
    echo [ERROR] Failed to activate local packaging venv.
    goto :fail
)

python -c "import sys; print('[INFO] Python:', sys.executable); print('[INFO] Version:', sys.version)"
if errorlevel 1 goto :fail

echo [INFO] Installing/updating requirements from %REQUIREMENTS_FILE%...
python -m pip install --disable-pip-version-check -r "%REQUIREMENTS_FILE%"
if errorlevel 1 (
    echo [ERROR] Failed to install requirements.
    goto :fail
)

echo [INFO] Compiling locale catalogs...
python -m babel.messages.frontend compile -d locale -D messages
if errorlevel 1 (
    echo [ERROR] Failed to compile locale catalogs.
    goto :fail
)

echo [INFO] Preserving runtime files while building in a temporary directory...
if exist "%FINAL_DIST%\config.json" (
    echo [INFO] Preserving %FINAL_DIST%\config.json
    copy /y "%FINAL_DIST%\config.json" "%CONFIG_BACKUP%" >nul
    if errorlevel 1 (
        echo [ERROR] Failed to back up %FINAL_DIST%\config.json.
        goto :fail
    )
    set "CONFIG_WAS_BACKED_UP=1"
)

if not exist "dist" mkdir "dist"
if not exist "%FINAL_DIST%" mkdir "%FINAL_DIST%"

echo [INFO] Running PyInstaller...
python -m PyInstaller ^
    --noconfirm ^
    --clean ^
    --onedir ^
    --name "%APP_NAME%" ^
    --distpath "%PYI_DIST%" ^
    --workpath "%PYI_BUILD%" ^
    --specpath "%PYI_BUILD%" ^
    --paths "%CD%\src" ^
    --add-data "%CD%\resources;resources" ^
    --add-data "%CD%\locale;locale" ^
    --add-data "%CD%\CHANGES_LOG.md;." ^
    --exclude-module "torch" ^
    --exclude-module "pandas" ^
    --exclude-module "matplotlib" ^
    --exclude-module "numba" ^
    --exclude-module "llvmlite" ^
    --exclude-module "openpyxl" ^
    "src\main.py"
if errorlevel 1 (
    echo [ERROR] PyInstaller failed.
    goto :fail
)

if not exist "%PYI_DIST%\%APP_NAME%\%APP_NAME%.exe" (
    echo [ERROR] PyInstaller output is missing: %PYI_DIST%\%APP_NAME%\%APP_NAME%.exe
    goto :fail
)

echo [INFO] Copying build output to %FINAL_DIST%...
rem Delete only generated libraries, after a successful build and an exact path check.
powershell -NoProfile -Command "$ErrorActionPreference = 'Stop'; $root = (Get-Location).Path; $target = [IO.Path]::GetFullPath('dist\wvd\_internal'); if ($target -ne (Join-Path $root 'dist\wvd\_internal')) { throw 'Invalid output path' }; foreach ($p in @('dist','dist\wvd',$target)) { if ((Test-Path -LiteralPath $p) -and ((Get-Item -LiteralPath $p).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Refusing linked output directory' } }; if (Get-Process wvd -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq (Join-Path $root 'dist\wvd\wvd.exe') }) { throw 'Output executable is running' }; if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }"
if errorlevel 1 goto :fail
robocopy "%PYI_DIST%\%APP_NAME%" "%FINAL_DIST%" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo [ERROR] Failed to copy build output to %FINAL_DIST%.
    goto :fail
)

if "%CONFIG_WAS_BACKED_UP%"=="1" (
    del /q "%CONFIG_BACKUP%" >nul 2>nul
    echo [INFO] Preserved %FINAL_DIST%\config.json
)

call :cleanup_temp

echo [INFO] Build completed: %FINAL_DIST%\%APP_NAME%.exe
goto :done

:fail
if "%CONFIG_WAS_BACKED_UP%"=="1" (
    if not exist "%FINAL_DIST%" mkdir "%FINAL_DIST%"
    if not exist "%FINAL_DIST%\config.json" copy /y "%CONFIG_BACKUP%" "%FINAL_DIST%\config.json" >nul
    del /q "%CONFIG_BACKUP%" >nul 2>nul
    echo [INFO] Restored %FINAL_DIST%\config.json
)
call :cleanup_temp
echo [INFO] Script failed.
if /i not "%NO_PAUSE%"=="1" pause
exit /b 1

:done
if /i not "%NO_PAUSE%"=="1" pause
exit /b 0

:cleanup_temp
rem Each temporary target must be a direct child of TEMP with our build prefix.
powershell -NoProfile -Command "$ErrorActionPreference = 'Stop'; $tempRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\'); foreach ($value in @($env:PYI_DIST, $env:PYI_BUILD)) { $p = [IO.Path]::GetFullPath($value); if ([IO.Path]::GetDirectoryName($p) -ne $tempRoot -or [IO.Path]::GetFileName($p) -notlike 'wvd_pyinstaller_*') { throw 'Invalid temporary path' }; if (Test-Path -LiteralPath $p) { if ((Get-Item -LiteralPath $p).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Refusing linked temporary directory' }; Remove-Item -LiteralPath $p -Recurse -Force } }"
exit /b %errorlevel%
