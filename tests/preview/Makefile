# Renders UI screenshots on the host and checks every touch target:
#   make -C tests/preview
# Writes build/*.ppm; convert them to PNG with any image tool.
CC      ?= gcc
SRC     := ../../source
OUT     := build
CFLAGS  := -std=gnu11 -O1 -g -Wall -Wextra -DKAIRADIO_VERSION=\"preview\" -I$(SRC) -I../../third_party -I$(OUT)
UI_SRC  := gfx.c font.c ui.c ui_now.c ui_common.c ui_buttons.c ui_hit.c util.c stations.c link.c app.c library.c \
           touch.c image_impl.c

.PHONY: run clean

run: $(OUT)/preview
	./$(OUT)/preview $(OUT)

$(OUT)/font_regular_ttf.c: ../../data/font_regular.ttf embed.py
	python3 embed.py $< $(OUT) font_regular_ttf

$(OUT)/font_bold_ttf.c: ../../data/font_bold.ttf embed.py
	python3 embed.py $< $(OUT) font_bold_ttf

$(OUT)/preview: preview.c stubs.c font_system_host.c $(addprefix $(SRC)/,$(UI_SRC)) $(OUT)/font_regular_ttf.c $(OUT)/font_bold_ttf.c
	$(CC) $(CFLAGS) preview.c stubs.c font_system_host.c $(addprefix $(SRC)/,$(UI_SRC)) ../../third_party/cJSON.c \
		$(OUT)/font_regular_ttf.c $(OUT)/font_bold_ttf.c -o $@ -lm

clean:
	rm -rf $(OUT)
