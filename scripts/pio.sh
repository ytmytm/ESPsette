#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
pio_bin="$project_dir/tools/venv/bin/pio"
if [ ! -x "$pio_bin" ]; then
  printf 'Run make setup first.\n' >&2
  exit 1
fi
export PLATFORMIO_CORE_DIR="$project_dir/tools/pio-core"
export PLATFORMIO_CACHE_DIR="$project_dir/tools/pio-cache"
export PLATFORMIO_BUILD_CACHE_DIR="$project_dir/tools/build-cache"
export ESPSETTE_OTA_PASSWORD="${ESPSETTE_OTA_PASSWORD:-}"
cd "$project_dir"
exec "$pio_bin" "$@"
