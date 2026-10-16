#!/bin/bash
# Compila las piezas nativas de Linux: fd_logros (logros sin conexion, rcheevos v11.6.0) y pt_bridge (mando/ventanas).
# Uso: herramientas/build_linux.sh <carpeta_salida>
set -e
OUT="${1:-linux}"; mkdir -p "$OUT"
H="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
git clone -q --depth 1 --branch v11.6.0 https://github.com/RetroAchievements/rcheevos.git "$TMP/rc"
gcc -O2 -std=gnu11 -w -DRC_DISABLE_LUA -o "$OUT/fd_logros" "$H/fd_logros.c" "$TMP"/rc/src/rcheevos/*.c "$TMP/rc/src/rhash/md5.c" \
  "$TMP/rc/src/rc_compat.c" "$TMP/rc/src/rc_util.c" -I"$TMP/rc/include" -I"$TMP/rc/src" -lpthread -lm
gcc -O2 -o "$OUT/pt_bridge" "$H/pt_bridge.c" -lpthread
strip "$OUT/fd_logros" "$OUT/pt_bridge"
ls -la "$OUT"
