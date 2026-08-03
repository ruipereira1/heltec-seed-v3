@echo off
rem  Clica duas vezes neste ficheiro para conferir a seed.
rem
rem  Existe porque abrir um terminal e escrever "python verify.py --dice ...
rem  --chip ... --commit ..." e' um degrau que trava a maior parte das pessoas
rem  -- e quem nao verifica fica exatamente na situacao da Coldcard Mk3: a
rem  confiar num aparelho sem ter como o contestar.

cd /d "%~dp0"
title Conferir a seed - HELTEC-SEED-V3

set PY=
where py >nul 2>nul && set PY=py -3
if "%PY%"=="" ( where python >nul 2>nul && set PY=python )

if "%PY%"=="" (
    echo.
    echo   Nao encontrei o Python neste computador.
    echo.
    echo   Instala-o a partir de python.org, marca a caixa que diz
    echo   "Add Python to PATH", e volta a clicar neste ficheiro.
    echo.
    pause
    exit /b 1
)

%PY% verify.py
echo.
echo   ------------------------------------------------------------
echo   Acabou. Podes fechar esta janela.
echo   ------------------------------------------------------------
echo.
pause
