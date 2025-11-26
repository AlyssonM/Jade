#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

// Pin definitions (aligned with ESP32CAM wallet wiring)
// Pinagem do LCD RPI ili9486
//  11  TP_IRQ          Touch Panel interrupt          (not used yet)
//  18  LCD_RS          Instruction/Data Register selection
//  19  LCD_SI / TP_SI  SPI MOSI of LCD/Touch Panel
//  21  TP_SO           SPI MISO of Touch Panel
//  22  RST             Reset
//  23  LCD_SCK / TP_SCK SPI clock of LCD/Touch Panel
//  24  LCD_CS          LCD chip selection, low active
//  26  TP_CS           Touch Panel chip selection, low active

// TFT signals (SPI2_HOST)
#define TFT_MOSI    13
#define TFT_SCLK    14
#define TFT_CS      12
#define TFT_DC      2
#define TFT_RST     -1

// Touch controller (XPT2046) signals sharing same SPI bus
#define TP_CS       15   // Chip select do touch
#define TP_MISO     4   // MISO do touch

// Display dimensions (landscape)
#define TFT_WIDTH   480
#define TFT_HEIGHT  320

// ILI9486 Commands
#define ILI9486_NOP         0x00
#define ILI9486_SWRESET     0x01
#define ILI9486_RDDID       0x04
#define ILI9486_RDDST       0x09
#define ILI9486_SLPIN       0x10
#define ILI9486_SLPOUT      0x11
#define ILI9486_PTLON       0x12
#define ILI9486_NORON       0x13
#define ILI9486_INVOFF      0x20
#define ILI9486_INVON       0x21
#define ILI9486_GAMMASET    0x26
#define ILI9486_DISPOFF     0x28
#define ILI9486_DISPON      0x29
#define ILI9486_CASET       0x2A
#define ILI9486_PASET       0x2B
#define ILI9486_RAMWR       0x2C
#define ILI9486_RAMRD       0x2E
#define ILI9486_MADCTL      0x36
#define ILI9486_PIXFMT      0x3A
#define ILI9486_FRMCTR1     0xB1
#define ILI9486_FRMCTR2     0xB2
#define ILI9486_FRMCTR3     0xB3
#define ILI9486_INVCTR      0xB4
#define ILI9486_DFUNCTR     0xB6
#define ILI9486_PWCTR1      0xC0
#define ILI9486_PWCTR2      0xC1
#define ILI9486_PWCTR3      0xC2
#define ILI9486_PWCTR4      0xC3
#define ILI9486_PWCTR5      0xC4
#define ILI9486_VMCTR1      0xC5
#define ILI9486_VMCTR2      0xC7
#define ILI9486_GMCTRP1     0xE0
#define ILI9486_GMCTRN1     0xE1

// RGB565 Colors
#define TFT_BLACK       0x0000
#define TFT_BLUE        0x001F
#define TFT_RED         0xF800
#define TFT_GREEN       0x07E0
#define TFT_CYAN        0x07FF
#define TFT_MAGENTA     0xF81F
#define TFT_YELLOW      0xFFE0
#define TFT_WHITE       0xFFFF

static const char *TAG = "TFT";
static spi_device_handle_t spi;
static spi_device_handle_t spi_touch;
static SemaphoreHandle_t spi_mutex = NULL;

// XPT2046 commands (12-bit measurements)
#define XPT2046_CMD_READ_X   0xD0
#define XPT2046_CMD_READ_Y   0x90

// Raw value range expected when touching; used for simple filtering
#define TOUCH_RAW_MIN        200
#define TOUCH_RAW_MAX        3800

// ===== Low-level SPI functions =====

static inline void spi_lock(void)
{
    if (spi_mutex) {
        xSemaphoreTake(spi_mutex, portMAX_DELAY);
    }
}

static inline void spi_unlock(void)
{
    if (spi_mutex) {
        xSemaphoreGive(spi_mutex);
    }
}

static void tft_spi_pre_transfer_callback(spi_transaction_t *t)
{
    int dc = (int)t->user;
    gpio_set_level(TFT_DC, dc);
}

static void tft_cmd(uint8_t cmd)
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &cmd;
    t.user = (void*)0;  // DC low for command
    spi_lock();
    ret = spi_device_transmit(spi, &t);
    spi_unlock();
    assert(ret == ESP_OK);
}

static void tft_data(uint8_t data)
{
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = 8;
    t.tx_buffer = &data;
    t.user = (void*)1;  // DC high for data
    spi_lock();
    ret = spi_device_transmit(spi, &t);
    spi_unlock();
    assert(ret == ESP_OK);
}

