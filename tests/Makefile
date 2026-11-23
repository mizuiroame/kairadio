# Host builds of the tests, with sanitizers.
#   make -C tests run        unit tests
#   make -C tests coverage   the same, then line coverage per file
CC      ?= gcc
SRC     := ../source
SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS  := -DKAIRADIO_VERSION=\"test\" -std=gnu11 -g -O1 -Wall -Wextra -fwrapv -pthread -I$(SRC) -I../third_party $(SAN)
LDFLAGS := $(SAN) -pthread -lm
OBJDIR  := build

TESTED  := util.c icy.c link.c stations.c ring.c hls.c directory.c logo.c library.c categories.c worldmap.c map_countries.c flags.c flags_index.c categoryicons.c icons.c icons_index.c config.c settings.c theme.c background.c approot.c kinetic.c globe.c gfx.c weather.c lang.c update.c updaterelease.c notice.c xmlread.c podcast.c podstore.c podfetch.c
TESTS   := test_main.c test_stream.c test_logo.c test_map.c test_flags.c test_icons.c test_settings.c test_approot.c test_kinetic.c test_globe.c test_weather.c fake_net.c test_lang.c test_update.c test_updaterelease.c test_notice.c test_podcast.c test_podstore.c

# GCC 12's ASan can hang on kernels with high mmap entropy, so ASLR is off.
# In Docker this needs --security-opt seccomp=unconfined.
NOASLR  := setarch $$(uname -m) -R

.PHONY: run coverage clean

run: $(OBJDIR)/test_main
	$(NOASLR) ./$(OBJDIR)/test_main

coverage: run
	cd $(OBJDIR) && gcov -n *.gcda 2>/dev/null | grep -A1 "File '.*source/" | grep -v "^--"

$(OBJDIR)/test_main: $(TESTS) check.h $(addprefix $(SRC)/,$(TESTED)) $(OBJDIR)/cJSON.o $(OBJDIR)/image_impl.o
	@mkdir -p $(OBJDIR)
	cd $(OBJDIR) && $(CC) $(CFLAGS) --coverage -I../$(SRC) -I../../third_party \
		$(addprefix ../,$(TESTS)) $(addprefix ../$(SRC)/,$(TESTED)) cJSON.o image_impl.o \
		-o test_main $(LDFLAGS) --coverage

$(OBJDIR)/cJSON.o: ../third_party/cJSON.c
	@mkdir -p $(OBJDIR)
	$(CC) -O2 -c $< -o $@

# The image and font libraries are built without instrumentation.
$(OBJDIR)/image_impl.o: $(SRC)/image_impl.c
	@mkdir -p $(OBJDIR)
	$(CC) -O2 -I$(SRC) -I../third_party -c $< -o $@

clean:
	rm -rf $(OBJDIR)
