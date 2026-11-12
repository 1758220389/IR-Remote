#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "OLED.h"
#include "ui_view.h"
#include "ui_presenter.h"

static unsigned char frame[8][128];
static unsigned clears, updates, chars, images, replacements;
void OLED_Clear(void) { memset(frame, 0, sizeof frame); ++clears; }
void OLED_Update(void) { ++updates; }
void OLED_ShowImage(int16_t x, int16_t y, uint8_t w, uint8_t h, const uint8_t *data)
{
    unsigned i, j;
    assert(x >= 0 && y >= 0 && x + w <= 128 && y + h <= 64);
    ++images;
    for (j = 0; j < h; ++j) for (i = 0; i < w; ++i)
        if (data[(j / 8) * w + i] & (1u << (j % 8)))
            frame[(y + j) / 8][x + i] |= (unsigned char)(1u << ((y + j) % 8));
}
void OLED_ShowChar(int16_t x, int16_t y, char c, uint8_t size)
{
    assert(c >= ' ' && c <= '~' && size == OLED_8X16);
    ++chars;
    if (c == '?') ++replacements;
    OLED_ShowImage(x, y, 8, 16, OLED_F8x16[(unsigned)c - ' ']);
}
static void test_ascii(void)
{
    UiViewModel model = {{"Config 1", "12345678901234567890", 0, "OK"}, 0, 1};
    UiView_Render(&model);
    assert(clears == 1 && updates == 1 && chars == 26);
    assert(frame[0][0] || frame[1][0]);
    puts("ascii layout and clipping: PASS");
}
static void test_chinese(void)
{
    UiViewModel model = {{"配置1", "长按确认切换", "学习", "接收中"}, 0, 1};
    chars = images = 0;
    UiView_Render(&model);
    assert(chars == 1 && images == 14); /* 13 Chinese images and ASCII digit */
    assert(frame[0][1] || frame[1][1]);
    puts("Chinese UTF-8 glyph rendering: PASS");
}
static void test_blink(void)
{
    UiViewModel model = {{"配置1", "长按确认切换", "学习", "接收中"}, 5, 0};
    chars = images = 0;
    UiView_Render(&model);
    assert(chars == 0 && images == 9);
    assert(memcmp(frame[0], (unsigned char[128]){0}, 128) == 0);
    model.blink_visible = 1;
    chars = images = 0;
    UiView_Render(&model);
    assert(chars == 1 && images == 14);
    UiView_Render(0);
    assert(memcmp(frame, (unsigned char[8][128]){{0}}, sizeof frame) == 0);
    puts("row blink, restore and NULL clear: PASS");
}
static void test_utf8_edges(void)
{
    static const char *cases[] = {"\xE9\xBE\x99", "\xF0\x9F\x98\x80", "\xE9", "\xE9\x85", "\xC0\xAF", "\xED\xA0\x80", "\x01"};
    static const unsigned counts[] = {1, 1, 1, 2, 2, 3, 1};
    unsigned i;
    for (i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        UiViewModel model = {{cases[i], 0, 0, 0}, 0, 1};
        chars = images = 0;
        UiView_Render(&model);
        assert(chars == counts[i] && images == counts[i]);
    }
    {
        UiViewModel model = {{"配置配置配置配置配", "123456789012345配", 0, 0}, 0, 1};
        chars = images = 0;
        UiView_Render(&model);
        assert(chars == 15 && images == 23);
    }
    puts("unsupported/malformed UTF-8 and mixed-width clipping: PASS");
}
static void export_pages(const char *directory)
{
    unsigned page, x, y;
    for (page = 0; page < 11; ++page) {
        char path[1024];
        FILE *file;
        UiState state;
        UiPresentation presentation;
        Ui_Init(&state, 0, 0);
        if (page < 3) state.active_profile = (uint8_t)page;
        else if (page < 5) { state.page=UI_PAGE_SETTINGS; state.selection=(uint8_t)(page-3); }
        else if (page == 5) state.page=UI_PAGE_RECEIVING;
        else if (page == 6) state.page=UI_PAGE_RECEIVED;
        else if (page == 7) { state.page=UI_PAGE_RECEIVING; state.learned_mask=1; }
        else if (page == 8) state.page=UI_PAGE_SAVE_SELECT;
        else { state.page=UI_PAGE_RESULT; state.result=page==9 ? UI_RESULT_SAVED_RAM : UI_RESULT_CANCELLED; }
        UiPresenter_Build(&state, &presentation);
        replacements = 0;
        UiView_Render(&presentation.view);
        assert(replacements == 0);
        assert(snprintf(path, sizeof path, "%s/ui_page_%02u.pbm", directory, page) < (int)sizeof path);
        file = fopen(path, "wb"); assert(file != 0);
        fprintf(file, "P1\n128 64\n");
        for (y = 0; y < 64; ++y) {
            for (x = 0; x < 128; ++x)
                fprintf(file, "%u ", (frame[y / 8][x] >> (y % 8)) & 1u);
            fputc('\n', file);
        }
        assert(fclose(file) == 0);
    }
    puts("sample page bitmaps exported: PASS");
}
int main(int argc, char **argv)
{
    test_ascii(); test_chinese(); test_blink(); test_utf8_edges();
    if (argc > 1) export_pages(argv[1]);
    return 0;
}
