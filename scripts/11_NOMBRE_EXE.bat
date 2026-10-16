@echo off
title Fragile Dreams - nombre de FragileDreams.exe
rem Quita el nombre "The Last Story" del ejecutable del launcher. Cierra el launcher antes.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp011_nombre_exe.ps1"
pause
