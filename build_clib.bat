@echo off
REM ===========================================================================
REM Build script for simple_vulkan C library
REM ===========================================================================
REM
REM Prerequisites:
REM   1. Vulkan SDK installed (download from https://vulkan.lunarg.com/sdk/home)
REM   2. VULKAN_SDK environment variable set (installer does this automatically)
REM   3. Visual Studio C++ Build Tools or Visual Studio with C++ workload
REM
REM Usage:
REM   1. Open "x64 Native Tools Command Prompt for VS 2022" (or your VS version)
REM   2. Navigate to D:\prod\simple_vulkan
REM   3. Run: build_clib.bat
REM
REM ===========================================================================

echo.
echo ============================================
echo   simple_vulkan C Library Build Script
echo ============================================
echo.

REM Check for VULKAN_SDK
if "%VULKAN_SDK%"=="" (
    echo ERROR: VULKAN_SDK environment variable not set.
    echo Please install the Vulkan SDK from https://vulkan.lunarg.com/sdk/home
    exit /b 1
)

echo Vulkan SDK: %VULKAN_SDK%
echo.

REM Check for cl compiler
where cl >nul 2>&1
if errorlevel 1 (
    echo ERROR: cl.exe not found in PATH.
    echo Please run this from "x64 Native Tools Command Prompt for VS 2022"
    echo Or set up your Visual Studio environment first.
    exit /b 1
)

cd /d "%~dp0Clib"

echo Compiling simple_vulkan.c...
cl /c /O2 /I"%VULKAN_SDK%\Include" simple_vulkan.c /Fosimple_vulkan.obj

if errorlevel 1 (
    echo ERROR: Compilation failed!
    exit /b 1
)

echo Creating simple_vulkan.lib...
lib /OUT:simple_vulkan.lib simple_vulkan.obj

if errorlevel 1 (
    echo ERROR: Library creation failed!
    exit /b 1
)

echo.
echo ============================================
echo   Build successful!
echo ============================================
echo.
echo Created:
echo   - Clib\simple_vulkan.obj
echo   - Clib\simple_vulkan.lib
echo.
echo You can now compile Eiffel projects that use simple_vulkan.
echo.

REM Clean up obj file (optional)
REM del simple_vulkan.obj

exit /b 0
