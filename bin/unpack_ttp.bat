@echo off
setlocal
if "%~1"=="" (
    echo [TinyTask Pro] Drag and drop a .ttp file onto this batch script to unpack.
    pause
    exit /b 1
)
python "%~dp0..\src\pro\tinytask_tool.py" unpack "%~1"
pause

