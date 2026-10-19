@echo off
rem Build OpenGothic (x64, RelWithDebInfo) into build\.
setlocal
for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath`) do set VSDIR=%%i
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
for /f "delims=" %%v in ('dir /b /ad /o-n "C:\VulkanSDK"') do if not defined VULKAN_SDK set VULKAN_SDK=C:\VulkanSDK\%%v
set PATH=%VULKAN_SDK%\Bin;C:\Program Files\Git\cmd;%PATH%
cd /d "%~dp0"
rem Always reconfigure: CMakeLists collects sources with GLOB, new files need a reconfigure.
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_SHARED_LIBS=ON >nul || exit /b 1
cmake --build build --target Gothic2Notr || exit /b 1
echo.
echo Done: %~dp0build\opengothic\Gothic2Notr.exe
