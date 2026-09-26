@echo off
rem FuhrerOS launcher for Windows: forwards to the bash scripts inside WSL.
rem   fuhreros build ^| run ^| test ^| benchmark ^| reset ^| ssh ^| debug
setlocal
set CMD=%1
if "%CMD%"=="" set CMD=run
shift
set ARGS=
:loop
if "%1"=="" goto go
set ARGS=%ARGS% %1
shift
goto loop
:go
for /f "delims=" %%p in ('wsl.exe -d Ubuntu -- wslpath -a "%~dp0."') do set REPO=%%p
wsl.exe -d Ubuntu -- bash "%REPO%/scripts/%CMD%.sh" %ARGS%
