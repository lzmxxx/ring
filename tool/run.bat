@echo off
setlocal
cd /d "%~dp0"

title SpO2-Ring BLE Host Computer

set "PYTHON_EXE=D:\software\anaconda3\envs\pyqt\python.exe"

if exist "%PYTHON_EXE%" (
    "%PYTHON_EXE%" main.py
    goto :end
)

where conda >nul 2>nul
if %ERRORLEVEL% equ 0 (
    call conda run -n pyqt python main.py
    goto :end
)

echo [ERROR] Cannot find python at %PYTHON_EXE% or conda pyqt environment.
pause

:end
endlocal