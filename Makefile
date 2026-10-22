PROFILE ?= 1m
PORT ?=
OTA_HOST ?=

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

.PHONY: setup build flash ota flash-id

setup:
	./scripts/setup-tools.sh

build:
	./scripts/pio.sh run -e $(SERIAL_ENV)

flash:
	@test -n "$(PORT)" || { echo 'Set PORT=/dev/cu.usbserial-...'; exit 1; }
	./scripts/pio.sh run -e $(SERIAL_ENV) -t upload --upload-port "$(PORT)"

ota:
	@test -n "$(OTA_ENV)" || { echo 'OTA is not planned for 512 KB'; exit 1; }
	@test -n "$(OTA_HOST)" || { echo 'Set OTA_HOST to the device IP or .local name'; exit 1; }
	@./scripts/ota.sh $(OTA_ENV) "$(OTA_HOST)"

flash-id:
	@test -n "$(PORT)" || { echo 'Set PORT=/dev/cu.usbserial-...'; exit 1; }
	./tools/venv/bin/python -m esptool --chip esp8266 --port "$(PORT)" flash-id
