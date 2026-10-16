# Host-side parser tests intentionally work without devkitPro.
.DEFAULT_GOAL := all
.PHONY: host-test clean-host assets packaging-tools verify-cia
host-test:
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_catalog_parser.cpp source/catalog_parser.cpp \
		-o tests/catalog_parser_tests
	./tests/catalog_parser_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_manifest_parser.cpp source/manifest_parser.cpp \
		-o tests/manifest_parser_tests
	./tests/manifest_parser_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_write_all.cpp -o tests/write_all_tests
	./tests/write_all_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_cia_header.cpp -o tests/cia_header_tests
	./tests/cia_header_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_transfer_status.cpp -o tests/transfer_status_tests
	./tests/transfer_status_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		-Ithird_party/monocypher -c source/updater.cpp -o tests/updater.o
	cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Ithird_party/monocypher \
		-c third_party/monocypher/monocypher.c -o tests/monocypher.o
	cc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -Ithird_party/monocypher \
		-c third_party/monocypher/monocypher-ed25519.c -o tests/monocypher-ed25519.o
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_updater.cpp tests/updater.o tests/monocypher.o \
		tests/monocypher-ed25519.o -o tests/updater_tests
	./tests/updater_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_ui_limits.cpp -o tests/ui_limits_tests
	./tests/ui_limits_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_settings.cpp source/settings.cpp -o tests/settings_tests
	./tests/settings_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_type_chooser.cpp source/type_chooser.cpp -o tests/type_chooser_tests
	./tests/type_chooser_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_filter_panel.cpp source/filter_panel.cpp -o tests/filter_panel_tests
	./tests/filter_panel_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_bottom_navigation.cpp source/bottom_navigation.cpp -o tests/bottom_navigation_tests
	./tests/bottom_navigation_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_cover_status.cpp source/cover_status.cpp -o tests/cover_status_tests
	./tests/cover_status_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_cover_pixels.cpp source/cover_pixels.cpp -o tests/cover_pixels_tests
	./tests/cover_pixels_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_text_layout.cpp source/text_layout.cpp -o tests/text_layout_tests
	./tests/text_layout_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_romm_api.cpp source/romm_api.cpp source/catalog_parser.cpp \
		-o tests/romm_api_tests
	./tests/romm_api_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_cache.cpp source/cache.cpp source/catalog_parser.cpp \
		-o tests/cache_tests
	./tests/cache_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_save_data.cpp source/save_data.cpp -o tests/save_data_tests
	./tests/save_data_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_save_sources.cpp source/save_sources.cpp -o tests/save_sources_tests
	./tests/save_sources_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_checkpoint_sync.cpp source/checkpoint_sync.cpp -o tests/checkpoint_sync_tests
	./tests/checkpoint_sync_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_title_picker.cpp source/title_picker.cpp -o tests/title_picker_tests
	./tests/title_picker_tests
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_save_selection.cpp source/save_selection.cpp -o tests/save_selection_tests
	./tests/save_selection_tests
	python3 tests/test_checkpoint_stack_usage.py
	g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
		tests/test_library_state.cpp source/library_state.cpp -o tests/library_state_tests
	./tests/library_state_tests

clean-host:
	rm -f tests/catalog_parser_tests tests/manifest_parser_tests tests/write_all_tests tests/cia_header_tests tests/transfer_status_tests tests/updater_tests tests/updater.o tests/monocypher.o tests/monocypher-ed25519.o tests/ui_limits_tests tests/settings_tests tests/type_chooser_tests tests/filter_panel_tests tests/bottom_navigation_tests tests/cover_status_tests tests/cover_pixels_tests tests/text_layout_tests tests/romm_api_tests tests/cache_tests tests/save_data_tests tests/save_sources_tests tests/checkpoint_sync_tests tests/title_picker_tests tests/save_selection_tests tests/library_state_tests

assets:
	python3 packaging/generate_assets.py --output assets

packaging-tools:
	sh packaging/build_tools.sh

# Do not load the 3DS rules for host-only goals.
ifeq ($(filter host-test clean-host assets packaging-tools,$(MAKECMDGOALS)),)

ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM is not set. Install devkitPro 3DS tools or run 'make host-test')
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET       := pocketshelf
BUILD        := build
SOURCES      := source third_party/monocypher
DATA         :=
INCLUDES     := include third_party/monocypher

APP_TITLE       := PocketShelf
APP_DESCRIPTION := Direct RomM 5.4 trusted-LAN client
APP_AUTHOR      := PocketShelf contributors

TOOLS_DIR       := $(CURDIR)/tools/bin
BANNERTOOL      ?= $(TOOLS_DIR)/bannertool
MAKEROM         ?= $(TOOLS_DIR)/makerom
RSF             := packaging/pocketshelf.rsf
CIA_VERSION_MAJOR := 0
CIA_VERSION_MINOR := 6
CIA_VERSION_MICRO := 1
CIA_VERSION       := 97

ARCH       := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS     := -g -Wall -Wextra -O2 -mword-relocations -ffunction-sections $(ARCH)
CFLAGS     += $(INCLUDE) -D__3DS__
CXXFLAGS   := $(CFLAGS) -fno-rtti -fno-exceptions -fstack-usage -std=gnu++17
ASFLAGS    := -g $(ARCH)
LDFLAGS    := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS       := -lcitro2d -lcitro3d -lpng -ljpeg -lz -lctru -lm
LIBDIRS    := $(CTRULIB) $(PORTLIBS)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                  $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD       := $(CXX)
export OFILES   := $(addsuffix .o,$(BINFILES)) \
                   $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export HFILES   := $(addsuffix .h,$(subst .,_,$(BINFILES)))
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export _3DSXDEPS := $(OUTPUT).smdh
export _3DSXFLAGS := --smdh=$(OUTPUT).smdh

.PHONY: all clean cia $(BUILD)
all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).elf $(TARGET).smdh $(TARGET).cia \
		$(TARGET).bnr $(TARGET).icn romm-3ds-client.3dsx romm-3ds-client.elf \
		romm-3ds-client.smdh romm-3ds-client.cia romm-3ds-client.bnr romm-3ds-client.icn

# Installed-title package using project-local, source-built tools and original assets.
cia: all assets
	@test -x "$(BANNERTOOL)" || { echo "$(BANNERTOOL) not found; run 'make packaging-tools'"; exit 1; }
	@test -x "$(MAKEROM)" || { echo "$(MAKEROM) not found; run 'make packaging-tools'"; exit 1; }
	$(BANNERTOOL) makesmdh -s "$(APP_TITLE)" -l "$(APP_DESCRIPTION)" -p "$(APP_AUTHOR)" \
		-i assets/icon.png -o $(TARGET).icn
	$(BANNERTOOL) makebanner -i assets/banner.png -a assets/banner.wav -o $(TARGET).bnr
	$(MAKEROM) -f cia -o $(TARGET).cia -target t -exefslogo -elf $(TARGET).elf \
		-ver $(CIA_VERSION) \
		-rsf $(RSF) -icon $(TARGET).icn -banner $(TARGET).bnr

verify-cia: cia
	python3 packaging/verify_cia.py $(TARGET).cia

else

# .DEFAULT_GOAL is also inherited by the recursive build invocation.
.PHONY: all
all: $(OUTPUT).3dsx

$(OUTPUT).3dsx: $(OUTPUT).elf $(_3DSXDEPS)
$(OUTPUT).elf: $(OFILES)

-include $(DEPSDIR)/*.d

endif
endif
