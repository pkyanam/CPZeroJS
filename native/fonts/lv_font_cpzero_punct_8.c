/*******************************************************************************
 * Size: 8 px
 * Bpp: 4
 * Opts: --no-compress --no-prefilter --bpp 4 --size 8 --font native/build/_deps/lvgl-src/scripts/built_in_font/Montserrat-Medium.ttf -r 0x2010-0x2026 -r 0x2190-0x2193 -r 0x7C=>0x2502 --format lvgl --lv-font-name lv_font_cpzero_punct_8 --lv-fallback lv_font_montserrat_8 -o native/fonts/lv_font_cpzero_punct_8.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_FONT_CPZERO_PUNCT_8
#define LV_FONT_CPZERO_PUNCT_8 1
#endif

#if LV_FONT_CPZERO_PUNCT_8

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+2010 "‐" */
    0x5a, 0x60,

    /* U+2012 "‒" */
    0x58, 0x88, 0x81,

    /* U+2013 "–" */
    0x88, 0x88,

    /* U+2014 "—" */
    0x88, 0x88, 0x88, 0x88,

    /* U+2015 "―" */
    0x88, 0x88, 0x88, 0x88,

    /* U+2018 "‘" */
    0x44, 0x84, 0x0,

    /* U+2019 "’" */
    0x75, 0x62, 0x0,

    /* U+201A "‚" */
    0x0, 0x75, 0x71,

    /* U+201C "“" */
    0x44, 0x88, 0x5c, 0x0, 0x0,

    /* U+201D "”" */
    0x75, 0xc6, 0x28, 0x0, 0x0,

    /* U+201E "„" */
    0x0, 0x7, 0x6d, 0x71, 0x80,

    /* U+2020 "†" */
    0x2, 0x90, 0x7, 0xad, 0xa2, 0x2, 0x90, 0x0,
    0x29, 0x0, 0x2, 0x90, 0x0, 0x29, 0x0,

    /* U+2021 "‡" */
    0x2, 0x90, 0x7, 0xad, 0xa2, 0x2, 0x90, 0x7,
    0xad, 0xa2, 0x2, 0x90, 0x0, 0x29, 0x0,

    /* U+2022 "•" */
    0x0, 0x5d, 0x2,

    /* U+2026 "…" */
    0x0, 0x0, 0x0, 0x74, 0x92, 0xb0,

    /* U+2190 "←" */
    0x6, 0x40, 0x1, 0xe9, 0x92, 0xa, 0x10, 0x0,
    0x12, 0x0,

    /* U+2191 "↑" */
    0x5, 0xc3, 0x2, 0x59, 0x70, 0x0, 0x90, 0x0,
    0x5, 0x0,

    /* U+2192 "→" */
    0x0, 0x73, 0x4, 0x99, 0xd0, 0x0, 0x37, 0x0,
    0x3, 0x0,

    /* U+2193 "↓" */
    0x0, 0x90, 0x0, 0xa, 0x0, 0x1a, 0xca, 0x0,
    0x6, 0x0,

    /* U+2502 "│" */
    0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 49, .box_w = 3, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 2, .adv_w = 90, .box_w = 6, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 5, .adv_w = 64, .box_w = 4, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 7, .adv_w = 128, .box_w = 8, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 11, .adv_w = 128, .box_w = 8, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 15, .adv_w = 29, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 18, .adv_w = 29, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 21, .adv_w = 29, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 24, .adv_w = 53, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 29, .adv_w = 53, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 34, .adv_w = 53, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 39, .adv_w = 72, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 54, .adv_w = 72, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 69, .adv_w = 40, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 72, .adv_w = 88, .box_w = 6, .box_h = 2, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 78, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 88, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 98, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 108, .adv_w = 77, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 118, .adv_w = 38, .box_w = 2, .box_h = 7, .ofs_x = 0, .ofs_y = -1}
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
    0, 0, -1, 0, 4, -2, -1, -2,
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

extern const lv_font_t lv_font_montserrat_8;


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t lv_font_cpzero_punct_8 = {
#else
lv_font_t lv_font_cpzero_punct_8 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 7,          /*The maximum line height required by the font*/
    .base_line = 1,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 0,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = &lv_font_montserrat_8,
#endif
    .user_data = NULL,
};



#endif /*#if LV_FONT_CPZERO_PUNCT_8*/

