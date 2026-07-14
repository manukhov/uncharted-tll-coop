@echo off
setlocal
for /f "usebackq delims=" %%i in (`"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSPATH=%%i
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0..\scanner\build"
cmake --build . --target tll_teleport_hook
exit /b %ERRORLEVEL%