#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
espsette_project_dir=$project_dir
# shellcheck source=tools-env.sh
. "$project_dir/scripts/tools-env.sh"

mkdir -p "$project_dir/tools"
uv_bin="$project_dir/tools/bin/uv"
if ! "$uv_bin" --version >/dev/null 2>&1; then
  command -v curl >/dev/null 2>&1 || { printf 'curl is required for first setup.\n' >&2; exit 1; }
  curl -fsSL https://astral.sh/uv/install.sh -o "$project_dir/tools/install-uv.sh"
  UV_UNMANAGED_INSTALL="$project_dir/tools/bin" sh "$project_dir/tools/install-uv.sh"
fi
# uv flock fails on NFS without locks (ENOLCK); keep lockable state on local disk
uv_local="${XDG_CACHE_HOME:-$HOME/.cache}/ESPsette/uv"
mkdir -p "$uv_local"
export UV_CACHE_DIR="$uv_local/cache"
export UV_DATA_DIR="$uv_local/data"
export UV_PYTHON_INSTALL_DIR="$uv_local/python"
export UV_PYTHON_BIN_DIR="$uv_local/python-bin"
export UV_LINK_MODE=copy

"$uv_bin" python install 3.12
mkdir -p "$(dirname -- "$ESPSETTE_VENV")"
if ! "$ESPSETTE_VENV/bin/python" --version >/dev/null 2>&1 &&
   ! "$ESPSETTE_VENV/Scripts/python.exe" --version >/dev/null 2>&1; then
  rm -rf "$ESPSETTE_VENV"
fi
"$uv_bin" venv --allow-existing "$ESPSETTE_VENV" --python 3.12
# Refresh python path after create
# shellcheck source=tools-env.sh
. "$project_dir/scripts/tools-env.sh"
"$uv_bin" pip install --python "$ESPSETTE_PYTHON" -r "$project_dir/requirements-tools.txt"
printf 'PlatformIO installed in %s (%s)\n' "$ESPSETTE_VENV" "$espsette_os"
# Stale cross-OS venv left in the repo tree is unused now.
if [ -e "$project_dir/tools/venv" ]; then
  printf 'Note: ignoring legacy %s (use per-OS cache venv instead).\n' \
    "$project_dir/tools/venv"
fi
