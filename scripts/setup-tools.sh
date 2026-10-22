#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$project_dir/tools"
uv_bin="$project_dir/tools/bin/uv"
if [ ! -x "$uv_bin" ]; then
  command -v curl >/dev/null 2>&1 || { printf 'curl is required for first setup.\n' >&2; exit 1; }
  curl -fsSL https://astral.sh/uv/install.sh -o "$project_dir/tools/install-uv.sh"
  UV_UNMANAGED_INSTALL="$project_dir/tools/bin" sh "$project_dir/tools/install-uv.sh"
fi
export UV_CACHE_DIR="$project_dir/tools/uv-cache"
export UV_DATA_DIR="$project_dir/tools/uv-data"
export UV_PYTHON_INSTALL_DIR="$project_dir/tools/python"
export UV_PYTHON_BIN_DIR="$project_dir/tools/python-bin"
"$uv_bin" python install 3.12
"$uv_bin" venv "$project_dir/tools/venv" --python 3.12
"$uv_bin" pip install --python "$project_dir/tools/venv/bin/python" -r "$project_dir/requirements-tools.txt"
printf 'PlatformIO installed in %s\n' "$project_dir/tools/venv"
