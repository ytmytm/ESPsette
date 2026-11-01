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
else ifeq ($(PROFILE),1m-write)
SERIAL_ENV := esp01_1m_write_serial
else ifeq ($(PROFILE),4m)
SERIAL_ENV := esp01_4m_serial
OTA_ENV := esp01_4m_ota
else ifeq ($(PROFILE),4m-write)
SERIAL_ENV := esp01_4m_write_serial
else
$(error PROFILE must be 512k, 1m, 4m, 1m-write, or 4m-write)
endif

ESPTOOL := ESPTOOL_CFGFILE="$(CURDIR)/esptool.cfg" ./tools/venv/bin/python -m esptool --chip esp8266 --port "$(PORT)"

.PHONY: help setup build flash ota flash-id chip-info embed-web preview

help:
	@printf '%s\n' \
		'espsette — ESP-01 tape firmware' \
		'' \
		'Targets:' \
		'  make help                          Show this help' \
		'  make setup                         Install local uv/Python/PlatformIO tools' \
		'  make build [PROFILE=1m] [VERBOSE=1] Compile (PROFILE: 512k|1m|4m)' \
		'  make flash PORT=... [PROFILE=1m]   Serial upload (download-mode wiring)' \
		'  make ota OTA_HOST=... [PROFILE=1m] Experimental; disabled in default firmware' \
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

setup:
	./scripts/setup-tools.sh

embed-web:
	./tools/venv/bin/python scripts/embed-player.py

preview:
	./scripts/open-preview.sh

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
