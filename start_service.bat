@echo off
start cmd /k "cd /d C:\self_drive\Cart-Dashboard && ..\self_drive_env\Scripts\activate && uvicorn server:app --host 0.0.0.0 --port 8000"
timeout /t 2 /nobreak >nul
start cmd /k "ngrok http 8000"
