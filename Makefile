# Among Us SP - NumWorks port (credits: discord.gg/sti2d | discord : cjnw (yamii))
CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
BUILD = output
CFLAGS = -std=c99 -mthumb -mfloat-abi=hard -mcpu=cortex-m7 -mfpu=fpv5-sp-d16 -Isrc
CFLAGS += -O2 -Wall -fsingle-precision-constant -fdata-sections -ffunction-sections
CFLAGS += -flto -fno-fat-lto-objects -fwhole-program -fvisibility=internal
LDFLAGS = -Wl,--relocatable -nostartfiles --specs=nano.specs
LDFLAGS += -Wl,-e,main -Wl,-u,eadk_app_name -Wl,-u,eadk_app_icon -Wl,-u,eadk_api_level -Wl,--gc-sections
LDFLAGS += -flinker-output=nolto-rel

all: $(BUILD)/AmongUs.nwa

$(BUILD)/AmongUs.nwa: $(BUILD)/main.o $(BUILD)/gfx.o $(BUILD)/assets.o $(BUILD)/icon.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -lm -o $@

$(BUILD)/%.o: src/%.c src/assets.h src/gfx.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/assets.o: src/assets.bin | $(BUILD)
	cd src && $(OBJCOPY) -I binary -O elf32-littlearm -B arm --rename-section .data=.rodata.assets,alloc,load,readonly,data,contents --set-section-alignment .rodata.assets=4 assets.bin ../$@

$(BUILD)/icon.o: src/icon.nwi | $(BUILD)
	cd src && $(OBJCOPY) -I binary -O elf32-littlearm -B arm --rename-section .data=.rodata.eadk_app_icon --redefine-sym _binary_icon_nwi_start=eadk_app_icon --strip-symbol _binary_icon_nwi_end --strip-symbol _binary_icon_nwi_size icon.nwi ../$@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)
