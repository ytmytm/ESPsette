#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
  printf 'Usage: ota.sh ENV HOST\n' >&2
  exit 2
fi

if [ -z "${ESPSETTE_OTA_PASSWORD:-}" ]; then
  if [ ! -r /dev/tty ]; then
    printf 'Set ESPSETTE_OTA_PASSWORD in a local environment.\n' >&2
    exit 1
  fi
  printf 'OTA password: ' >&2
  stty -echo < /dev/tty
  trap 'stty echo < /dev/tty' 0 1 2 3 15
  IFS= read -r ESPSETTE_OTA_PASSWORD < /dev/tty
  stty echo < /dev/tty
  trap - 0 1 2 3 15
  printf '\n' >&2
fi

if [ -z "$ESPSETTE_OTA_PASSWORD" ]; then
  printf 'OTA password is required.\n' >&2
  exit 1
fi
export ESPSETTE_OTA_PASSWORD
exec "$(dirname "$0")/pio.sh" run -e "$1" -t upload --upload-port "$2"
