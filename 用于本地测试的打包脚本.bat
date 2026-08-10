@echo off
setlocal EnableExtensions

chcp 65001 >nul
cd /d "%~dp0"

set "APP_NAME=wvd"
set "BUILD_VENV=.venv-build"
set "REQUIREMENTS_FILE=requirements-build.txt"

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
if exist "dist" rd /s /q "dist"
if exist "build" rd /s /q "build"
if exist "%APP_NAME%.spec" del /q "%APP_NAME%.spec"

echo [INFO] Running PyInstaller...
python -m PyInstaller ^
    --noconfirm ^
    --clean ^
    --onedir ^
    --name "%APP_NAME%" ^
    --paths "src" ^
    --add-data "resources;resources" ^
    --add-data "locale;locale" ^
    --add-data "CHANGES_LOG.md;." ^
    --hidden-import "pkg_resources" ^
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

if exist "CHANGES_LOG.md" copy /y "CHANGES_LOG.md" "dist\%APP_NAME%\" >nul

echo [INFO] Build completed: dist\%APP_NAME%\%APP_NAME%.exe
goto :done

:fail
echo [INFO] Script failed.
if /i not "%NO_PAUSE%"=="1" pause
exit /b 1

:done
if /i not "%NO_PAUSE%"=="1" pause
exit /b 0
