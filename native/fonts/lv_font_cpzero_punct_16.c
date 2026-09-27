/*******************************************************************************
 * Size: 16 px
 * Bpp: 4
 * Opts: --no-compress --no-prefilter --bpp 4 --size 16 --font native/build/_deps/lvgl-src/scripts/built_in_font/Montserrat-Medium.ttf -r 0x2010-0x2026 -r 0x2190-0x2193 -r 0x7C=>0x2502 --format lvgl --lv-font-name lv_font_cpzero_punct_16 --lv-fallback lv_font_montserrat_16 -o native/fonts/lv_font_cpzero_punct_16.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_FONT_CPZERO_PUNCT_16
#define LV_FONT_CPZERO_PUNCT_16 1
#endif

#if LV_FONT_CPZERO_PUNCT_16

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+2010 "‐" */
    0x1, 0x11, 0x10, 0x1f, 0xff, 0xf3, 0x4, 0x44,
    0x40,

    /* U+2012 "‒" */
    0x3f, 0xff, 0xff, 0xff, 0xff, 0x70, 0x22, 0x22,
    0x22, 0x22, 0x21,

    /* U+2013 "–" */
    0xff, 0xff, 0xff, 0xff, 0x22, 0x22, 0x22, 0x22,

    /* U+2014 "—" */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,

    /* U+2015 "―" */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x22,

    /* U+2018 "‘" */
    0x6, 0xa0, 0xb6, 0xf, 0x53, 0xfc, 0x9, 0x50,

    /* U+2019 "’" */
    0x1e, 0x91, 0xfd, 0xa, 0x90, 0xe3, 0xa, 0x0,

    /* U+201A "‚" */
    0x9, 0x52, 0xfd, 0xb, 0xa0, 0xc5, 0xf, 0x0,

    /* U+201C "“" */
    0x6, 0xa0, 0x5b, 0xb, 0x60, 0xa7, 0xf, 0x50,
    0xf6, 0x3f, 0xc3, 0xfd, 0x9, 0x50, 0x96,

    /* U+201D "”" */
    0x1e, 0x90, 0xda, 0x1f, 0xd1, 0xfd, 0xa, 0x90,
    0xa9, 0xe, 0x30, 0xd4, 0xa, 0x0, 0xa0,

    /* U+201E "„" */
    0x9, 0x50, 0x85, 0x2f, 0xd2, 0xfd, 0xb, 0xa0,
    0xba, 0xc, 0x50, 0xc5, 0xf, 0x0, 0xf0,

    /* U+2020 "†" */
    0x0, 0x4, 0xf3, 0x0, 0x0, 0x0, 0x4f, 0x30,
    0x0, 0x0, 0x4, 0xf3, 0x0, 0x0, 0x11, 0x5f,
    0x51, 0x10, 0x8f, 0xff, 0xff, 0xff, 0x72, 0x44,
    0x8f, 0x74, 0x42, 0x0, 0x4, 0xf3, 0x0, 0x0,
    0x0, 0x4f, 0x30, 0x0, 0x0, 0x4, 0xf3, 0x0,
    0x0, 0x0, 0x4f, 0x30, 0x0, 0x0, 0x4, 0xf3,
    0x0, 0x0, 0x0, 0x4f, 0x30, 0x0, 0x0, 0x4,
    0xf3, 0x0, 0x0, 0x0, 0x4f, 0x30, 0x0, 0x0,
    0x4, 0xf3, 0x0, 0x0,

    /* U+2021 "‡" */
    0x0, 0x4, 0xf3, 0x0, 0x0, 0x0, 0x4f, 0x30,
    0x0, 0x0, 0x4, 0xf3, 0x0, 0x0, 0x11, 0x5f,
    0x51, 0x10, 0x8f, 0xff, 0xff, 0xff, 0x72, 0x44,
    0x8f, 0x74, 0x42, 0x0, 0x4, 0xf3, 0x0, 0x0,
    0x0, 0x4f, 0x30, 0x0, 0x0, 0x4, 0xf3, 0x0,
    0x0, 0x11, 0x5f, 0x51, 0x10, 0x8f, 0xff, 0xff,
    0xff, 0x72, 0x44, 0x8f, 0x74, 0x42, 0x0, 0x4,
    0xf3, 0x0, 0x0, 0x0, 0x4f, 0x30, 0x0, 0x0,
    0x4, 0xf3, 0x0, 0x0,

    /* U+2022 "•" */
    0x0, 0x8, 0xf8, 0xef, 0xe7, 0xf7,

    /* U+2026 "…" */
    0x3, 0x10, 0x3, 0x0, 0x13, 0x2, 0xfc, 0x7,
    0xf7, 0xb, 0xf3, 0x1e, 0x90, 0x4f, 0x50, 0x9e,
    0x10,

    /* U+2190 "←" */
    0x0, 0x5a, 0x0, 0x0, 0x1, 0xe6, 0x0, 0x0,
    0xb, 0xc0, 0x0, 0x0, 0x4f, 0xff, 0xff, 0xf8,
    0xd, 0xb3, 0x33, 0x31, 0x3, 0xf4, 0x0, 0x0,
    0x0, 0x7c, 0x0, 0x0,

    /* U+2191 "↑" */
    0x0, 0x2c, 0x60, 0x0, 0x7f, 0xff, 0xb1, 0x8d,
    0x4e, 0x6a, 0xe3, 0x0, 0xe5, 0x4, 0x0, 0xe,
    0x50, 0x0, 0x0, 0xe5, 0x0, 0x0, 0xe, 0x50,
    0x0, 0x0, 0x10, 0x0,

    /* U+2192 "→" */
    0x0, 0x3, 0xa0, 0x0, 0x0, 0x0, 0xca, 0x0,
    0x0, 0x0, 0x3f, 0x40, 0xef, 0xff, 0xff, 0xe0,
    0x23, 0x33, 0x4f, 0x70, 0x0, 0x0, 0xac, 0x0,
    0x0, 0x3, 0xe2, 0x0,

    /* U+2193 "↓" */
    0x0, 0xc, 0x50, 0x0, 0x0, 0xd5, 0x0, 0x0,
    0xd, 0x50, 0x1, 0x0, 0xd5, 0x1, 0x8b, 0x1d,
    0x56, 0xf1, 0xaf, 0xfe, 0xe4, 0x0, 0x5e, 0xb1,
    0x0, 0x0, 0x0, 0x0,

    /* U+2502 "│" */
    0x5f, 0x15, 0xf1, 0x5f, 0x15, 0xf1, 0x5f, 0x15,
    0xf1, 0x5f, 0x15, 0xf1, 0x5f, 0x15, 0xf1, 0x5f,
    0x15, 0xf1, 0x5f, 0x15, 0xf1, 0x5f, 0x10
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 98, .box_w = 6, .box_h = 3, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 9, .adv_w = 179, .box_w = 11, .box_h = 2, .ofs_x = 0, .ofs_y = 5},
    {.bitmap_index = 20, .adv_w = 128, .box_w = 8, .box_h = 2, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 28, .adv_w = 256, .box_w = 16, .box_h = 2, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 44, .adv_w = 256, .box_w = 16, .box_h = 2, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 60, .adv_w = 58, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 7},
    {.bitmap_index = 68, .adv_w = 58, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 7},
    {.bitmap_index = 76, .adv_w = 58, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 84, .adv_w = 106, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 7},
    {.bitmap_index = 99, .adv_w = 106, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 7},
    {.bitmap_index = 114, .adv_w = 106, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 129, .adv_w = 143, .box_w = 9, .box_h = 15, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 197, .adv_w = 143, .box_w = 9, .box_h = 15, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 265, .adv_w = 80, .box_w = 3, .box_h = 4, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 271, .adv_w = 177, .box_w = 11, .box_h = 3, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 288, .adv_w = 154, .box_w = 8, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 316, .adv_w = 154, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 344, .adv_w = 154, .box_w = 8, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 372, .adv_w = 154, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 400, .adv_w = 77, .box_w = 3, .box_h = 15, .ofs_x = 1, .ofs_y = -3}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint16_t unicode_list_0[] = {
    0x0, 0x2, 0x3, 0x4, 0x5, 0x8, 0x9, 0xa,
    0xc, 0xd, 0xe, 0x10, 0x11, 0x12, 0x16, 0x180,
    0x181, 0x182, 0x183, 0x4f2
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 8208, .range_length = 1267, .glyph_id_start = 1,
        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = 20, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Map glyph_ids to kern left classes*/
static const uint8_t kern_left_class_mapping[] =
{
    0, 1, 0, 0, 0, 0, 2, 2,
    3, 2, 2, 3, 0, 0, 1, 3,
    0, 0, 0, 0, 0
};

/*Map glyph_ids to kern right classes*/
static const uint8_t kern_right_class_mapping[] =
{
    0, 1, 0, 0, 0, 0, 2, 2,
    3, 2, 2, 3, 0, 0, 1, 3,
    0, 0, 0, 0, 0
};

/*Kern values between classes*/
static const int8_t kern_class_values[] =
{
    1, 0, -2, 0, 7, -5, -2, -5,
    0
};


/*Collect the kern class' data in one place*/
static const lv_font_fmt_txt_kern_classes_t kern_classes =
{
    .class_pair_values   = kern_class_values,
    .left_class_mapping  = kern_left_class_mapping,
    .right_class_mapping = kern_right_class_mapping,
    .left_class_cnt      = 3,
    .right_class_cnt     = 3,
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = &kern_classes,
    .kern_scale = 16,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 1,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};

extern const lv_font_t lv_font_montserrat_16;


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t lv_font_cpzero_punct_16 = {
#else
lv_font_t lv_font_cpzero_punct_16 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 15,          /*The maximum line height required by the font*/
    .base_line = 3,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = &lv_font_montserrat_16,
#endif
    .user_data = NULL,
};



#endif /*#if LV_FONT_CPZERO_PUNCT_16*/

