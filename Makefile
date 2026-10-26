TARGET  := encoder_test
BUILD   := build

CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
SIZE    := arm-none-eabi-size

CFLAGS  := -mcpu=cortex-m3 -mthumb -O2 -g3 -Wall -Wextra \
           -ffunction-sections -fdata-sections -fno-common
LDFLAGS := -mcpu=cortex-m3 -mthumb -T linker/STM32F103C8Tx_FLASH.ld \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map -nostartfiles

SRCS := src/startup_stm32f103c8tx.s src/main.c
OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(filter %.c,$(SRCS))) \
        $(patsubst src/%.s,$(BUILD)/%.o,$(filter %.s,$(SRCS)))

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: src/%.s | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $@
	$(SIZE) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@

flash: all
	openocd -f interface/stlink.cfg -f target/stm32f1x.cfg \
	  -c "program $(BUILD)/$(TARGET).elf verify reset exit"

clean:
	rm -rf $(BUILD)

.PHONY: all flash clean