static void tft_data_buf(uint8_t *data, int len)
{
    if (len == 0) return;
    esp_err_t ret;
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = len * 8;
    t.tx_buffer = data;
    t.user = (void*)1;  // DC high for data
    spi_lock();
    ret = spi_device_transmit(spi, &t);
    spi_unlock();
    assert(ret == ESP_OK);
}

// ===== Hardware initialization =====

void tft_init_pins(void)
{
    gpio_set_direction(TFT_DC, GPIO_MODE_OUTPUT);
    if (TFT_RST != -1)
    {
        gpio_set_direction(TFT_RST, GPIO_MODE_OUTPUT);
    }
    ESP_LOGI(TAG, "GPIO pins configured");
}

void tft_reset(void)
{
    if (TFT_RST == -1) return;
    ESP_LOGI(TAG, "Hardware reset...");
    gpio_set_level(TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_LOGI(TAG, "Reset complete");
}

void tft_init_spi(void)
{
    esp_err_t ret;

    // --- TFT on SPI2_HOST -------------------------------------------------
    spi_bus_config_t buscfg = {
        .miso_io_num = TP_MISO,
        .mosi_io_num = TFT_MOSI,
        .sclk_io_num = TFT_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = TFT_WIDTH * 2 * 20,
        .flags = 0
    };

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 12 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = TFT_CS,
        .queue_size = 7,
        .pre_cb = tft_spi_pre_transfer_callback,
        .flags = 0
    };

    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    // --- Touch (XPT2046) on SPI3_HOST ------------------------------------
    // Reutiliza os mesmos pinos físicos (MOSI/SCLK/MISO), mas em um host
    // SPI separado para evitar conflitos internos do driver/HAL.
    spi_bus_config_t touch_buscfg = {
        .miso_io_num = TP_MISO,
        .mosi_io_num = TFT_MOSI,
        .sclk_io_num = TFT_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 3,      // comando + 2 bytes de resposta
        .flags = 0
    };

    spi_device_interface_config_t touchcfg = {
        .clock_speed_hz = 4 * 1000 * 1000, // 1 MHz is enough for XPT2046
        .mode = 0,
        .spics_io_num = TP_CS,
        .queue_size = 1,
        .pre_cb = NULL,
        .flags = 0
    };

    // Touch uses same SPI2 bus; no second bus init
    ret = spi_bus_add_device(SPI2_HOST, &touchcfg, &spi_touch);
    ESP_ERROR_CHECK(ret);

    spi_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(spi_mutex ? ESP_OK : ESP_FAIL);

    ESP_LOGI(TAG, "SPI bus initialized (TFT@SPI2 10MHz, Touch@SPI2 1MHz)");
}

// ===== Display initialization (COMPLETE SEQUENCE) =====

void tft_init_display(void)
{
    ESP_LOGI(TAG, "Starting ILI9486 initialization...");

    // Software Reset
    tft_cmd(ILI9486_SWRESET);
    vTaskDelay(pdMS_TO_TICKS(150));

    // Interface Mode Control
    tft_cmd(0xB0);
    tft_data(0x00);  // SDO NOT USE

    // Sleep Out
    tft_cmd(ILI9486_SLPOUT);
    vTaskDelay(pdMS_TO_TICKS(120));

    // Interface Pixel Format (16-bit RGB565)
    tft_cmd(ILI9486_PIXFMT);
    tft_data(0x55);

    // Power Control 1
    tft_cmd(ILI9486_PWCTR1);
    tft_data(0x19);
    tft_data(0x1A);

    // Power Control 2
    tft_cmd(ILI9486_PWCTR2);
    tft_data(0x45);
    tft_data(0x00);

    // Power Control 3 (Normal Mode)
    tft_cmd(ILI9486_PWCTR3);
    tft_data(0x33);

    // VCOM Control
    tft_cmd(ILI9486_VMCTR1);
    tft_data(0x00);
    tft_data(0x12);
    tft_data(0x80);

    // Memory Access Control - LANDSCAPE with BGR
    tft_cmd(ILI9486_MADCTL);
    // MV=1 (landscape), BGR=1 (panel wiring), colors are provided in Jade's native format
    tft_data(0x28);

    // Display Function Control
    tft_cmd(ILI9486_DFUNCTR);
    tft_data(0x00);
    tft_data(0x02);
    tft_data(0x3B);

    // Frame Rate Control (Normal Mode)
    tft_cmd(ILI9486_FRMCTR1);
    tft_data(0xB0);
    tft_data(0x11);

    // Display Inversion Control
    tft_cmd(ILI9486_INVCTR);
    tft_data(0x02);

    // Positive Gamma Control
    tft_cmd(ILI9486_GMCTRP1);
    tft_data(0x0F); tft_data(0x24); tft_data(0x1C); tft_data(0x0A); tft_data(0x0F);
    tft_data(0x08); tft_data(0x43); tft_data(0x88); tft_data(0x32); tft_data(0x0F);
    tft_data(0x10); tft_data(0x06); tft_data(0x0F); tft_data(0x07); tft_data(0x00);

    // Negative Gamma Control
    tft_cmd(ILI9486_GMCTRN1);
    tft_data(0x0F); tft_data(0x38); tft_data(0x30); tft_data(0x09); tft_data(0x0F);
    tft_data(0x0F); tft_data(0x4E); tft_data(0x77); tft_data(0x3C); tft_data(0x07);
    tft_data(0x10); tft_data(0x05); tft_data(0x23); tft_data(0x1B); tft_data(0x00);

    // Display ON
    tft_cmd(ILI9486_DISPON);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "ILI9486 initialization complete");
}

