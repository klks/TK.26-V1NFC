# Bike RFID reader firmware - GCC rebuild for the Puya PY32F002B (Cortex-M0+, 24 KB / 3 KB).
#
#   make                       build/bike_rfid.elf + .bin + .map, and print section sizes
#   make OPTION_BYTES=1        also write the USER option bytes on first boot, as the vendor
#                              does (PC0 becomes GPIO) - see CLAUDE.md before using it
#   make clean                 remove the build outputs
#   make flash                 program build/bike_rfid.bin with J-Link - SPARE BOARD ONLY
#
# Toolchain and SDK live outside the repo (L:\LocalDev\compilers); override on the command line.
# The HAL is the vendor's exact one: PY32F002B Firmware package, tag 1.2.1, compiled unchanged.

TOOLCHAIN ?= L:/LocalDev/compilers/arm-gnu-toolchain-14.2.rel1/bin
PY32_FW   ?= L:/LocalDev/compilers/PY32F002B_Firmware/Drivers
JLINK     ?= L:/LocalDev/compilers/JLink_V842/JLink.exe
OPTION_BYTES ?= 0

CC      = $(TOOLCHAIN)/arm-none-eabi-gcc
OBJCOPY = $(TOOLCHAIN)/arm-none-eabi-objcopy
SIZE    = $(TOOLCHAIN)/arm-none-eabi-size

TARGET = build/bike_rfid
LDSCRIPT = src/py32f002bx5.ld
HAL_DIR = $(PY32_FW)/PY32F002B_HAL_Driver

# The HAL modules the vendor linked (ida/firmware.asm), and nothing else.
HAL_MODULES = hal hal_cortex hal_rcc hal_gpio hal_flash hal_iwdg hal_adc hal_tim hal_tim_ex hal_uart

C_SRCS = $(wildcard src/*.c)
S_SRCS = src/startup_py32f002b.s
HAL_SRCS = $(foreach m,$(HAL_MODULES),$(HAL_DIR)/Src/py32f002b_$(m).c)
OBJS = $(patsubst src/%.c,build/%.o,$(C_SRCS)) $(patsubst src/%.s,build/%.o,$(S_SRCS)) \
       $(foreach m,$(HAL_MODULES),build/hal/py32f002b_$(m).o)

ARCH = -mcpu=cortex-m0plus -mthumb
INCLUDES = -Isrc -I$(PY32_FW)/CMSIS/Include -I$(PY32_FW)/CMSIS/Device/PY32F0xx/Include \
           -I$(HAL_DIR)/Inc
CFLAGS_COMMON = $(ARCH) -std=gnu11 -Os -g3 -fno-builtin-printf -ffunction-sections -fdata-sections \
                -DPY32F002Bx5 -DBIKE_WRITE_OPTION_BYTES=$(OPTION_BYTES) $(INCLUDES) -MMD -MP
CFLAGS = $(CFLAGS_COMMON) -Wall -Wextra
# Vendor code is compiled as shipped; its warnings are not ours to fix.
HAL_CFLAGS = $(CFLAGS_COMMON) -w
LDFLAGS = $(ARCH) -nostartfiles -T$(LDSCRIPT) -Wl,--gc-sections -Wl,-Map=$(TARGET).map \
          --specs=nano.specs --specs=nosys.specs

all: $(TARGET).bin

$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	$(CC) $(LDFLAGS) $(OBJS) -o $@
	$(SIZE) $@

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.o: src/%.s | build
	$(CC) $(ARCH) -c $< -o $@

build/hal/%.o: $(HAL_DIR)/Src/%.c | build/hal
	$(CC) $(HAL_CFLAGS) -c $< -o $@

build:
	mkdir -p build

build/hal:
	mkdir -p build/hal

flash: $(TARGET).bin
	tools/flash.sh $(TARGET).bin

clean:
	rm -f build/*.o build/*.d build/hal/*.o build/hal/*.d $(TARGET).elf $(TARGET).bin $(TARGET).map

-include $(wildcard build/*.d build/hal/*.d)

.PHONY: all clean flash
