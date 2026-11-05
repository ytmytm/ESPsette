PROFILE ?= 1m
PORT ?=
OTA_HOST ?=
VERBOSE ?= 0

ifeq ($(VERBOSE),1)
PIO_VERBOSE := -v
else
PIO_VERBOSE :=
endif

ifeq ($(PROFILE),512k)
SERIAL_ENV := esp01_512k_serial
else ifeq ($(PROFILE),1m)
SERIAL_ENV := esp01_1m_serial
OTA_ENV := esp01_1m_ota
else ifeq ($(PROFILE),4m)
SERIAL_ENV := esp01_4m_serial
OTA_ENV := esp01_4m_ota
else
$(error PROFILE must be 512k, 1m, or 4m)
endif

# Per-OS venv under ~/.cache/ESPsette/venv-{darwin,linux,windows} (see scripts/tools-env.sh).
ESPTOOL = ESPTOOL_CFGFILE="$(CURDIR)/esptool.cfg" "$$(./scripts/venv-python.sh)" -m esptool --chip esp8266 --port "$(PORT)"

.PHONY: help setup build flash ota flash-id chip-info embed-web preview fastloader-test native version

help:
	@printf '%s\n' \
		'ESPsette — ESP-01 tape firmware' \
		'' \
		'Targets:' \
		'  make help                          Show this help' \
		'  make version                       Show firmware Git identity without building' \
		'  make setup                         Install local uv/Python/PlatformIO tools' \
		'  make preview                       Open local UI with simulated ESP state' \
		'  make fastloader-test               Run isolated fastloader sender tests' \
		'  make native                       Assemble C64 and C16/Plus4 fastloaders (ACME)' \
		'  make build [PROFILE=1m] [VERBOSE=1] Compile (PROFILE: 512k|1m|4m)' \
		'  make flash PORT=... [PROFILE=1m]   Serial upload (download-mode wiring)' \
		'  make ota OTA_HOST=... [PROFILE=1m] Compressed OTA; stop MOTOR first' \
		'  make flash-id PORT=...             SPI flash manufacturer / size' \
		'  make chip-info PORT=...            flash-id + chip-id + MAC + flash status' \
		'' \
		'Serial flash / chip-info wiring (CH340 without RTS):' \
		'  GPIO0-GND, TXD-RXD, RXD-TXD, VCC-VCC 3V3, DTR-RST; CH_PD at 3V3' \
		'  After flash: disconnect GPIO0 from GND, reset to run' \
		'' \
		'Examples:' \
		'  make setup' \
		'  make chip-info PORT=/dev/cu.usbserial-XXXX' \
		'  make build PROFILE=1m' \
		'  make flash PROFILE=1m PORT=/dev/cu.usbserial-XXXX'

version:
	@python3 scripts/build-version.py --show-only

setup:
	./scripts/setup-tools.sh

embed-web:
	"$$(./scripts/venv-python.sh)" scripts/embed-player.py

preview:
	sh ./scripts/open-preview.sh

fastloader-test:
	$(CC) -std=c11 -Wall -Wextra -Werror -pedantic experiments/fastloader/sender-test.c experiments/fastloader/sender.c -o /tmp/ESPsette-fastloader-test
	/tmp/ESPsette-fastloader-test

native:
	python3 scripts/build-native.py

build: embed-web
	./scripts/pio.sh run $(PIO_VERBOSE) -e $(SERIAL_ENV)

flash: embed-web
	@test -n "$(PORT)" || { echo 'Set PORT=/dev/cu.usbserial-...'; exit 1; }
	./scripts/pio.sh run $(PIO_VERBOSE) -e $(SERIAL_ENV) -t upload --upload-port "$(PORT)"

ota: embed-web
	@test -n "$(OTA_ENV)" || { echo 'OTA is not planned for 512 KB'; exit 1; }
	@test -n "$(OTA_HOST)" || { echo 'Set OTA_HOST to the device IP or .local name'; exit 1; }
	@./scripts/ota.sh $(OTA_ENV) "$(OTA_HOST)"

flash-id:
	@test -n "$(PORT)" || { echo 'Set PORT=/dev/cu.usbserial-...'; exit 1; }
	$(ESPTOOL) flash-id

# Read-only chip dump (same download-mode wiring as flash-id; does not erase).
chip-info:
	@test -n "$(PORT)" || { echo 'Set PORT=/dev/cu.usbserial-...'; exit 1; }
	@echo '=== flash-id ==='
	$(ESPTOOL) flash-id
	@echo '=== chip-id ==='
	$(ESPTOOL) chip-id
	@echo '=== read-mac ==='
	$(ESPTOOL) read-mac
	@echo '=== read-flash-status ==='
	$(ESPTOOL) read-flash-status
