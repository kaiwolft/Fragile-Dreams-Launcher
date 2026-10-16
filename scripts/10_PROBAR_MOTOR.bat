@echo off
title Fragile Dreams - prueba del motor
rem Prueba fd_motor.exe (core de Dolphin dentro de nuestro propio programa) sin pasar por el launcher.
rem Usa el main.dol que preparo el launcher la ultima vez (texto espanol), las partidas de Dolphin-x64\User
rem y escribe todo en motor\fd_motor.log. Cierra la ventana o pulsa Alt+F4 para salir.
pushd "%~dp0.."
set "R=%CD%"
if not exist "%R%\juego\EU_ESPANOL\DATA\sys\main.dol" (
  echo No encuentro juego\EU_ESPANOL\DATA\sys\main.dol. Inicia el juego una vez desde el launcher y vuelve a probar.
  pause & exit /b 1
)
"%R%\motor\fd_motor.exe" --core "%R%\core\dolphin_libretro.dll" --game "%R%\juego\EU_ESPANOL\DATA\sys\main.dol" --system "%R%\core\system" --save "%R%\Dolphin-x64" --opts "%R%\motor\opciones_prueba.txt" --log "%R%\motor\fd_motor.log" --fullscreen 0 --width 1280 --height 720
echo.
echo El motor termino (codigo %ERRORLEVEL%). Avisale a Claude para que revise motor\fd_motor.log
popd
pause
