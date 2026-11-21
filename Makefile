DEVKITPRO ?= $(CURDIR)/vendor/devkitpro
MOADER_VERSION := 3.5.0
export DEVKITPRO
include $(DEVKITPRO)/libounce/switch2_cpu.mk
PREFIX := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-
ARCH := $(SWITCH2_CPU) -mtp=soft -fPIE
CPPFLAGS := -D__SWITCH__ -D__SWITCH2__ -Iinclude -Iruntime/kaihen/libkaihen/include -I$(DEVKITPRO)/libounce/include -I$(DEVKITPRO)/portlibs/switch/include -I$(DEVKITPRO)/portlibs/switch2/include
CXXFLAGS := $(ARCH) -std=gnu++20 -O2 -g -Wall -Wextra -Werror -Wno-missing-field-initializers -ffunction-sections -fdata-sections -MMD -MP $(SOURCE_PREFIX_FLAGS)
CORE := build/native/model.o build/native/library.o build/native/zip.o build/native/web.o build/native/runtime_backend.o

UI_NAMES := ui canvas navigation artwork settings_screen store store_screen
UI_SRC := $(addprefix native/,$(addsuffix .cpp,$(UI_NAMES)))
UI_OBJ := $(addprefix build/native/,$(addsuffix .o,$(UI_NAMES)))

all: build/native/libmoader.a build/native/core-linkcheck.elf dist/Moader.nro dist/moader.nso dist/moader-diagnostics.nro

build/native dist:
	mkdir -p $@

build/native/%.o: source/%.cpp | build/native
	$(PREFIX)g++ $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/native/libmoader.a: $(CORE)
	$(PREFIX)ar rcs $@ $^

build/native/switch.specs: $(DEVKITPRO)/libounce/switch.specs | build/native
	sed 's|%:getenv(DEVKITPRO /libnx/switch.ld)|$(DEVKITPRO)/libounce/switch.ld|' $< > $@

build/native/diagnostics.o: native/diagnostics.cpp | build/native
	$(PREFIX)g++ $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/generated/web_assets.hpp: web/index.html web/style.css web/app.js web/store.js web/icon.svg branding/lockup.svg native/assets/Text.ttf native/assets/Strong.ttf tools/embed-web.py
	python3 tools/embed-web.py

