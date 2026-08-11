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

echo [INFO] Cleaning old build outputs...
if exist "%FINAL_DIST%\config.json" (
    echo [INFO] Preserving %FINAL_DIST%\config.json
    copy /y "%FINAL_DIST%\config.json" "%CONFIG_BACKUP%" >nul
    if errorlevel 1 (
        echo [ERROR] Failed to back up %FINAL_DIST%\config.json.
        goto :fail
    )
    set "CONFIG_WAS_BACKED_UP=1"
)

if exist "%PYI_DIST%" rd /s /q "%PYI_DIST%"
if exist "%PYI_BUILD%" rd /s /q "%PYI_BUILD%"
if exist "build" rd /s /q "build" >nul 2>nul
if exist "%APP_NAME%.spec" del /q "%APP_NAME%.spec" >nul 2>nul

if not exist "dist" mkdir "dist"
if not exist "%FINAL_DIST%" mkdir "%FINAL_DIST%"

for /f "delims=" %%I in ('dir /a /b "%FINAL_DIST%" 2^>nul') do (
    if /i not "%%I"=="config.json" (
        if exist "%FINAL_DIST%\%%I\*" (
            rd /s /q "%FINAL_DIST%\%%I" 2>nul
        ) else (
            del /f /q "%FINAL_DIST%\%%I" 2>nul
        )
    )
)

set "STALE_ITEM="
for /f "delims=" %%I in ('dir /a /b "%FINAL_DIST%" 2^>nul') do (
    if /i not "%%I"=="config.json" set "STALE_ITEM=%%I"
)
if defined STALE_ITEM (
    echo [ERROR] Failed to clean stale item in %FINAL_DIST%: !STALE_ITEM!
    echo [ERROR] Close any running %FINAL_DIST%\%APP_NAME%.exe windows and retry.
    goto :fail
)
if exist "build" (
    echo [WARN] Could not remove old local build directory. Using a temp PyInstaller work dir.
)
if exist "%APP_NAME%.spec" (
    echo [WARN] Could not remove old local spec file. Using a temp PyInstaller spec dir.
)

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
robocopy "%PYI_DIST%\%APP_NAME%" "%FINAL_DIST%" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (
    echo [ERROR] Failed to copy build output to %FINAL_DIST%.
    goto :fail
)

if "%CONFIG_WAS_BACKED_UP%"=="1" (
    copy /y "%CONFIG_BACKUP%" "%FINAL_DIST%\config.json" >nul
    del /q "%CONFIG_BACKUP%" >nul 2>nul
    echo [INFO] Restored %FINAL_DIST%\config.json
)

if exist "%PYI_DIST%" rd /s /q "%PYI_DIST%" >nul 2>nul
if exist "%PYI_BUILD%" rd /s /q "%PYI_BUILD%" >nul 2>nul

echo [INFO] Build completed: %FINAL_DIST%\%APP_NAME%.exe
goto :done

:fail
if "%CONFIG_WAS_BACKED_UP%"=="1" (
    if not exist "%FINAL_DIST%" mkdir "%FINAL_DIST%"
    copy /y "%CONFIG_BACKUP%" "%FINAL_DIST%\config.json" >nul
    del /q "%CONFIG_BACKUP%" >nul 2>nul
    echo [INFO] Restored %FINAL_DIST%\config.json
)
if exist "%PYI_DIST%" rd /s /q "%PYI_DIST%" >nul 2>nul
if exist "%PYI_BUILD%" rd /s /q "%PYI_BUILD%" >nul 2>nul
echo [INFO] Script failed.
if /i not "%NO_PAUSE%"=="1" pause
exit /b 1

:done
if /i not "%NO_PAUSE%"=="1" pause
exit /b 0