// ===== Drawing helpers =====

static void tft_set_addr_window(int x0, int y0, int x1, int y1)
{
    tft_cmd(ILI9486_CASET);
    tft_data(x0 >> 8);
    tft_data(x0 & 0xFF);
    tft_data(x1 >> 8);
    tft_data(x1 & 0xFF);

    tft_cmd(ILI9486_PASET);
    tft_data(y0 >> 8);
    tft_data(y0 & 0xFF);
    tft_data(y1 >> 8);
    tft_data(y1 & 0xFF);

    tft_cmd(ILI9486_RAMWR);
}

void tft_fill_screen(uint16_t color)
{
    ESP_LOGI(TAG, "Filling screen with color 0x%04X", color);
    tft_set_addr_window(0, 0, TFT_WIDTH - 1, TFT_HEIGHT - 1);

    uint8_t line[TFT_WIDTH * 2];
    for (int i = 0; i < TFT_WIDTH; i++) {
        line[i * 2] = color >> 8;
        line[i * 2 + 1] = color & 0xFF;
    }

    gpio_set_level(TFT_DC, 1);
    for (int y = 0; y < TFT_HEIGHT; y++) {
        tft_data_buf(line, TFT_WIDTH * 2);
    }
}

void tft_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x >= TFT_WIDTH || y >= TFT_HEIGHT || w <= 0 || h <= 0) return;
    if (x + w > TFT_WIDTH) w = TFT_WIDTH - x;
    if (y + h > TFT_HEIGHT) h = TFT_HEIGHT - y;

    tft_set_addr_window(x, y, x + w - 1, y + h - 1);

    uint8_t line[w * 2];
    for (int i = 0; i < w; i++) {
        line[i * 2] = color >> 8;
        line[i * 2 + 1] = color & 0xFF;
    }

    gpio_set_level(TFT_DC, 1);
    for (int i = 0; i < h; i++) {
        tft_data_buf(line, w * 2);
    }
}

void tft_draw_rgb565_hline(int x, int y, const uint16_t *line, int w)
{
    if (x < 0 || y < 0 || x >= TFT_WIDTH || y >= TFT_HEIGHT || w <= 0) {
        return;
    }
    if (x + w > TFT_WIDTH) {
        w = TFT_WIDTH - x;
    }

    tft_set_addr_window(x, y, x + w - 1, y);

    uint8_t buf[w * 2];
    for (int i = 0; i < w; i++) {
        uint16_t c = line[i];
        // Jade's color_t values are 16-bit little-endian; send low byte first.
        buf[i * 2]     = (uint8_t)(c & 0xFF);
        buf[i * 2 + 1] = (uint8_t)(c >> 8);
    }

    gpio_set_level(TFT_DC, 1);
    tft_data_buf(buf, w * 2);
}

// ===== Touch (XPT2046) helpers =====

static uint16_t xpt2046_read_raw(uint8_t command)
{
    spi_transaction_t t;
    uint8_t tx[3] = { command, 0x00, 0x00 };
    uint8_t rx[3] = { 0, 0, 0 };

    memset(&t, 0, sizeof(t));
    t.length = 8 * sizeof(tx);
    t.tx_buffer = tx;
    t.rx_buffer = rx;

    spi_lock();
    esp_err_t ret = spi_device_transmit(spi_touch, &t);
    spi_unlock();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erro SPI no touch: %d", ret);
        return 0;
    }

    // XPT2046 devolve 12 bits de dado alinhados nos bits mais significativos
    uint16_t value = ((uint16_t)rx[1] << 8) | rx[2];
    value >>= 3;
    return value;
}

