# Shared by setup/pio/make: per-OS venv outside the repo (NFS-safe).
# tools/venv on a shared disk breaks when WSL/Windows/macOS overwrite each other.
# shellcheck shell=sh

espsette_project_dir=${espsette_project_dir:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
espsette_uname=$(uname -s 2>/dev/null || printf unknown)
case $espsette_uname in
  Darwin) espsette_os=darwin ;;
  Linux) espsette_os=linux ;;
  MINGW*|MSYS*|CYGWIN*) espsette_os=windows ;;
  *) espsette_os=$(printf '%s' "$espsette_uname" | tr 'A-Z' 'a-z') ;;
esac

ESPSETTE_VENV="${XDG_CACHE_HOME:-$HOME/.cache}/ESPsette/venv-$espsette_os"
export ESPSETTE_VENV

if [ -x "$ESPSETTE_VENV/bin/python" ]; then
  ESPSETTE_PYTHON="$ESPSETTE_VENV/bin/python"
elif [ -x "$ESPSETTE_VENV/Scripts/python.exe" ]; then
  ESPSETTE_PYTHON="$ESPSETTE_VENV/Scripts/python.exe"
else
  ESPSETTE_PYTHON=
fi
export ESPSETTE_PYTHON

if [ -x "$ESPSETTE_VENV/bin/pio" ]; then
  ESPSETTE_PIO="$ESPSETTE_VENV/bin/pio"
elif [ -x "$ESPSETTE_VENV/Scripts/pio.exe" ]; then
  ESPSETTE_PIO="$ESPSETTE_VENV/Scripts/pio.exe"
else
  ESPSETTE_PIO=
fi
export ESPSETTE_PIO
