# GNU make predefines CXX as g++, so `CXX ?= clang++` would never take effect.
# Only make's own default is replaced; CXX from the command line or the
# environment wins.
ifeq ($(origin CXX),default)
CXX = clang++
endif
CXXFLAGS = -std=c++20 -Wall -Wextra -Wpedantic -Werror

# Each layer compiles against only the include roots of the layers AGENTS.md lets
# it depend on, so an #include across a layer does not compile. The replay tool
# sits on the pipeline; the tests see everything.
COMMON_INC = -Iinclude -Isrc/common/include
CAPTURE_INC = $(COMMON_INC) -Isrc/capture/include
PROTOCOL_INC = $(COMMON_INC) -Isrc/protocol/include
METER_INC = $(PROTOCOL_INC) -Isrc/meter/include
PIPELINE_INC = $(COMMON_INC) -Isrc/capture/include -Isrc/protocol/include -Isrc/meter/include \
               -Isrc/pipeline/include
APP_INC = $(PIPELINE_INC) -Isrc/app/include
TOOLS_INC = $(PIPELINE_INC)
TEST_INC = $(APP_INC) -Itests

# $(1): a source or header path; the include roots it compiles against.
layer_inc = $(strip \
    $(if $(filter include/% src/common/%,$(1)),$(COMMON_INC), \
    $(if $(filter src/capture/%,$(1)),$(CAPTURE_INC), \
    $(if $(filter src/protocol/%,$(1)),$(PROTOCOL_INC), \
    $(if $(filter src/meter/%,$(1)),$(METER_INC), \
    $(if $(filter src/pipeline/%,$(1)),$(PIPELINE_INC), \
    $(if $(filter src/app/%,$(1)),$(APP_INC), \
    $(if $(filter tools/synth/%,$(1)),$(TEST_INC), \
    $(if $(filter tools/%,$(1)),$(TOOLS_INC), \
    $(TEST_INC))))))))))

# Recorded in each variant's flags stamp, so a change of include roots rebuilds.
LAYER_INC_STAMP = $(COMMON_INC) | $(CAPTURE_INC) | $(PROTOCOL_INC) | $(METER_INC) | $(PIPELINE_INC) | \
                  $(APP_INC) | $(TOOLS_INC) | $(TEST_INC)

