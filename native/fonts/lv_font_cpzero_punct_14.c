/*******************************************************************************
 * Size: 14 px
 * Bpp: 4
 * Opts: --no-compress --no-prefilter --bpp 4 --size 14 --font native/build/_deps/lvgl-src/scripts/built_in_font/Montserrat-Medium.ttf -r 0x2010-0x2026 -r 0x2190-0x2193 -r 0x7C=>0x2502 --format lvgl --lv-font-name lv_font_cpzero_punct_14 --lv-fallback lv_font_montserrat_14 -o native/fonts/lv_font_cpzero_punct_14.c
 ******************************************************************************/

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#ifndef LV_FONT_CPZERO_PUNCT_14
#define LV_FONT_CPZERO_PUNCT_14 1
#endif

#if LV_FONT_CPZERO_PUNCT_14

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+2010 "‐" */
    0x0, 0x0, 0x3, 0xff, 0xf9, 0x3, 0x33, 0x10,

    /* U+2012 "‒" */
    0x5e, 0xee, 0xee, 0xee, 0xe2, 0x1, 0x11, 0x11,
    0x11, 0x10,

    /* U+2013 "–" */
    0xff, 0xff, 0xff, 0xf0,

    /* U+2014 "—" */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,

    /* U+2015 "―" */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,

    /* U+2018 "‘" */
    0x9, 0x50, 0xe1, 0x3f, 0x34, 0xf6, 0x2, 0x0,

    /* U+2019 "’" */
    0x2e, 0x52, 0xf7, 0xd, 0x21, 0xd0, 0x1, 0x0,

    /* U+201A "‚" */
    0x1, 0x3, 0xf6, 0x1e, 0x60, 0xe1, 0x2c, 0x0,

    /* U+201C "“" */
    0x9, 0x50, 0xe0, 0xe, 0x14, 0xb0, 0x3f, 0x39,
    0xd0, 0x4f, 0x69, 0xf1, 0x2, 0x0, 0x10,

    /* U+201D "”" */
    0x2e, 0x57, 0xe0, 0x2f, 0x77, 0xf1, 0xd, 0x23,
    0xc0, 0x1d, 0x7, 0x70, 0x1, 0x1, 0x0,

    /* U+201E "„" */
    0x1, 0x0, 0x10, 0x3f, 0x69, 0xf1, 0x1e, 0x66,
    0xf0, 0xe, 0x14, 0xb0, 0x2c, 0x7, 0x60,

    /* U+2020 "†" */
    0x0, 0xc, 0x90, 0x0, 0x0, 0xc, 0x90, 0x0,
    0x0, 0xc, 0x90, 0x0, 0x9f, 0xff, 0xff, 0xf6,
    0x23, 0x3d, 0xa3, 0x31, 0x0, 0xc, 0x90, 0x0,
    0x0, 0xc, 0x90, 0x0, 0x0, 0xc, 0x90, 0x0,
    0x0, 0xc, 0x90, 0x0, 0x0, 0xc, 0x90, 0x0,
    0x0, 0xc, 0x90, 0x0, 0x0, 0xc, 0x90, 0x0,
    0x0, 0xc, 0x90, 0x0,

    /* U+2021 "‡" */
    0x0, 0xc, 0x90, 0x0, 0x0, 0xc, 0x90, 0x0,
    0x0, 0xc, 0x90, 0x0, 0x9f, 0xff, 0xff, 0xf6,
    0x23, 0x3d, 0xa3, 0x31, 0x0, 0xc, 0x90, 0x0,
    0x0, 0xc, 0x90, 0x0, 0x0, 0xc, 0x90, 0x0,
    0x9f, 0xff, 0xff, 0xf6, 0x23, 0x3d, 0xa3, 0x31,
    0x0, 0xc, 0x90, 0x0, 0x0, 0xc, 0x90, 0x0,
    0x0, 0xc, 0x90, 0x0,

    /* U+2022 "•" */
    0x6, 0xa1, 0xf, 0xf6, 0xb, 0xe2,

    /* U+2026 "…" */
    0x0, 0x0, 0x0, 0x0, 0x0, 0x3f, 0x50, 0xe9,
    0xa, 0xd0, 0x2e, 0x40, 0xd8, 0xa, 0xc0,

    /* U+2190 "←" */
    0x0, 0x62, 0x0, 0x0, 0x6d, 0x0, 0x0, 0x1f,
    0x52, 0x22, 0x7, 0xfe, 0xee, 0xe6, 0xd, 0x60,
    0x0, 0x0, 0x3e, 0x10, 0x0, 0x0, 0x31, 0x0,
    0x0,

    /* U+2191 "↑" */
    0x0, 0x5c, 0x30, 0x1, 0xae, 0xfe, 0x80, 0x78,
    0x3e, 0x1a, 0x30, 0x2, 0xe0, 0x0, 0x0, 0x2e,
    0x0, 0x0, 0x2, 0xe0, 0x0, 0x0, 0x3, 0x0,
    0x0,

    /* U+2192 "→" */
    0x0, 0x0, 0x72, 0x0, 0x0, 0x0, 0x7c, 0x0,
    0x2, 0x22, 0x2d, 0x70, 0xe, 0xee, 0xef, 0xe0,
    0x0, 0x0, 0x1e, 0x40, 0x0, 0x0, 0x9a, 0x0,
    0x0, 0x0, 0x40, 0x0,

    /* U+2193 "↓" */
    0x0, 0x2d, 0x0, 0x0, 0x2, 0xe0, 0x0, 0x0,
    0x2e, 0x0, 0x5, 0x32, 0xe0, 0x52, 0x3e, 0xaf,
    0xbd, 0x10, 0x1b, 0xf8, 0x0, 0x0, 0x2, 0x0,
    0x0,

    /* U+2502 "│" */
    0x8b, 0x8b, 0x8b, 0x8b, 0x8b, 0x8b, 0x8b, 0x8b,
    0x8b, 0x8b, 0x8b, 0x8b, 0x8b, 0x8b
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 86, .box_w = 5, .box_h = 3, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 8, .adv_w = 157, .box_w = 10, .box_h = 2, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 18, .adv_w = 112, .box_w = 7, .box_h = 1, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 22, .adv_w = 224, .box_w = 14, .box_h = 1, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 29, .adv_w = 224, .box_w = 14, .box_h = 1, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 36, .adv_w = 51, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 6},
    {.bitmap_index = 44, .adv_w = 51, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 6},
    {.bitmap_index = 52, .adv_w = 51, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 60, .adv_w = 93, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 6},
    {.bitmap_index = 75, .adv_w = 93, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 6},
    {.bitmap_index = 90, .adv_w = 93, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 105, .adv_w = 125, .box_w = 8, .box_h = 13, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 157, .adv_w = 125, .box_w = 8, .box_h = 13, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 209, .adv_w = 70, .box_w = 4, .box_h = 3, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 215, .adv_w = 155, .box_w = 10, .box_h = 3, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 230, .adv_w = 134, .box_w = 7, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 255, .adv_w = 134, .box_w = 7, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 280, .adv_w = 134, .box_w = 8, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 308, .adv_w = 134, .box_w = 7, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 333, .adv_w = 67, .box_w = 2, .box_h = 14, .ofs_x = 1, .ofs_y = -3}
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
    1, 0, -2, 0, 6, -4, -2, -4,
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

extern const lv_font_t lv_font_montserrat_14;


/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t lv_font_cpzero_punct_14 = {
#else
lv_font_t lv_font_cpzero_punct_14 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 14,          /*The maximum line height required by the font*/
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
    .fallback = &lv_font_montserrat_14,
#endif
    .user_data = NULL,
};



#endif /*#if LV_FONT_CPZERO_PUNCT_14*/