bool rpi_lcd_touch_read_raw(uint16_t *x, uint16_t *y)
{
    if (!x || !y) {
        return false;
    }

    // Faz vǽrias leituras e usa a mǭdia simples para reduzir ruǭdo
    const int samples = 4;
    uint32_t sum_x = 0;
    uint32_t sum_y = 0;

    for (int i = 0; i < samples; i++) {
        uint16_t raw_x = xpt2046_read_raw(XPT2046_CMD_READ_X);
        uint16_t raw_y = xpt2046_read_raw(XPT2046_CMD_READ_Y);
        sum_x += raw_x;
        sum_y += raw_y;
    }

    uint16_t avg_x = (uint16_t)(sum_x / samples);
    uint16_t avg_y = (uint16_t)(sum_y / samples);

    if (avg_x < TOUCH_RAW_MIN || avg_x > TOUCH_RAW_MAX ||
        avg_y < TOUCH_RAW_MIN || avg_y > TOUCH_RAW_MAX) {
        return false; // provavelmente sem toque
    }

    *x = avg_x;
    *y = avg_y;
    return true;
}

bool rpi_lcd_touch_read_pixel(uint16_t *x, uint16_t *y)
{
    uint16_t raw_x, raw_y;
    if (!rpi_lcd_touch_read_raw(&raw_x, &raw_y)) {
        return false;
    }

    // Conversǭo simples raw -> coordenada de tela.
    // Ajuste TOUCH_RAW_MIN/MAX conforme calibra��ǜo real.
    int span_x = TOUCH_RAW_MAX - TOUCH_RAW_MIN;
    int span_y = TOUCH_RAW_MAX - TOUCH_RAW_MIN;

    int tx = (int)(raw_x - TOUCH_RAW_MIN);
    int ty = (int)(raw_y - TOUCH_RAW_MIN);

    if (tx < 0) tx = 0;
    if (ty < 0) ty = 0;
    if (tx > span_x) tx = span_x;
    if (ty > span_y) ty = span_y;

    // Dependendo da orienta��ǜo do display, pode ser necessǭrio inverter X/Y
    *x = (uint16_t)((tx * (int)TFT_WIDTH) / span_x);
    *y = (uint16_t)((ty * (int)TFT_HEIGHT) / span_y);

    // Corrige espelhamento horizontal invertendo o eixo X
    *x = (uint16_t)(TFT_WIDTH - 1 - *x);

    return true;
}

// ===== Test functions =====

void test_simple(void)
{
    ESP_LOGI(TAG, "=== SIMPLE COLOR TEST ===");
    tft_fill_screen(TFT_RED);
    vTaskDelay(pdMS_TO_TICKS(1000));
    tft_fill_screen(TFT_GREEN);
    vTaskDelay(pdMS_TO_TICKS(1000));
    tft_fill_screen(TFT_BLUE);
    vTaskDelay(pdMS_TO_TICKS(1000));
    tft_fill_screen(TFT_WHITE);
    vTaskDelay(pdMS_TO_TICKS(1000));
    tft_fill_screen(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

void test_rectangles(void)
{
    ESP_LOGI(TAG, "=== RECTANGLE TEST ===");
    tft_fill_screen(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(500));

    tft_fill_rect(50, 50, 100, 100, TFT_RED);
    tft_fill_rect(200, 50, 100, 100, TFT_GREEN);
    tft_fill_rect(350, 50, 100, 100, TFT_BLUE);

    vTaskDelay(pdMS_TO_TICKS(2000));
}

void rpi_lcd_touch_test(void)
{
    ESP_LOGI(TAG, "=== TOUCH TEST ===");
    ESP_LOGI(TAG, "Toque na tela para ver pontos e coordenadas.");

    // Limpa a tela para o teste
    tft_fill_screen(TFT_BLACK);

    const TickType_t duration   = pdMS_TO_TICKS(15000); // 15 segundos de teste
    const TickType_t start_tick = xTaskGetTickCount();

    while ((xTaskGetTickCount() - start_tick) < duration) {
        uint16_t x, y;

        if (rpi_lcd_touch_read_pixel(&x, &y)) {
            ESP_LOGI(TAG, "Touch em x=%u, y=%u", x, y);

            // Desenha um pequeno quadrado vermelho no ponto tocado
            int px = (int)x;
            int py = (int)y;
            int size = 6;

            if (px < size / 2) px = size / 2;
            if (py < size / 2) py = size / 2;
            if (px > (int)TFT_WIDTH  - size / 2)  px = (int)TFT_WIDTH  - size / 2;
            if (py > (int)TFT_HEIGHT - size / 2)  py = (int)TFT_HEIGHT - size / 2;

            tft_fill_rect(px - size / 2, py - size / 2, size, size, TFT_RED);

            vTaskDelay(pdMS_TO_TICKS(80));
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    ESP_LOGI(TAG, "Touch test finalizado. Ajuste TOUCH_RAW_MIN/MAX se necessario.");
}
