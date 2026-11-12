#include "ui_view.h"
#include "OLED.h"
#include "ui_font.h"

/* Read one scalar without reading past a terminating NUL. Invalid input
 * consumes one byte and becomes '?'; valid unsupported scalars do so once. */
static uint32_t next_codepoint(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    uint32_t value, minimum;
    unsigned length, i;
    unsigned char lead = s[0];
    *text += 1;
    if (lead < 0x80) return lead;
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2; value = lead & 0x1F; minimum = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3; value = lead & 0x0F; minimum = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4; value = lead & 0x07; minimum = 0x10000;
    } else return '?';
    for (i = 1; i < length; ++i) {
        if ((s[i] & 0xC0) != 0x80) return '?';
        value = (value << 6) | (s[i] & 0x3F);
    }
    if (value < minimum || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF)) return '?';
    *text += length - 1;
    return value;
}

void UiView_Render(const UiViewModel *model)
{
    uint8_t row;
    OLED_Clear();
    if (model != 0) {
        for (row = 0; row < 4; ++row) {
            const char *s = model->lines[row];
            uint16_t x = 0;
            if (s == 0 || (!model->blink_visible &&
                (model->blink_rows & (1u << row)))) continue;
            while (*s != '\0' && x + 8 <= 128) {
                uint32_t codepoint = next_codepoint(&s);
                const uint8_t *glyph = UiFont_Find(codepoint);
                if (glyph != 0) {
                    if (x + 16 > 128) break;
                    OLED_ShowImage((int16_t)x, row * 16, 16, 16, glyph);
                    x += 16;
                } else {
                    char c = codepoint >= ' ' && codepoint <= '~' ? (char)codepoint : '?';
                    OLED_ShowChar((int16_t)x, row * 16, c, OLED_8X16);
                    x += 8;
                }
            }
        }
    }
    OLED_Update();
}
