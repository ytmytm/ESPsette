#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
  printf 'Usage: ota.sh ENV HOST\n' >&2
  exit 2
fi

# Open OTA by default. If the running image was built with a non-empty
# ESPSETTE_OTA_PASSWORD, export the same value here so --auth is passed.
set -- "$(dirname "$0")/pio.sh" run -e "$1" -t upload --upload-port "$2"
if [ -n "${ESPSETTE_OTA_PASSWORD:-}" ]; then
  set -- "$@" --upload-flags "--auth=${ESPSETTE_OTA_PASSWORD}"
fi
exec "$@"
