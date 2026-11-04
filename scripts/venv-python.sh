#!/bin/sh
set -eu
espsette_project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
# shellcheck source=tools-env.sh
. "$(dirname -- "$0")/tools-env.sh"
if [ -z "${ESPSETTE_PYTHON:-}" ]; then
  printf 'Run make setup first (no Python venv for this OS).\n' >&2
  exit 1
fi
printf '%s\n' "$ESPSETTE_PYTHON"
