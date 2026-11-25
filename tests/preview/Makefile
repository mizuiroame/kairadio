# Renders UI screenshots on the host and checks every touch target:
#   make -C tests/preview
# Writes build/*.ppm; convert them to PNG with any image tool.
CC      ?= gcc
SRC     := ../../source
OUT     := build
CFLAGS  := -std=gnu11 -O1 -g -Wall -Wextra -DKAIRADIO_VERSION=\"preview\" -I$(SRC) -I../../third_party -I$(OUT)
UI_SRC  := ui_brand.c categories.c gfx.c font.c ui.c ui_now.c ui_common.c ui_buttons.c ui_hit.c util.c stations.c link.c app.c library.c \
           touch.c kinetic.c globe.c sunlight.c terrain.c passport.c ui_passport.c weather.c image_impl.c ui_map.c worldmap.c mapzoom.c app_map.c map_countries.c ui_flag.c flags.c flags_index.c categoryicons.c icons.c icons_index.c \
           ui_settings.c app_settings.c config.c settings.c theme.c background.c lang.c \
           notice.c ui_notice.c app_update.c update.c app_podcast.c ui_podcast.c podstore.c podcast.c xmlread.c

.PHONY: run clean

# Every language in data/ too, each in build/<id>: what no longer fits shows.
# For Japanese, Chinese and Korean, KAIRADIO_CJK_FONT names a font with them
# (font_system_host.c); without one, their text draws as boxes.
LANGS   := $(patsubst ../../data/lang_%.txt,%,$(wildcard ../../data/lang_*.txt))

run: $(OUT)/preview
	./$(OUT)/preview $(OUT)
	@for l in $(LANGS); do mkdir -p $(OUT)/$$l && KAI_LANG_FILE=../../data/lang_$$l.txt ./$(OUT)/preview $(OUT)/$$l || exit 1; done

$(OUT)/about_icon_png.c: ../../data/about_icon.png embed.py
	python3 embed.py $< $(OUT) about_icon_png

$(OUT)/brand_seal_png.c: ../../data/brand_seal.png embed.py
	python3 embed.py $< $(OUT) brand_seal_png

$(OUT)/brand_word_png.c: ../../data/brand_word.png embed.py
	python3 embed.py $< $(OUT) brand_word_png

$(OUT)/font_regular_ttf.c: ../../data/font_regular.ttf embed.py
	python3 embed.py $< $(OUT) font_regular_ttf

$(OUT)/font_bold_ttf.c: ../../data/font_bold.ttf embed.py
	python3 embed.py $< $(OUT) font_bold_ttf

$(OUT)/world_map_png.c: ../../data/world_map.png embed.py
	python3 embed.py $< $(OUT) world_map_png

$(OUT)/world_terrain_jpg.c: ../../data/world_terrain.jpg embed.py
	python3 embed.py $< $(OUT) world_terrain_jpg

$(OUT)/flags_bin.c: ../../data/flags.bin embed.py
	python3 embed.py $< $(OUT) flags_bin

$(OUT)/icons_bin.c: ../../data/icons.bin embed.py
	python3 embed.py $< $(OUT) icons_bin

$(OUT)/preview: $(OUT)/about_icon_png.c $(OUT)/brand_seal_png.c $(OUT)/brand_word_png.c preview.c stubs.c font_system_host.c $(addprefix $(SRC)/,$(UI_SRC)) $(OUT)/font_regular_ttf.c $(OUT)/font_bold_ttf.c \
		$(OUT)/world_map_png.c $(OUT)/world_terrain_jpg.c $(OUT)/flags_bin.c $(OUT)/icons_bin.c
	$(CC) $(CFLAGS) preview.c stubs.c font_system_host.c $(addprefix $(SRC)/,$(UI_SRC)) ../../third_party/cJSON.c \
		$(OUT)/about_icon_png.c $(OUT)/brand_seal_png.c $(OUT)/brand_word_png.c $(OUT)/font_regular_ttf.c $(OUT)/font_bold_ttf.c $(OUT)/world_map_png.c $(OUT)/world_terrain_jpg.c \
		$(OUT)/flags_bin.c $(OUT)/icons_bin.c -o $@ -lm

clean:
	rm -rf $(OUT)
