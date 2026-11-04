#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
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
if ! "$project_dir/tools/venv/bin/python" --version >/dev/null 2>&1; then
  rm -rf "$project_dir/tools/venv"
fi
"$uv_bin" venv --allow-existing "$project_dir/tools/venv" --python 3.12
"$uv_bin" pip install --python "$project_dir/tools/venv/bin/python" -r "$project_dir/requirements-tools.txt"
printf 'PlatformIO installed in %s\n' "$project_dir/tools/venv"
