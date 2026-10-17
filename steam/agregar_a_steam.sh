#!/bin/sh
# Agregar a Steam (Linux / Steam Deck) / Add to Steam (Linux / Steam Deck)
cd "$(dirname "$(readlink -f "$0")")" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
  echo "Falta python3 / python3 is required (sudo apt install python3)"; exit 1
fi
python3 agregar_a_steam.py "$@"
status=$?
if [ -t 0 ]; then printf '\nPulsa Enter para cerrar / Press Enter to close'; read -r _; fi
exit $status
