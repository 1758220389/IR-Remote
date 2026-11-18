@echo off
setlocal
cd /d "%~dp0"
if not exist ".venv\Scripts\pythonw.exe" (
    echo Missing existing Python environment: ml\.venv
    pause
    exit /b 1
)
start "" ".venv\Scripts\pythonw.exe" "scripts\capture_gui.py" %*