build/generated/native_font.hpp: $(wildcard native/assets/*.ttf) native/embed-font.py
	python3 native/embed-font.py

build/generated/store_assets.hpp: native/store/catalog.json native/store/cache.json $(wildcard native/store/previews/*) native/embed-store.py
	python3 native/embed-store.py

$(UI_OBJ): build/native/%.o: native/%.cpp | build/native
	$(PREFIX)g++ $(CPPFLAGS) -Ibuild/generated $(CXXFLAGS) -Wno-unused-function -c $< -o $@

build/native/canvas.o: build/generated/native_font.hpp

build/native/catalog_fetch.o: native/catalog_fetch.cpp native/catalog_fetch.hpp | build/native
	$(PREFIX)g++ $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/native/app.o: native/app.cpp native/reload_client.hpp runtime/kaihen/libkaihen/include/kaihen/reload.h build/generated/web_assets.hpp | build/native
	$(PREFIX)g++ $(CPPFLAGS) -Ibuild/generated $(CXXFLAGS) -c $< -o $@

build/native/service.o: native/service.cpp native/resident_lifecycle.hpp runtime/kaihen/libkaihen/include/kaihen/reload.h build/generated/web_assets.hpp | build/native
	$(PREFIX)g++ $(CPPFLAGS) -Ibuild/generated $(CXXFLAGS) -c $< -o $@

# The redesigned foreground app gets its own ELF identity. The resident service
# keeps its existing reload protocol and independent build target.
build/native/Moader.elf: build/native/app.o build/native/catalog_fetch.o $(UI_OBJ) build/native/libmoader.a build/native/switch.specs
	$(PREFIX)g++ $(ARCH) -specs=build/native/switch.specs -Wl,--gc-sections -Wl,--build-id=sha1 build/native/app.o build/native/catalog_fetch.o $(UI_OBJ) build/native/libmoader.a -L$(DEVKITPRO)/libounce/lib -L$(DEVKITPRO)/portlibs/switch/lib -lcurl -lounce_core -lz -o $@

runtime/kaihen/libkaihen/build/libkaihen_plugin.a: $(wildcard runtime/kaihen/libkaihen/source/*) $(wildcard runtime/kaihen/libkaihen/include/kaihen/*) runtime/kaihen/libkaihen/Makefile
	$(MAKE) -C runtime/kaihen/libkaihen DEVKITPRO=$(DEVKITPRO) LIBOUNCE=$(DEVKITPRO)/libounce build/libkaihen_plugin.a

build/native/moader-service.elf: build/native/service.o build/native/artwork.o build/native/libmoader.a build/native/switch.specs runtime/kaihen/libkaihen/build/libkaihen_plugin.a
	$(PREFIX)g++ $(ARCH) -specs=build/native/switch.specs -Wl,--gc-sections -Wl,--build-id=sha1 $< build/native/artwork.o build/native/libmoader.a -Lruntime/kaihen/libkaihen/build -L$(DEVKITPRO)/portlibs/switch2/lib -L$(DEVKITPRO)/libounce/lib -L$(DEVKITPRO)/portlibs/switch/lib -lkaihen_plugin -lounce_core -lz -o $@

build/native/Moader.nacp: Makefile | build/native
	$(DEVKITPRO)/tools/bin/nacptool --create 'Moader' 'Moader' '$(MOADER_VERSION)' $@

dist/Moader.nro: build/native/Moader.elf build/native/Moader.nacp branding/hbmenu.jpg | dist
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/native/Moader.nacp --icon=branding/hbmenu.jpg

dist/moader.nso: build/native/moader-service.elf | dist
	$(DEVKITPRO)/tools/bin/elf2nso $< $@

build/native/diagnostics.elf: build/native/diagnostics.o build/native/switch.specs
	$(PREFIX)g++ $(ARCH) -specs=build/native/switch.specs -Wl,--gc-sections $< -L$(DEVKITPRO)/libounce/lib -lounce_core -o $@

build/native/diagnostics.nacp: Makefile | build/native
	$(DEVKITPRO)/tools/bin/nacptool --create 'Moader Diagnostics' 'Moader' '$(MOADER_VERSION)' $@

build/native/core-linkcheck.o: tests/native_link.cpp | build/native
	$(PREFIX)g++ $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/native/core-linkcheck.elf: build/native/core-linkcheck.o build/native/libmoader.a build/native/switch.specs
	$(PREFIX)g++ $(ARCH) -specs=build/native/switch.specs -Wl,--gc-sections $< build/native/libmoader.a -L$(DEVKITPRO)/libounce/lib -L$(DEVKITPRO)/portlibs/switch/lib -lounce_core -lz -o $@

dist/moader-diagnostics.nro: build/native/diagnostics.elf build/native/diagnostics.nacp | dist
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/native/diagnostics.nacp

check: build/generated/web_assets.hpp
	python3 tools/test-core.py
	python3 tools/test-library-refresh.py
	python3 tools/test-web.py
	python3 tools/test-web-worker.py
	python3 tools/test-runtime.py
	python3 tools/test-backend-restart.py
	python3 tools/test-runtime-logs.py
	python3 tools/test-metadata-names.py
	python3 tools/test-game-names.py
	python3 tools/test-game-availability.py
	python3 tools/test-resident.py
	python3 tools/test-reload-client.py
	LIBOUNCE=$(DEVKITPRO)/libounce CXX=clang++ sh runtime/kaihen/hen/test.sh
	LIBOUNCE=$(DEVKITPRO)/libounce CXX=clang++ sh runtime/kaihen/ro/test.sh
	python3 tools/test-native-ui.py

-include $(CORE:.o=.d) build/native/diagnostics.d build/native/core-linkcheck.d build/native/app.d build/native/catalog_fetch.d $(UI_OBJ:.o=.d) build/native/service.d
.PHONY: all check

# Uses the identical renderer and embedded font; fixtures are explicitly synthetic.
build/host/native-capture: native/ui_capture.cpp $(UI_SRC) $(wildcard native/*.hpp) branding/mark-alpha.hpp build/generated/native_font.hpp build/generated/store_assets.hpp
	mkdir -p build/host
	c++ -std=c++20 -O2 -Wall -Wextra -Werror -Wno-unused-function -Iinclude -Ibuild/generated native/ui_capture.cpp $(UI_SRC) -o $@

native-captures: build/host/native-capture
	build/host/native-capture native/review
	python3 native/ui_capture_convert.py

.PHONY: native-captures

build/host/native-control-test: native/ui_control_test.cpp native/control.hpp native/ui_tests/switch.h include/moader/ipc.hpp source/model.cpp source/library.cpp source/zip.cpp source/web.cpp
	mkdir -p build/host
	c++ -std=c++20 -O1 -g -Wall -Wextra -Werror -Wno-missing-field-initializers -fsanitize=address,undefined -Inative/ui_tests -Inative -Iinclude native/ui_control_test.cpp source/model.cpp source/library.cpp source/zip.cpp source/web.cpp -lz -pthread -o $@

native-check: build/host/native-control-test build/host/native-capture
	build/host/native-control-test
	build/host/native-capture native/review

.PHONY: native-check

# Isolated, normal NRO: bounded NS metadata reads only, started by A.
build/native/metadata.o: native/metadata.cpp native/metadata_names.hpp | build/native
	$(PREFIX)g++ $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/native/metadata.elf: build/native/metadata.o build/native/switch.specs
	$(PREFIX)g++ $(ARCH) -specs=build/native/switch.specs -Wl,--gc-sections $< -L$(DEVKITPRO)/libounce/lib -L$(DEVKITPRO)/portlibs/switch/lib -lounce_core -lz -o $@

build/native/metadata.nacp: Makefile | build/native
	$(DEVKITPRO)/tools/bin/nacptool --create 'Moader Metadata' 'Moader' '$(MOADER_VERSION)' $@

dist/moader-metadata.nro: build/native/metadata.elf build/native/metadata.nacp | dist
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/native/metadata.nacp

-include build/native/metadata.d

# Ordinary read-only consumer for same-storage MK8 verification; not part of all.
build/native/read-probe.o: native/read_probe.cpp | build/native
	$(PREFIX)g++ $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/native/read-probe.elf: build/native/read-probe.o build/native/switch.specs
	$(PREFIX)g++ $(ARCH) -specs=build/native/switch.specs -Wl,--gc-sections $< -L$(DEVKITPRO)/libounce/lib -lounce_core -o $@

build/native/read-probe.nacp: Makefile | build/native
	$(DEVKITPRO)/tools/bin/nacptool --create 'Moader MK8 Read Probe' 'Moader' '$(MOADER_VERSION)' $@

dist/moader-read-probe.nro: build/native/read-probe.elf build/native/read-probe.nacp branding/hbmenu.jpg | dist
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/native/read-probe.nacp --icon=branding/hbmenu.jpg

-include build/native/read-probe.d

# Ordinary fixed-path log access diagnostic; separate from the release build.
build/native/log-probe.o: native/log_probe.cpp | build/native
	$(PREFIX)g++ $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

build/native/log-probe.elf: build/native/log-probe.o build/native/switch.specs
	$(PREFIX)g++ $(ARCH) -specs=build/native/switch.specs -Wl,--gc-sections $< -L$(DEVKITPRO)/libounce/lib -lounce_core -o $@

build/native/log-probe.nacp: | build/native
	$(DEVKITPRO)/tools/bin/nacptool --create 'MoaderLogProbe' 'Moader' '0.1.0-diag' $@

dist/moader-log-probe.nro: build/native/log-probe.elf build/native/log-probe.nacp branding/hbmenu.jpg | dist
	$(DEVKITPRO)/tools/bin/elf2nro $< $@ --nacp=build/native/log-probe.nacp --icon=branding/hbmenu.jpg

-include build/native/log-probe.d
