PIO ?= pio
ENV ?= esp32dev
BUILD_DIR := .pio/build/$(ENV)
MERGED_BIN := build/terrarium-flash.bin
ESPTOOL ?= $(HOME)/.platformio/packages/tool-esptoolpy/esptool.py

.PHONY: all build upload upload-merged monitor clean erase menuconfig merge

all: build

build:
	$(PIO) run -e $(ENV)

upload:
	$(PIO) run -e $(ENV) -t upload

upload-merged: merge
	$(ESPTOOL) --chip esp32 --port "$(PORT)" --baud 460800 write_flash 0x0 $(MERGED_BIN)

monitor:
	$(PIO) device monitor -e $(ENV)

clean:
	$(PIO) run -e $(ENV) -t clean

erase:
	$(PIO) run -e $(ENV) -t erase

menuconfig:
	$(PIO) run -e $(ENV) -t menuconfig

# Bundle bootloader.bin + partitions.bin + firmware.bin into a single flash
# image so the whole project can be flashed with one file at offset 0x0.
merge: build
	mkdir -p build
	python3 $(ESPTOOL) --chip esp32 merge_bin -o $(MERGED_BIN) \
		--flash_mode dio --flash_freq 40m --flash_size 4MB \
		0x1000 $(BUILD_DIR)/bootloader.bin \
		0x8000 $(BUILD_DIR)/partitions.bin \
		0x10000 $(BUILD_DIR)/firmware.bin
