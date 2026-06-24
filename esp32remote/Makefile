# esp32remote — JMGO BT beamer remote (ESP-IDF)
#
#   make            build
#   make deploy     build + flash over USB (alias: make flash)
#   make push       build + OTA update over WiFi (no cable) -> make push HOST=<ip>
#   make monitor    serial monitor  (exit: Ctrl+])
#   make run        flash + monitor
#   make clean      fullclean
#   make erase      erase whole flash
#   make port       show detected serial port
#   make version    show firmware version from main.c
#
# Override on the CLI if needed, e.g.:  make deploy PORT=/dev/cu.usbserial-XXXX
SHELL := /bin/bash

IDF_PATH ?= /Users/akw/.espressif/v6.0.1/esp-idf
PORT     ?= $(shell ls /dev/cu.usbserial-* /dev/cu.wchusbserial* /dev/cu.SLAB_USBtoUART* 2>/dev/null | head -n1)
BAUD     ?= 115200
# OTA target IP (fix a static DHCP lease!); override: make push HOST=1.2.3.4
HOST     ?= 192.168.178.175
APP_BIN  ?= build/esp32remote.bin

# Work around the broken system ESP-IDF env (system python got upgraded):
# use the VS Code extension's venv and skip the constraints check.
IDF_TOOLS_PATH               ?= /Users/akw/.espressif
IDF_PYTHON_ENV_PATH          ?= /Users/akw/.espressif/tools/python/v6.0.1/venv
IDF_PYTHON_CHECK_CONSTRAINTS ?= no
export IDF_TOOLS_PATH IDF_PYTHON_ENV_PATH IDF_PYTHON_CHECK_CONSTRAINTS

# Source export.sh (NOT piped -> no subshell) then run idf.py in the same shell.
IDF = . "$(IDF_PATH)/export.sh" >/dev/null 2>&1 && idf.py -C "$(CURDIR)"

.PHONY: build deploy flash push monitor run clean erase port version
build:
	@$(IDF) build

deploy flash:
	@test -n "$(PORT)" || { echo "No serial port found — plug in the ESP32 or pass PORT=..."; exit 1; }
	@echo ">> flashing $(PORT) @ $(BAUD) baud"
	@$(IDF) -p "$(PORT)" -b $(BAUD) flash

push: build
	@echo ">> OTA push $(APP_BIN) -> http://$(HOST)/api/ota/upload"
	@curl -fS --max-time 120 -X POST --data-binary @"$(APP_BIN)" http://$(HOST)/api/ota/upload && echo

monitor:
	@test -n "$(PORT)" || { echo "No serial port found — pass PORT=..."; exit 1; }
	@$(IDF) -p "$(PORT)" monitor

run:
	@test -n "$(PORT)" || { echo "No serial port found — pass PORT=..."; exit 1; }
	@$(IDF) -p "$(PORT)" -b $(BAUD) flash monitor

clean:
	@$(IDF) fullclean

erase:
	@test -n "$(PORT)" || { echo "No serial port found — pass PORT=..."; exit 1; }
	@$(IDF) -p "$(PORT)" erase-flash

port:
	@echo "PORT=$(PORT)"

version:
	@grep -E '^set\(PROJECT_VER' CMakeLists.txt | sed -E 's/.*"(.*)".*/\1/'
