#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
pio_bin="$project_dir/tools/venv/bin/pio"
if [ ! -x "$pio_bin" ]; then
  printf 'Run make setup first.\n' >&2
  exit 1
fi
# PlatformIO flock fails on NFS without locks (ENOLCK); keep lockable state on local disk
pio_local="${XDG_CACHE_HOME:-$HOME/.cache}/ESPsette/pio"
# Per-worktree workspace so libdeps/build locks stay local and do not collide
pio_ws="$pio_local/ws/$(basename -- "$project_dir")"
mkdir -p "$pio_local" "$pio_ws"
export PLATFORMIO_CORE_DIR="$pio_local/core"
export PLATFORMIO_CACHE_DIR="$pio_local/cache"
export PLATFORMIO_BUILD_CACHE_DIR="$pio_local/build-cache"
export PLATFORMIO_WORKSPACE_DIR="$pio_ws"
export ESPSETTE_OTA_PASSWORD="${ESPSETTE_OTA_PASSWORD:-}"
# DTR-only reset for CH340 without RTS (see esptool.cfg)
export ESPTOOL_CFGFILE="${ESPTOOL_CFGFILE:-$project_dir/esptool.cfg}"
# Unbuffered so progress shows before PlatformIO finishes starting
export PYTHONUNBUFFERED=1
printf 'pio: core=%s\n' "$PLATFORMIO_CORE_DIR" >&2
printf 'pio: workspace=%s\n' "$PLATFORMIO_WORKSPACE_DIR" >&2
printf 'pio: starting %s %s\n' "$pio_bin" "$*" >&2
cd "$project_dir"
exec "$pio_bin" "$@"
