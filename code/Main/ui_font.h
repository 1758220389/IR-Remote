#ifndef UI_FONT_H
#define UI_FONT_H
#include <stdint.h>
/* SSD1306 page-major 16x16, 32 bytes in flash; NULL for unsupported codepoint. */
const uint8_t *UiFont_Find(uint32_t codepoint);
#endif
