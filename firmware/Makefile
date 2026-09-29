# Host build: unit tests and the simulator run the real firmware sources
# against platform/host. The device build lives in platform/stm32wl.
#
#   make test      build and run all unit tests (+ payload vectors for the JS decoder)
#   make sim       build the simulator (options: see platform/host/sim_main.c)
#   make sim BUILD=build-15m DEFS=-DMIN_INTERVAL_S=900u   override config.h

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror -Wshadow -Wconversion -Wno-sign-conversion
INCS    := -Iinclude -Iplatform/host -D_DEFAULT_SOURCE $(DEFS)
LDLIBS  += -lm

BUILD   := build
APP_SRC := src/airtime.c src/payload.c src/sensor.c src/eink.c src/display.c \
           src/lorawan.c src/power.c src/watchdog.c src/app.c src/font6x11.c
HOST_SRC := platform/host/hal_host.c platform/host/lorawan_port_host.c
LIB_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(APP_SRC) $(HOST_SRC))

TESTS   := test_airtime test_payload test_sensor test_lorawan test_display test_app
VECTORS := $(BUILD)/payload_vectors.json

.PHONY: all test sim clean
all: sim $(addprefix $(BUILD)/,$(TESTS))

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(INCS) -c $< -o $@

$(BUILD)/test_%: tests/test_%.c $(LIB_OBJ)
	$(CC) $(CFLAGS) $(INCS) $^ -o $@ $(LDLIBS)

$(BUILD)/sim: platform/host/sim_main.c $(LIB_OBJ)
	$(CC) $(CFLAGS) $(INCS) $^ -o $@ $(LDLIBS)

sim: $(BUILD)/sim

test: $(addprefix $(BUILD)/,$(TESTS))
	@set -e; for t in $(TESTS); do \
	  if [ $$t = test_payload ]; then $(BUILD)/$$t $(VECTORS); else $(BUILD)/$$t; fi; \
	done
	@echo "payload vectors -> $(VECTORS)"

clean:
	rm -rf $(BUILD)
