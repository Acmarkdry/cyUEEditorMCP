@echo off
REM ============================================================================
REM  UECliTool - One-Click Python Setup
REM  Automatically finds UE engine's built-in Python, creates a venv,
REM  and installs the MCP package. No external Python installation required.
REM ============================================================================

setlocal EnableDelayedExpansion

set "PLUGIN_DIR=%~dp0"
set "PYTHON_DIR=%PLUGIN_DIR%Python"
set "VENV_DIR=%PYTHON_DIR%\.venv"
REM ProjectRoot = go up two levels from Plugins/UEEditorMCP
for %%I in ("%PLUGIN_DIR%..\..\") do set "PROJECT_ROOT=%%~fI"

echo ============================================
echo  UECliTool - Python Environment Setup
echo ============================================
echo.

REM --- Step 1: Find UE Engine Python ---
echo [1/4] Searching for Unreal Engine Python...

set "UE_PYTHON="

REM --- Priority 1: Command-line argument ---
if not "%~1"=="" (
    if exist "%~1\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" (
        set "UE_PYTHON=%~1\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
        echo   [param] !UE_PYTHON!
        goto :found_python
    )
)

REM --- Priority 2: Read .uproject EngineAssociation → Registry ---
REM Find .uproject file
for %%F in ("%PROJECT_ROOT%\*.uproject") do (
    REM Extract EngineAssociation value (simple text parsing)
    for /f "tokens=2 delims=:," %%A in ('findstr /i "EngineAssociation" "%%F" 2^>nul') do (
        set "ENGINE_VER=%%~A"
        REM Trim quotes and spaces
        set "ENGINE_VER=!ENGINE_VER: =!"
        set "ENGINE_VER=!ENGINE_VER:"=!"
        if not "!ENGINE_VER!"=="" (
            echo   Found EngineAssociation: !ENGINE_VER!
            REM Check HKLM registry
            for /f "tokens=2*" %%R in ('reg query "HKLM\SOFTWARE\EpicGames\Unreal Engine\!ENGINE_VER!" /v InstalledDirectory 2^>nul') do (
                set "REG_DIR=%%S"
                if exist "!REG_DIR!\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" (
                    set "UE_PYTHON=!REG_DIR!\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
                    echo   [registry] !UE_PYTHON!
                    goto :found_python
                )
            )
        )
    )
)

REM --- Priority 3: UE_ENGINE_DIR environment variable ---
if defined UE_ENGINE_DIR (
    if exist "%UE_ENGINE_DIR%\Binaries\ThirdParty\Python3\Win64\python.exe" (
        set "UE_PYTHON=%UE_ENGINE_DIR%\Binaries\ThirdParty\Python3\Win64\python.exe"
        echo   [env] !UE_PYTHON!
        goto :found_python
    )
)

REM --- Priority 4: Scan common disk locations ---
for %%D in (C D E F) do (
    for %%P in (
        "%%D:\EpicGame\UE_5.7"
        "%%D:\EpicGame\UE_5.6"
        "%%D:\EpicGame\UE_5.5"
        "%%D:\Program Files\Epic Games\UE_5.7"
        "%%D:\Program Files\Epic Games\UE_5.6"
        "%%D:\Program Files\Epic Games\UE_5.5"
        "%%D:\UnrealEngine\UE_5.7"
    ) do (
        if exist %%~P\Engine\Binaries\ThirdParty\Python3\Win64\python.exe (
            set "UE_PYTHON=%%~P\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
            echo   [disk scan] !UE_PYTHON!
            goto :found_python
        )
    )
)

REM --- Priority 5: Ask user ---
echo.
echo   Could not auto-detect UE Engine Python.
echo   Please enter the full path to your UE engine root directory
echo   (e.g., E:\EpicGame\UE_5.7)
echo.
set /p "ENGINE_ROOT=  Engine root path: "
if exist "!ENGINE_ROOT!\Engine\Binaries\ThirdParty\Python3\Win64\python.exe" (
    set "UE_PYTHON=!ENGINE_ROOT!\Engine\Binaries\ThirdParty\Python3\Win64\python.exe"
    echo   Found UE Python: !UE_PYTHON!
    goto :found_python
)

echo.
echo   ERROR: Could not find python.exe at:
echo   !ENGINE_ROOT!\Engine\Binaries\ThirdParty\Python3\Win64\python.exe
pause
exit /b 1

:found_python

REM Show Python version
echo.
for /f "delims=" %%v in ('"!UE_PYTHON!" --version 2^>^&1') do echo   Python version: %%v
echo.

REM --- Write ue_mcp_config.yaml ---
REM Derive engine root from UE_PYTHON path or command-line argument.
REM UE Python lives at: <EngineRoot>\Engine\Binaries\ThirdParty\Python3\Win64\python.exe
if not "%~1"=="" (
    set "DETECTED_ENGINE_ROOT=%~1"
) else (
    for %%P in ("!UE_PYTHON!") do set "_P1=%%~dpP"
    for %%P in ("!_P1!..") do set "_P2=%%~fP"
    for %%P in ("!_P2!..") do set "_P3=%%~fP"
    for %%P in ("!_P3!..") do set "_P4=%%~fP"
    for %%P in ("!_P4!..") do set "_P5=%%~fP"
    for %%P in ("!_P5!..") do set "DETECTED_ENGINE_ROOT=%%~fP"
)
REM Remove trailing backslash if present
if "!DETECTED_ENGINE_ROOT:~-1!"=="\" set "DETECTED_ENGINE_ROOT=!DETECTED_ENGINE_ROOT:~0,-1!"

REM Convert backslashes to forward slashes
set "ENGINE_ROOT_FWD=!DETECTED_ENGINE_ROOT:\=/!"
set "PROJECT_ROOT_CLEAN=%PROJECT_ROOT%"
if "!PROJECT_ROOT_CLEAN:~-1!"=="\" set "PROJECT_ROOT_CLEAN=!PROJECT_ROOT_CLEAN:~0,-1!"
set "PROJECT_ROOT_FWD=!PROJECT_ROOT_CLEAN:\=/!"

set "CONFIG_PATH=%PROJECT_ROOT%ue_mcp_config.yaml"

if exist "!CONFIG_PATH!" (
    echo   Merging into existing !CONFIG_PATH! ...
    REM Read existing file, update engine_root and project_root, preserve other keys
    set "FOUND_ENGINE_ROOT=0"
    set "FOUND_PROJECT_ROOT=0"
    set "TMPCONFIG=!CONFIG_PATH!.tmp"
    REM Clear temp file
    type nul > "!TMPCONFIG!"
    for /f "usebackq delims=" %%L in ("!CONFIG_PATH!") do (
        set "LINE=%%L"
        REM Check if line starts with engine_root:
        echo !LINE! | findstr /b /c:"engine_root:" >nul 2>&1
        if !ERRORLEVEL! equ 0 (
            >> "!TMPCONFIG!" echo engine_root: !ENGINE_ROOT_FWD!
            set "FOUND_ENGINE_ROOT=1"
        ) else (
            REM Check if line starts with project_root:
            echo !LINE! | findstr /b /c:"project_root:" >nul 2>&1
            if !ERRORLEVEL! equ 0 (
                >> "!TMPCONFIG!" echo project_root: !PROJECT_ROOT_FWD!
                set "FOUND_PROJECT_ROOT=1"
            ) else (
                >> "!TMPCONFIG!" echo !LINE!
            )
        )
    )
    REM Append any keys that were not found in the existing file
    if "!FOUND_ENGINE_ROOT!"=="0" (
        >> "!TMPCONFIG!" echo engine_root: !ENGINE_ROOT_FWD!
    )
    if "!FOUND_PROJECT_ROOT!"=="0" (
        >> "!TMPCONFIG!" echo project_root: !PROJECT_ROOT_FWD!
    )
    move /y "!TMPCONFIG!" "!CONFIG_PATH!" >nul
) else (
    echo   Creating !CONFIG_PATH! ...
    > "!CONFIG_PATH!" echo engine_root: !ENGINE_ROOT_FWD!
    >> "!CONFIG_PATH!" echo project_root: !PROJECT_ROOT_FWD!
)
echo   Config written: !CONFIG_PATH!
echo.

REM --- Step 2: Create virtual environment ---
echo [2/4] Creating virtual environment...

if exist "%VENV_DIR%" (
    echo   Removing existing venv...
    rmdir /s /q "%VENV_DIR%"
)

"!UE_PYTHON!" -m venv "%VENV_DIR%"
if %ERRORLEVEL% neq 0 (
    echo   ERROR: Failed to create virtual environment.
    pause
    exit /b 1
)
echo   Created: %VENV_DIR%
echo.

REM --- Step 3: Install dependencies ---
echo [3/4] Installing MCP package...

"%VENV_DIR%\Scripts\pip.exe" install -r "%PYTHON_DIR%\requirements.txt" --quiet
if %ERRORLEVEL% neq 0 (
    echo   ERROR: Failed to install dependencies.
    pause
    exit /b 1
)
echo   Dependencies installed successfully.
echo.

REM --- Step 4: Generate mcp.json ---
echo [4/4] Generating mcp.json...
echo.
echo  Generating .vscode/mcp.json...

set "VSCODE_DIR=%PROJECT_ROOT%\.vscode"
if not exist "%VSCODE_DIR%" mkdir "%VSCODE_DIR%"

set "MCP_JSON=%VSCODE_DIR%\mcp.json"
set "VP=%VENV_DIR:\=/%"
set "PP=%PYTHON_DIR:\=/%"

>  "%MCP_JSON%" echo {
>> "%MCP_JSON%" echo   "servers": {
>> "%MCP_JSON%" echo     "ue-cli-tool": {
>> "%MCP_JSON%" echo       "command": "%VP%/Scripts/python.exe",
>> "%MCP_JSON%" echo       "args": ["-m", "ue_cli_tool.server"],
>> "%MCP_JSON%" echo       "env": { "PYTHONPATH": "%PP%" }
>> "%MCP_JSON%" echo     }
>> "%MCP_JSON%" echo   }
>> "%MCP_JSON%" echo }

echo   Generated: %MCP_JSON%
echo.
echo ============================================
echo  Done! No external Python installation needed.
echo  Configured server: ue-cli-tool
echo ============================================

pause
