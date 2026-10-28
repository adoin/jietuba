@echo off
setlocal
for /f "delims=" %%i in ('python -c "import json; print(json.load(open(r'%~dp0..\branding.json', encoding='utf-8'))['name'])"') do set "APP_BRAND=%%i"
if not defined APP_BRAND exit /b 1
if not exist "%~dp0dist\Release\%APP_BRAND%_cpp.exe" (
    echo Build first: build.cmd
    exit /b 1
)
start "" "%~dp0dist\Release\%APP_BRAND%_cpp.exe" %*
