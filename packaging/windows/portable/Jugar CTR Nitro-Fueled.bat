@echo off
rem CTR Nitro-Fueled PC - build portable para Windows (sin MSYS2).
rem El juego mergeado va en .\CUSA13795-1.21 (o define BB_GAME_DIR).
rem Menu del port dentro del juego: tecla Insert o L3+R3 en el mando.
setlocal
cd /d "%~dp0"
if "%BB_GAME_DIR%"=="" set "BB_GAME_DIR=%~dp0CUSA13795-1.21"
where py >nul 2>nul
if %errorlevel%==0 (set "PY=py -3") else (set "PY=python")
%PY% run.py %*
if %errorlevel% neq 0 (
    echo.
    echo El juego se cerro con un error. Revisa last_run.log y LEEME.txt.
    pause
)
