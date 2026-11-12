#ifndef UI_VIEW_H
#define UI_VIEW_H
#include <stdint.h>
/* Four UTF-8 rows at y=0,16,32,48. ASCII=8px; Chinese=16px.
 * Text is left aligned, clipped at 128px, never wrapped. NULL rows are blank.
 * Unknown Unicode scalars and invalid UTF-8 bytes render as ASCII '?'.
 * Bits 0..3 of blink_rows hide corresponding rows when blink_visible is zero.
 * Caller owns strings and blink timing. Render clears and updates OLED once.
 * No OLED initialization, state-machine dependency, or additional framebuffer.
 */
typedef struct {
    const char *lines[4];
    uint8_t blink_rows;
    uint8_t blink_visible;
} UiViewModel;
void UiView_Render(const UiViewModel *model);
#endif
