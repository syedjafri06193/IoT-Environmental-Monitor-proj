#ifndef FONT6X11_H
#define FONT6X11_H
#include <stdint.h>

#define FONT_W       6
#define FONT_H       11
#define FONT_FIRST   0x20
#define FONT_GLYPHS  96      /* 0x20..0x7E plus degree sign at 0x7F */
#define FONT_DEGREE  "\x7f"

extern const uint8_t font6x11[FONT_GLYPHS][FONT_H];

#endif