COMMON_SRCS = $(wildcard src/common/src/*.cpp) $(wildcard src/common/src/*/*.cpp)
# capture/src/os holds the Win32 capture edge; off Windows it compiles to its mocks.
CAPTURE_SRCS = $(wildcard src/capture/src/*.cpp) $(wildcard src/capture/src/os/*.cpp)
PROTOCOL_SRCS = $(wildcard src/protocol/src/*.cpp)
METER_SRCS = $(wildcard src/meter/src/*.cpp)
PIPELINE_SRCS = $(wildcard src/pipeline/src/*.cpp)
# src/app/src/win32 (WinMain, the windows, hotkeys) only builds on Windows; src/app/src/ui
# is the desktop window's ImGui pages, which build everywhere.
APP_SRCS = $(wildcard src/app/src/*.cpp) $(wildcard src/app/src/ui/*.cpp)
LIB_SRCS = $(COMMON_SRCS) $(CAPTURE_SRCS) $(PROTOCOL_SRCS) $(METER_SRCS) $(PIPELINE_SRCS) $(APP_SRCS)

TEST_SRCS = $(wildcard tests/*.cpp)
REPLAY_SRCS = $(wildcard tools/replay/*.cpp)
SYNTH_SRCS = $(wildcard tools/synth/*.cpp)

# The sanitizer runs are the same suite built into build/<variant>/.
TSAN_FLAGS = -fsanitize=thread -g -O1
ASAN_FLAGS = -fsanitize=address,undefined -fno-sanitize-recover=undefined \
             -fno-omit-frame-pointer -g -O1

all: test replay

test: aion2_test_runner check-headers check-ui check-forbidden check-skills check-maps check-npcs replay-smoke
	./aion2_test_runner

replay: aion2meter-replay

# Each variant compiles one object per source into build/<variant>/obj, with -MMD -MP
# dependency files, so an edit recompiles only what includes it. flags records the
# compiler and flags: it is rewritten only when they change, and then every object
# of that variant is rebuilt rather than linked against another compiler's.
# $(1): variant, $(2): test runner, $(3): extra compile and link flags.
define variant
$(1)_LIB_OBJS = $$(patsubst %.cpp,build/$(1)/obj/%.o,$$(LIB_SRCS))
$(1)_TEST_OBJS = $$(patsubst %.cpp,build/$(1)/obj/%.o,$$(TEST_SRCS))
$(1)_REPLAY_OBJS = $$(patsubst %.cpp,build/$(1)/obj/%.o,$$(REPLAY_SRCS))
$(1)_SYNTH_OBJS = $$(patsubst %.cpp,build/$(1)/obj/%.o,$$(SYNTH_SRCS))

$(2): $$($(1)_LIB_OBJS) $$($(1)_TEST_OBJS)
	$$(CXX) $$(CXXFLAGS) $(3) $$^ -o $$@ -pthread

build/$(1)/obj/%.o: %.cpp build/$(1)/flags
	@mkdir -p $$(dir $$@)
	$$(CXX) $$(CXXFLAGS) $$(call layer_inc,$$<) $(3) -MMD -MP -c $$< -o $$@

build/$(1)/flags: FORCE
	@mkdir -p $$(dir $$@)
	@echo '$$(CXX) $$(CXXFLAGS) $(3) $$(LAYER_INC_STAMP)' | cmp -s - $$@ || echo '$$(CXX) $$(CXXFLAGS) $(3) $$(LAYER_INC_STAMP)' > $$@

-include $$($(1)_LIB_OBJS:.o=.d) $$($(1)_TEST_OBJS:.o=.d) $$($(1)_REPLAY_OBJS:.o=.d) $$($(1)_SYNTH_OBJS:.o=.d)
endef

$(eval $(call variant,test,aion2_test_runner,))
$(eval $(call variant,tsan,build/tsan/aion2_test_runner,$(TSAN_FLAGS)))
$(eval $(call variant,asan,build/asan/aion2_test_runner,$(ASAN_FLAGS)))

aion2meter-replay: $(test_LIB_OBJS) $(test_REPLAY_OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ -pthread

build/synth/aion2meter-synth: $(test_LIB_OBJS) $(test_SYNTH_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ -pthread

# The replay tool end to end on the tests' made-up boss fight: its --fights report
# must match tests/golden/synthetic_fights.txt. After a change that moves the
# numbers on purpose, regenerate it with `make golden`.
SYNTH_CAPTURE = build/synth/synthetic.pcapng

$(SYNTH_CAPTURE): build/synth/aion2meter-synth
	./build/synth/aion2meter-synth $@ >/dev/null

replay-smoke: aion2meter-replay $(SYNTH_CAPTURE)
	./aion2meter-replay $(SYNTH_CAPTURE) --fights --quiet | diff -u tests/golden/synthetic_fights.txt -
	@echo "Replay smoke: OK"

golden: aion2meter-replay $(SYNTH_CAPTURE)
	@mkdir -p tests/golden
	./aion2meter-replay $(SYNTH_CAPTURE) --fights --quiet > tests/golden/synthetic_fights.txt

# ThreadSanitizer run of the suite: the queue, the snapshot slot and the runtime's
# threads. TSan exits non-zero when it reports.
tsan: build/tsan/aion2_test_runner
	TSAN_OPTIONS=halt_on_error=1 ./build/tsan/aion2_test_runner

asan: build/asan/aion2_test_runner
	./build/asan/aion2_test_runner

# The overlay UI lives behind HAVE_IMGUI, which the mock build never defines, so
# those blocks would compile for the first time in CI unless they are checked here.
# ImGui is vendored and platform-independent; the Win32/DX11 backends are not, so
# this is syntax-only and does not link.
IMGUI_DIR = src/third_party/imgui
UI_CHECK_SRCS = $(APP_SRCS)
# clang 20's -Wnontrivial-memcall fires on imgui.h's own memset(this, ...) constructors.
# Older clang rejects the unknown -Wno- option under -Werror and g++ has no such
# warning, so it is only passed to a compiler that knows it.
NO_NONTRIVIAL_MEMCALL := $(shell $(CXX) -Werror -Wnontrivial-memcall -x c++ -fsyntax-only /dev/null 2>/dev/null && echo -Wno-nontrivial-memcall)
UI_CHECK_FLAGS = -std=c++20 -Wall -Wextra -Wpedantic -Werror \
                 $(APP_INC) -I$(IMGUI_DIR) -DHAVE_IMGUI=1 -include $(IMGUI_DIR)/imgui.h \
                 $(NO_NONTRIVIAL_MEMCALL)
UI_CHECK_STAMPS = $(patsubst %.cpp,build/check-ui/%.ok,$(UI_CHECK_SRCS))

check-ui: $(UI_CHECK_STAMPS)
	@echo "UI check: OK"

$(UI_CHECK_STAMPS): build/check-ui/%.ok: %.cpp build/check-ui/flags
	@mkdir -p $(dir $@)
	$(CXX) $(UI_CHECK_FLAGS) -fsyntax-only $< -MMD -MP -MF $(@:.ok=.d) -MT $@
	@touch $@

build/check-ui/flags: FORCE
	@mkdir -p $(dir $@)
	@echo '$(CXX) $(UI_CHECK_FLAGS)' | cmp -s - $@ || echo '$(CXX) $(UI_CHECK_FLAGS)' > $@

-include $(UI_CHECK_STAMPS:.ok=.d)

# Every header, compiled on its own against its layer's include roots. A header is
# otherwise only compiled inside the sources that include it, with their roots, so
# a common header that only the app includes could reach into app/ unseen. It also
# keeps each header self-contained. One stamp per header, as check-ui.
HEADERS = $(shell find include src/common/include src/capture/include src/protocol/include \
                       src/meter/include src/pipeline/include src/app/include tests \
                       -name '*.hpp' 2>/dev/null)
HEADER_CHECK_STAMPS = $(patsubst %.hpp,build/check-headers/%.ok,$(HEADERS))

check-headers: $(HEADER_CHECK_STAMPS)
	@echo "Header check: OK"

$(HEADER_CHECK_STAMPS): build/check-headers/%.ok: %.hpp build/check-headers/flags
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(call layer_inc,$<) -fsyntax-only -include $< -x c++ /dev/null \
		-MMD -MP -MF $(@:.ok=.d) -MT $@
	@touch $@

build/check-headers/flags: FORCE
	@mkdir -p $(dir $@)
	@echo '$(CXX) $(CXXFLAGS) $(LAYER_INC_STAMP)' | cmp -s - $@ || echo '$(CXX) $(CXXFLAGS) $(LAYER_INC_STAMP)' > $@

-include $(HEADER_CHECK_STAMPS:.ok=.d)

# The passive-only rule (AGENTS.md): no API that touches the game process, injects,
# hooks or sends. Runs over our own code, not the vendored libraries.
check-forbidden:
	@sh tools/check_forbidden_apis.sh

# skill_table.inc is generated from src/meter/data/skills.tsv; this fails when the two differ.
check-skills:
	@python3 tools/gen_skill_table.py --check

# map_table.inc is generated from src/meter/data/maps.tsv, the same way.
check-maps:
	@python3 tools/gen_map_table.py --check

# npc_table.inc is generated from src/meter/data/npcs.tsv, the same way.
check-npcs:
	@python3 tools/gen_npc_table.py --check

# README pictures (docs/images): the overlay's own drawing code, run on the made-up
# boss fight and rasterized in software (tools/screenshots/canvas.cpp, from
# ffxiv-hub). Needs zlib; SHOTS_FONT stands in for Segoe UI.
SHOTS_DIR = tools/screenshots
SHOTS_BUILD = build/screenshots
SHOTS_FONT ?= /System/Library/Fonts/Supplemental/Arial.ttf
SHOTS_BOLD_FONT ?= /System/Library/Fonts/Supplemental/Arial Bold.ttf
SHOTS_BASE_FLAGS = -std=c++20 -O2 -MMD -MP $(TEST_INC) -I$(SHOTS_DIR) -I$(IMGUI_DIR)
SHOTS_FLAGS = $(SHOTS_BASE_FLAGS) -Wall -Wextra -Wpedantic -Wno-format-truncation $(NO_NONTRIVIAL_MEMCALL) \
              -DHAVE_IMGUI=1 -include $(IMGUI_DIR)/imgui.h
SHOTS_IMGUI_SRCS = $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp \
                   $(IMGUI_DIR)/imgui_widgets.cpp
SHOTS_SRCS = $(LIB_SRCS) $(wildcard $(SHOTS_DIR)/*.cpp)
SHOTS_OBJS = $(patsubst %.cpp,$(SHOTS_BUILD)/obj/%.o,$(SHOTS_SRCS))
SHOTS_IMGUI_OBJS = $(patsubst %.cpp,$(SHOTS_BUILD)/obj/%.o,$(SHOTS_IMGUI_SRCS))

screenshots: $(SHOTS_BUILD)/aion2_screenshots
	TZ=UTC $(SHOTS_BUILD)/aion2_screenshots --font "$(SHOTS_FONT)" --bold "$(SHOTS_BOLD_FONT)" --out docs/images

$(SHOTS_BUILD)/aion2_screenshots: $(SHOTS_OBJS) $(SHOTS_IMGUI_OBJS)
	$(CXX) $^ -o $@ -lz -pthread

$(SHOTS_IMGUI_OBJS): $(SHOTS_BUILD)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(SHOTS_BASE_FLAGS) -w -c $< -o $@

$(SHOTS_OBJS): $(SHOTS_BUILD)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(SHOTS_FLAGS) -c $< -o $@

-include $(SHOTS_OBJS:.o=.d)

# Replays every capture under tests/fixtures/local/ and diffs its fight report
# against the .expected.txt beside it. Captures hold character names, so they and
# their expectations never leave the machine (.gitignore).
FIXTURES = $(wildcard tests/fixtures/local/*.pcapng tests/fixtures/local/*.pcap)

fixtures: aion2meter-replay
	@if [ -z "$(FIXTURES)" ]; then echo "No captures in tests/fixtures/local/"; exit 0; fi; \
	status=0; \
	for f in $(FIXTURES); do \
		expected="$${f%.*}.expected.txt"; \
		if [ ! -f "$$expected" ]; then echo "SKIP $$f (no $$expected)"; continue; fi; \
		if ./aion2meter-replay "$$f" --fights --quiet | diff -u "$$expected" -; then echo "PASS $$f"; \
		else echo "FAIL $$f"; status=1; fi; \
	done; exit $$status

clean:
	rm -f aion2_test_runner aion2meter-replay
	rm -rf build/test build/tsan build/asan build/check-ui build/check-headers build/screenshots build/synth

FORCE:

.PHONY: all test replay tsan asan clean check-ui check-headers check-forbidden check-skills check-maps check-npcs fixtures replay-smoke golden screenshots FORCE
