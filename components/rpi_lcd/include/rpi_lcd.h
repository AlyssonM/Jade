#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Basic init sequence (pins + SPI + display)
void tft_init_pins(void);
void tft_init_spi(void);
void tft_reset(void);
void tft_init_display(void);

// Simple drawing helpers
void tft_fill_screen(uint16_t color);
void tft_fill_rect(int x, int y, int w, int h, uint16_t color);

// Draw a horizontal line of RGB565 pixels from a buffer
void tft_draw_rgb565_hline(int x, int y, const uint16_t *line, int w);

// Touch helpers (XPT2046 on same SPI bus)
//  - rpi_lcd_touch_read_raw: devolve valores crus de 0-4095
//  - rpi_lcd_touch_read_pixel: devolve coordenadas (x,y) na resolu��ǜo do TFT
bool rpi_lcd_touch_read_raw(uint16_t *x, uint16_t *y);
bool rpi_lcd_touch_read_pixel(uint16_t *x, uint16_t *y);

// Função simples de validação/calibração visual do touch
void rpi_lcd_touch_test(void);

// Optional test helpers (color sweep and rectangles)
void test_simple(void);
void test_rectangles(void);

#ifdef __cplusplus
}
#endif
