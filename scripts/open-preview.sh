#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
page="$project_dir/web/player.html"
if command -v open >/dev/null 2>&1; then
  open "$page"
elif command -v wslview >/dev/null 2>&1; then
  wslview "$page"
elif command -v xdg-open >/dev/null 2>&1; then
  xdg-open "$page"
else
  printf 'Open this file in a browser: %s\n' "$page"
fi
