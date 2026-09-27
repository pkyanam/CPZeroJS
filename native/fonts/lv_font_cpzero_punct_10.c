/*******************************************************************************
 * Size: 10 px
 * Bpp: 4
 * Opts: --no-compress --no-prefilter --bpp 4 --size 10 --font native/build/_deps/lvgl-src/scripts/built_in_font/Montserrat-Medium.ttf -r 0x2010-0x2026 -r 0x2190-0x2193 -r 0x7C=>0x2502 --format lvgl --lv-font-name lv_font_cpzero_punct_10 --lv-fallback lv_font_montserrat_10 -o native/fonts/lv_font_cpzero_punct_10.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_FONT_CPZERO_PUNCT_10
#define LV_FONT_CPZERO_PUNCT_10 1
#endif

#if LV_FONT_CPZERO_PUNCT_10

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+2010 "‐" */
    0x5c, 0xc3,

    /* U+2012 "‒" */
    0x5a, 0xaa, 0xaa, 0x50,

    /* U+2013 "–" */
    0xaa, 0xaa, 0xa0,

    /* U+2014 "—" */
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa,

    /* U+2015 "―" */
    0xaa, 0xaa, 0xaa, 0xaa, 0xaa,

    /* U+2018 "‘" */
    0x19, 0x69, 0x35,

    /* U+2019 "’" */
    0x6a, 0x39, 0x32,

    /* U+201A "‚" */
    0x35, 0x4a, 0x55,

    /* U+201C "“" */
    0x19, 0x27, 0x69, 0x87, 0x35, 0x44,

    /* U+201D "”" */
    0x6a, 0x78, 0x39, 0x57, 0x32, 0x41,

    /* U+201E "„" */
    0x35, 0x44, 0x4a, 0x69, 0x55, 0x73,

    /* U+2020 "†" */
    0x0, 0xb4, 0x0, 0x0, 0xb4, 0x0, 0x0, 0xb4,
    0x0, 0x8c, 0xed, 0xc3, 0x0, 0xb4, 0x0, 0x0,
    0xb4, 0x0, 0x0, 0xb4, 0x0, 0x0, 0xb4, 0x0,
    0x0, 0xb4, 0x0,

    /* U+2021 "‡" */
    0x0, 0xb4, 0x0, 0x0, 0xb4, 0x0, 0x0, 0xb4,
    0x0, 0x8c, 0xed, 0xc3, 0x0, 0xb4, 0x0, 0x0,
    0xb4, 0x0, 0x8c, 0xed, 0xc3, 0x0, 0xb4, 0x0,
    0x0, 0xb4, 0x0,

    /* U+2022 "•" */
    0x19, 0x23, 0xe4,

    /* U+2026 "…" */
    0x2, 0x2, 0x2, 0x6, 0xa1, 0xe0, 0xb4,

    /* U+2190 "←" */
    0x0, 0x90, 0x0, 0x7, 0x70, 0x0, 0xd, 0xcb,
    0xb3, 0x4, 0xa0, 0x0, 0x0, 0x50, 0x0,

    /* U+2191 "↑" */
    0x2, 0xb7, 0x2, 0xbb, 0x8a, 0x0, 0x92, 0x0,
    0x9, 0x20, 0x0, 0x41, 0x0,

    /* U+2192 "→" */
    0x0, 0x9, 0x0, 0x0, 0x7, 0x70, 0x3b, 0xbc,
    0xd0, 0x0, 0xa, 0x40, 0x0, 0x5, 0x0,

    /* U+2193 "↓" */
    0x0, 0xa0, 0x0, 0x1b, 0x0, 0x51, 0xb3, 0x33,
    0xce, 0xa1, 0x0, 0x40, 0x0,

    /* U+2502 "│" */
    0xee, 0xee, 0xee, 0xee, 0xe0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 61, .box_w = 4, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 2, .adv_w = 112, .box_w = 7, .box_h = 1, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 6, .adv_w = 80, .box_w = 5, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 9, .adv_w = 160, .box_w = 10, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 14, .adv_w = 160, .box_w = 10, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 19, .adv_w = 36, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 22, .adv_w = 36, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 25, .adv_w = 36, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 28, .adv_w = 67, .box_w = 4, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 34, .adv_w = 67, .box_w = 4, .box_h = 3, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 40, .adv_w = 67, .box_w = 4, .box_h = 3, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 46, .adv_w = 90, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 73, .adv_w = 90, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 100, .adv_w = 50, .box_w = 3, .box_h = 2, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 103, .adv_w = 111, .box_w = 7, .box_h = 2, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 110, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 125, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 138, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 153, .adv_w = 96, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 166, .adv_w = 48, .box_w = 1, .box_h = 9, .ofs_x = 1, .ofs_y = -2}
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
    0, 0, -1, 0, 4, -3, -1, -3,
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

extern const lv_font_t lv_font_montserrat_10;


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t lv_font_cpzero_punct_10 = {
#else
lv_font_t lv_font_cpzero_punct_10 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 9,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = &lv_font_montserrat_10,
#endif
    .user_data = NULL,
};



#endif /*#if LV_FONT_CPZERO_PUNCT_10*/

