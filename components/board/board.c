/*
 * SPDX-FileCopyrightText: 2024-2026 AIWearable Contributors
 * SPDX-License-Identifier: MIT
 *
 * Board initialization — Waveshare ESP32-S3 Audio Board
 */

#include "board.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "led_strip.h"
#include <math.h>
#include "esp_random.h"

/* Waveshare specific includes */
#include "esp_io_expander.h"
#include "esp_io_expander_tca95xx_16bit.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "es7210_adc.h"

/* Display and touch */
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_jd9853.h"
#include "touch_axs5106l.h"
#include "esp_lcd_touch.h"
#include "esp_lvgl_port.h"
#include "rom/ets_sys.h"

/* SD card via SDMMC */
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"

static const char *TAG = "board";

// ============================================================================
// Internal state
// ============================================================================
static led_strip_handle_t s_rgb_handle = NULL;
static i2s_chan_handle_t s_i2s_tx = NULL;
static i2s_chan_handle_t s_i2s_rx = NULL;
static i2c_master_bus_handle_t s_i2c0_bus = NULL;
static esp_io_expander_handle_t s_io_exp = NULL;
static esp_lcd_panel_handle_t s_lcd_panel = NULL;
static esp_lcd_panel_io_handle_t s_lcd_io = NULL;
static lv_disp_t *s_lvgl_disp = NULL;
static esp_codec_dev_handle_t s_play_dev = NULL;
static esp_codec_dev_handle_t s_record_dev = NULL;
static SemaphoreHandle_t s_codec_mutex = NULL;

static sdmmc_card_t *s_sdcard = NULL;
static bool s_sdcard_mounted = false;

// ============================================================================
// Board info
// ============================================================================

const char *board_get_name(void)
{
    return BOARD_NAME;
}

const char *board_get_mcu(void)
{
    return BOARD_MCU;
}

void *board_get_i2c0_handle(void)
{
    return (void *)s_i2c0_bus;
}

void board_tp_reset(bool active)
{
    if (s_io_exp) {
        esp_io_expander_set_level(s_io_exp, 1 << BOARD_IOEXP_TOUCH_RST, active ? 0 : 1);
    }
}

// ============================================================================
// I2C Bus Init
// ============================================================================

static esp_err_t board_i2c0_init(void)
{
    if (s_i2c0_bus != NULL) return ESP_OK;

    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_i2c0_bus), TAG, "I2C0 bus create failed");

    ESP_LOGI(TAG, "I2C0 initialized (SDA=%d, SCL=%d)", BOARD_I2C_SDA, BOARD_I2C_SCL);
    return ESP_OK;
}

// ============================================================================
// IO Expander (PCA9535 via TCA95xx driver)
// ============================================================================

static esp_err_t board_io_expander_init(void)
{
    if (s_io_exp != NULL) return ESP_OK;

    ESP_RETURN_ON_ERROR(
        esp_io_expander_new_i2c_tca95xx_16bit(s_i2c0_bus, BOARD_IO_EXP_ADDR, &s_io_exp),
        TAG, "IO expander init failed");

    // Outputs: LCD_RST(0), TOUCH_RST(1), CAM_EN(5), CAM_MUX(6), PA_EN(8)
    uint32_t output_mask = (1 << BOARD_IOEXP_LCD_RST) | (1 << BOARD_IOEXP_TOUCH_RST) |
                           (1 << BOARD_IOEXP_CAM_EN) | (1 << BOARD_IOEXP_CAM_MUX) |
                           (1 << BOARD_IOEXP_PA_EN);
    esp_io_expander_set_dir(s_io_exp, output_mask, IO_EXPANDER_OUTPUT);

    // Inputs: TOUCH_INT(2), BTN1(9), BTN2(10), BTN3(11)
    uint32_t input_mask = (1 << BOARD_IOEXP_TOUCH_INT) |
                          (1 << BOARD_IOEXP_BTN1) | (1 << BOARD_IOEXP_BTN2) | (1 << BOARD_IOEXP_BTN3);
    esp_io_expander_set_dir(s_io_exp, input_mask, IO_EXPANDER_INPUT);

    // Default all outputs low
    esp_io_expander_set_level(s_io_exp, output_mask, 0);

    ESP_LOGI(TAG, "IO expander initialized at 0x%02X", BOARD_IO_EXP_ADDR);
    return ESP_OK;
}

// ============================================================================
// RGB LED
// ============================================================================

esp_err_t board_rgb_init(void)
{
    if (s_rgb_handle != NULL) return ESP_OK;

    led_strip_config_t strip_config = {
        .strip_gpio_num = BOARD_RGB_GPIO,
        .max_leds = BOARD_RGB_LED_COUNT,
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };

    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_rgb_handle),
                        TAG, "RGB LED init failed");

    led_strip_set_pixel(s_rgb_handle, 0, 0, 0, 0);
    led_strip_refresh(s_rgb_handle);

    ESP_LOGI(TAG, "RGB LED initialized (GPIO%d)", BOARD_RGB_GPIO);
    return ESP_OK;
}

esp_err_t board_rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    if (s_rgb_handle == NULL) return ESP_ERR_INVALID_STATE;
    for (int i = 0; i < BOARD_RGB_LED_COUNT; i++) {
        ESP_RETURN_ON_ERROR(led_strip_set_pixel(s_rgb_handle, i, r, g, b), TAG, "RGB set failed");
    }
    return led_strip_refresh(s_rgb_handle);
}

esp_err_t board_rgb_set_single(uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if (s_rgb_handle == NULL) return ESP_ERR_INVALID_STATE;
    if (index >= BOARD_RGB_LED_COUNT) return ESP_ERR_INVALID_ARG;
    return led_strip_set_pixel(s_rgb_handle, index, r, g, b);
}

esp_err_t board_rgb_refresh(void)
{
    if (s_rgb_handle == NULL) return ESP_ERR_INVALID_STATE;
    return led_strip_refresh(s_rgb_handle);
}

/* ── RGB animation task ──────────────────────────────────────────────── */
static struct {
    board_rgb_mode_t mode;
    uint8_t r, g, b;
    bool running;
} s_rgb_anim = {0};

static const uint8_t s_gamma8[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255,
};

static inline void gamma_rgb(uint8_t *r, uint8_t *g, uint8_t *b)
{
    *r = s_gamma8[*r];
    *g = s_gamma8[*g];
    *b = s_gamma8[*b];
}

static void hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (s == 0) { *r = *g = *b = v; return; }
    uint8_t region = h / 60;
    uint16_t remainder = (h - (region * 60)) * 6;
    uint8_t p = (v * (255 - s)) >> 8;
    uint8_t q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    uint8_t t = (v * (255 - ((s * (255 - (remainder & 0xFF))) >> 8))) >> 8;
    switch (region) {
    case 0:  *r = v; *g = t; *b = p; break;
    case 1:  *r = q; *g = v; *b = p; break;
    case 2:  *r = p; *g = v; *b = t; break;
    case 3:  *r = p; *g = q; *b = v; break;
    case 4:  *r = t; *g = p; *b = v; break;
    default: *r = v; *g = p; *b = q; break;
    }
}

static void rgb_anim_task(void *arg)
{
    float phase = 0.0f;
    int frame = 0;
    s_rgb_anim.running = true;

    while (1) {
        board_rgb_mode_t mode = s_rgb_anim.mode;
        uint8_t r = s_rgb_anim.r, g = s_rgb_anim.g, b = s_rgb_anim.b;
        int nleds = BOARD_RGB_LED_COUNT;

        switch (mode) {
        case RGB_MODE_SOLID:
            board_rgb_set(r, g, b);
            vTaskDelay(pdMS_TO_TICKS(200));
            break;

        case RGB_MODE_BREATHE: {
            phase += 0.04f;
            if (phase > 3.14159f) phase -= 3.14159f;
            float bright = sinf(phase);
            bright = bright * bright;
            uint8_t br = (uint8_t)(r * bright);
            uint8_t bg = (uint8_t)(g * bright);
            uint8_t bb = (uint8_t)(b * bright);
            gamma_rgb(&br, &bg, &bb);
            board_rgb_set(br, bg, bb);
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        }

        case RGB_MODE_BLINK:
            board_rgb_set(r, g, b);
            vTaskDelay(pdMS_TO_TICKS(500));
            board_rgb_set(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case RGB_MODE_OFF:
            board_rgb_set(0, 0, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            break;

        case RGB_MODE_RAINBOW_SPIN: {
            const uint8_t bri = 50;
            float progress = fmodf(frame * 0.005f, 1.0f);
            uint16_t base_hue = (uint16_t)(progress * 360.0f);
            for (int i = 0; i < nleds; i++) {
                float led_off = (float)i / nleds;
                float motion  = progress * 2.0f;
                float hue_f   = base_hue + (led_off + motion) * 120.0f;
                uint16_t h    = ((int)hue_f) % 360;
                uint8_t  sat  = 200 + (uint8_t)(55.0f * sinf(progress * 3.14159f));
                uint8_t cr, cg, cb;
                hsv_to_rgb(h, sat, bri, &cr, &cg, &cb);
                led_strip_set_pixel(s_rgb_handle, i, cr, cg, cb);
            }
            led_strip_refresh(s_rgb_handle);
            frame++;
            vTaskDelay(pdMS_TO_TICKS(16));
            break;
        }

        case RGB_MODE_AURORA: {
            const uint8_t bri = 50;
            float t = frame * 0.008f;
            for (int i = 0; i < nleds; i++) {
                float pos = (float)i / nleds;
                float w1 = (1.0f + sinf(pos * 4.2f + t * 1.0f)) * 0.5f;
                float w2 = (1.0f + sinf(pos * 7.1f - t * 1.7f)) * 0.5f;
                float w3 = (1.0f + sinf(pos * 3.0f + t * 0.6f)) * 0.5f;
                uint8_t cr = (uint8_t)(bri * 0.25f * w3);
                uint8_t cg = (uint8_t)(bri * (0.3f + 0.7f * w1));
                uint8_t cb = (uint8_t)(bri * (0.2f + 0.6f * w2));
                led_strip_set_pixel(s_rgb_handle, i, cr, cg, cb);
            }
            led_strip_refresh(s_rgb_handle);
            frame++;
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        }

        case RGB_MODE_STARFIELD: {
            const uint8_t bri = 50;
            for (int i = 0; i < nleds; i++) {
                float freq = 0.015f + (i * 7 % 11) * 0.004f;
                float ph   = (i * 137 % 360) * 0.01745f;
                float lum  = (1.0f + sinf(frame * freq + ph)) * 0.5f;
                lum = 0.12f + 0.88f * lum * lum * lum;
                uint16_t hue = (i * 47 + frame / 80) % 60;
                if (i % 3 == 0) hue = 210 + (i * 13 % 30);
                uint8_t sat = (i % 3 == 0) ? 140 : 70;
                uint8_t cr, cg, cb;
                hsv_to_rgb(hue, sat, (uint8_t)(bri * lum), &cr, &cg, &cb);
                led_strip_set_pixel(s_rgb_handle, i, cr, cg, cb);
            }
            led_strip_refresh(s_rgb_handle);
            frame++;
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        }

        case RGB_MODE_FIRE: {
            const uint8_t bri = 50;
            for (int i = 0; i < nleds; i++) {
                float base  = (1.0f + sinf(frame * 0.04f + i * 2.1f)) * 0.25f;
                float flick = (1.0f + sinf(frame * 0.09f - i * 3.7f)) * 0.25f;
                uint32_t hash = (frame * 2654435761U + i * 40503U) >> 24;
                float noise = (float)hash / 255.0f * 0.25f;
                float inten = base + flick + noise;
                if (inten > 1.0f) inten = 1.0f;
                inten = 0.15f + 0.85f * inten * inten;
                uint16_t hue = (uint16_t)(inten * 35.0f);
                uint8_t  sat = 255 - (uint8_t)(inten * 30.0f);
                uint8_t cr, cg, cb;
                hsv_to_rgb(hue, sat, (uint8_t)(bri * inten), &cr, &cg, &cb);
                led_strip_set_pixel(s_rgb_handle, i, cr, cg, cb);
            }
            led_strip_refresh(s_rgb_handle);
            frame++;
            vTaskDelay(pdMS_TO_TICKS(25));
            break;
        }

        case RGB_MODE_OCEAN: {
            const uint8_t bri = 50;
            float t = frame * 0.012f;
            for (int i = 0; i < nleds; i++) {
                float pos  = (float)i / nleds * 6.283f;
                float deep = (1.0f + sinf(pos * 1.5f - t)) * 0.5f;
                float surf = (1.0f + sinf(pos * 3.0f - t * 2.3f)) * 0.5f;
                float comb = deep * 0.6f + surf * 0.4f;
                uint8_t cr = (uint8_t)(bri * 0.08f * comb);
                uint8_t cg = (uint8_t)(bri * (0.12f + 0.45f * comb));
                uint8_t cb = (uint8_t)(bri * (0.35f + 0.55f * comb));
                led_strip_set_pixel(s_rgb_handle, i, cr, cg, cb);
            }
            led_strip_refresh(s_rgb_handle);
            frame++;
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        }

        case RGB_MODE_CHASE: {
            float head_f = fmodf(frame * 0.3f, (float)nleds);
            for (int i = 0; i < nleds; i++) {
                float dist = fmodf(head_f - i + nleds, (float)nleds);
                float fade = 0.0f;
                if (dist < 1.0f) fade = 1.0f;
                else if (dist < 4.0f) fade = 1.0f / (dist * dist);
                uint8_t pr = (uint8_t)(r * fade);
                uint8_t pg = (uint8_t)(g * fade);
                uint8_t pb = (uint8_t)(b * fade);
                gamma_rgb(&pr, &pg, &pb);
                led_strip_set_pixel(s_rgb_handle, i, pr, pg, pb);
            }
            led_strip_refresh(s_rgb_handle);
            frame++;
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        }

        case RGB_MODE_PULSE_WAVE: {
            for (int i = 0; i < nleds; i++) {
                float pos = (float)i / nleds;
                float wave = (1.0f + sinf((pos * 6.283f) - phase)) * 0.5f;
                wave = wave * wave;
                uint8_t pr = (uint8_t)(r * wave);
                uint8_t pg = (uint8_t)(g * wave);
                uint8_t pb = (uint8_t)(b * wave);
                gamma_rgb(&pr, &pg, &pb);
                led_strip_set_pixel(s_rgb_handle, i, pr, pg, pb);
            }
            led_strip_refresh(s_rgb_handle);
            phase += 0.08f;
            if (phase > 6.283f) phase -= 6.283f;
            vTaskDelay(pdMS_TO_TICKS(20));
            break;
        }

        case RGB_MODE_SPARKLE: {
            for (int i = 0; i < nleds; i++) {
                if ((esp_random() & 0x07) == 0) {
                    led_strip_set_pixel(s_rgb_handle, i, r, g, b);
                } else {
                    led_strip_set_pixel(s_rgb_handle, i, r / 8, g / 8, b / 8);
                }
            }
            led_strip_refresh(s_rgb_handle);
            vTaskDelay(pdMS_TO_TICKS(60));
            break;
        }
        }
    }
}

esp_err_t board_rgb_animate(board_rgb_mode_t mode, uint8_t r, uint8_t g, uint8_t b)
{
    s_rgb_anim.mode = mode;
    s_rgb_anim.r = r;
    s_rgb_anim.g = g;
    s_rgb_anim.b = b;
    return ESP_OK;
}

esp_err_t board_rgb_task_start(void)
{
    if (s_rgb_anim.running) return ESP_OK;
    xTaskCreatePinnedToCore(rgb_anim_task, "rgb_anim", 4096, NULL, 1, NULL, 1);
    return ESP_OK;
}

// ============================================================================
// LCD Display (JD9853) + LVGL
// ============================================================================

esp_err_t board_display_init(void)
{
    // Backlight PWM
    const ledc_timer_config_t bl_timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_1,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&bl_timer), TAG, "LEDC timer failed");

    const ledc_channel_config_t bl_channel = {
        .gpio_num = BOARD_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_1,
        .timer_sel = LEDC_TIMER_1,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&bl_channel), TAG, "LEDC channel failed");

    // Reset LCD via IO expander
    if (s_io_exp) {
        esp_io_expander_set_level(s_io_exp, 1 << BOARD_IOEXP_LCD_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        esp_io_expander_set_level(s_io_exp, 1 << BOARD_IOEXP_LCD_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    // SPI bus
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = BOARD_LCD_SCLK,
        .mosi_io_num = BOARD_LCD_MOSI,
        .miso_io_num = BOARD_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOARD_LCD_H_RES * BOARD_LCD_V_RES * sizeof(uint16_t),
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "SPI init failed");

    // Panel IO
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = BOARD_LCD_DC,
        .cs_gpio_num = BOARD_LCD_CS,
        .pclk_hz = BOARD_LCD_PIXEL_CLK_HZ,
        .lcd_cmd_bits = BOARD_LCD_CMD_BITS,
        .lcd_param_bits = BOARD_LCD_PARAM_BITS,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST, &io_cfg, &s_lcd_io), TAG, "Panel IO failed");

    // JD9853 Panel
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1, // reset handled by IO expander
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BOARD_LCD_BPP,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_jd9853(s_lcd_io, &panel_cfg, &s_lcd_panel), TAG, "Panel init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_lcd_panel, 34, 0), TAG, "Panel set gap failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_lcd_panel, true), TAG, "LCD invert failed");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_lcd_panel), TAG, "LCD reset failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_lcd_panel), TAG, "LCD init failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_lcd_panel, false, false), TAG, "LCD mirror failed");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_lcd_panel, true), TAG, "LCD on failed");

    ESP_LOGI(TAG, "LCD initialized (%dx%d)", BOARD_LCD_H_RES, BOARD_LCD_V_RES);
    return ESP_OK;
}

esp_err_t board_display_set_brightness(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t duty = (1024 * percent) / 100;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, duty), TAG, "ledc_set_duty");
    ESP_RETURN_ON_ERROR(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1), TAG, "ledc_update_duty");
    return ESP_OK;
}

// ============================================================================
// Touch Panel (AXS5106L)
// ============================================================================

esp_err_t board_touch_init(void)
{
    touch_axs5106l_config_t touch_cfg = TOUCH_AXS5106L_CONFIG_DEFAULT();
    touch_cfg.x_max = BOARD_LCD_H_RES - 1;
    touch_cfg.y_max = BOARD_LCD_V_RES - 1;
    ESP_RETURN_ON_ERROR(touch_axs5106l_init(&touch_cfg), TAG, "Touch hardware init failed");
    ESP_RETURN_ON_ERROR(touch_axs5106l_register_lvgl(), TAG, "Touch LVGL registration failed");
    return ESP_OK;
}

// ============================================================================
// LVGL Setup
// ============================================================================

esp_err_t board_lvgl_init(void)
{
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port init failed");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_lcd_io,
        .panel_handle = s_lcd_panel,
        .buffer_size = BOARD_LCD_H_RES * 10 * sizeof(lv_color_t),
        .double_buffer = false,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
        },
    };
    s_lvgl_disp = lvgl_port_add_disp(&disp_cfg);
    if (s_lvgl_disp == NULL) {
        ESP_LOGE(TAG, "LVGL display add failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL initialized with display and touch");
    return ESP_OK;
}

lv_disp_t *board_get_lvgl_disp(void)
{
    return s_lvgl_disp;
}

// ============================================================================
// Audio (I2S + ES8311 + ES7210)
// ============================================================================

#define MIC_GAIN_DB 37.5f

esp_err_t board_audio_init(void)
{
    if (s_i2s_tx != NULL) return ESP_OK;

    s_codec_mutex = xSemaphoreCreateMutex();

    i2s_chan_config_t chan_cfg = {
        .id = BOARD_I2S_NUM,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = 8,
        .dma_frame_num = 512,
        .auto_clear = true,
        .intr_priority = 3,
    };
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx), TAG, "I2S channel create failed");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BOARD_AUDIO_SAMPLE_RATE),
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_MONO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK,
            .bclk = BOARD_I2S_SCLK,
            .ws   = BOARD_I2S_LRCK,
            .dout = BOARD_I2S_DOUT,
            .din  = BOARD_I2S_DSIN,
            .invert_flags = { false, false, false },
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_tx, &std_cfg), TAG, "I2S TX init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_tx), TAG, "I2S TX enable failed");

    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_i2s_rx, &std_cfg), TAG, "I2S RX init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_i2s_rx), TAG, "I2S RX enable failed");

    ESP_LOGI(TAG, "Audio I2S initialized");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = BOARD_I2S_NUM,
        .rx_handle = s_i2s_rx,
        .tx_handle = s_i2s_tx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data_if) {
        ESP_LOGE(TAG, "Failed to create I2S data interface");
        return ESP_FAIL;
    }

    // Speaker codec (ES8311)
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    audio_codec_i2c_cfg_t spk_i2c_cfg = {
        .port = BOARD_I2C_PORT,
        .addr = BOARD_ES8311_ADDR << 1,
        .bus_handle = s_i2c0_bus,
    };
    const audio_codec_ctrl_if_t *spk_ctrl = audio_codec_new_i2c_ctrl(&spk_i2c_cfg);
    if (!spk_ctrl) return ESP_FAIL;

    esp_codec_dev_hw_gain_t hw_gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 };
    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = spk_ctrl,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = GPIO_NUM_NC,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = hw_gain,
    };
    const audio_codec_if_t *es8311_dev = es8311_codec_new(&es8311_cfg);
    if (!es8311_dev) return ESP_FAIL;

    esp_codec_dev_cfg_t spk_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8311_dev,
        .data_if = data_if,
    };
    s_play_dev = esp_codec_dev_new(&spk_dev_cfg);
    if (!s_play_dev) return ESP_FAIL;
    ESP_LOGI(TAG, "ES8311 speaker codec initialized");

    // Microphone codec (ES7210)
    audio_codec_i2c_cfg_t mic_i2c_cfg = {
        .port = BOARD_I2C_PORT,
        .addr = BOARD_ES7210_ADDR << 1,
        .bus_handle = s_i2c0_bus,
    };
    const audio_codec_ctrl_if_t *mic_ctrl = audio_codec_new_i2c_ctrl(&mic_i2c_cfg);
    if (!mic_ctrl) {
        ESP_LOGW(TAG, "ES7210 I2C ctrl failed");
        return ESP_OK;
    }
    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = mic_ctrl,
        .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
    };
    const audio_codec_if_t *mic_codec = es7210_codec_new(&es7210_cfg);
    if (!mic_codec) {
        ESP_LOGW(TAG, "ES7210 codec create failed");
        return ESP_OK;
    }

    esp_codec_dev_cfg_t mic_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = mic_codec,
        .data_if = data_if,
    };
    s_record_dev = esp_codec_dev_new(&mic_dev_cfg);
    if (!s_record_dev) {
        ESP_LOGW(TAG, "Failed to create mic codec dev");
    }

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = BOARD_AUDIO_SAMPLE_RATE,
    };
    xSemaphoreTake(s_codec_mutex, portMAX_DELAY);
    if (s_play_dev) esp_codec_dev_open(s_play_dev, &fs);
    if (s_record_dev) {
        fs.channel = 2;
        fs.channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
        esp_codec_dev_open(s_record_dev, &fs);
        esp_codec_dev_set_in_gain(s_record_dev, MIC_GAIN_DB);
    }
    xSemaphoreGive(s_codec_mutex);

    ESP_LOGI(TAG, "Mic codec initialized (gain=%.0fdB)", MIC_GAIN_DB);
    return ESP_OK;
}

esp_err_t board_audio_record(int16_t *buffer, size_t samples, size_t *samples_read)
{
    if (s_record_dev == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_codec_mutex, portMAX_DELAY);
    int ret = esp_codec_dev_read(s_record_dev, buffer, samples * sizeof(int16_t));
    xSemaphoreGive(s_codec_mutex);
    if (samples_read) *samples_read = (ret == 0) ? samples : 0;
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t board_audio_play(const int16_t *buffer, size_t samples, size_t *samples_written)
{
    if (s_play_dev == NULL) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_codec_mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        if (samples_written) *samples_written = 0;
        return ESP_ERR_INVALID_STATE;
    }
    int ret = esp_codec_dev_write(s_play_dev, (void *)buffer, samples * sizeof(int16_t));
    xSemaphoreGive(s_codec_mutex);
    if (samples_written) *samples_written = (ret == 0) ? samples : 0;
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t board_audio_set_volume(int volume_percent)
{
    if (!s_play_dev) return ESP_ERR_INVALID_STATE;
    if (volume_percent < 0) volume_percent = 0;
    if (volume_percent > 100) volume_percent = 100;
    xSemaphoreTake(s_codec_mutex, portMAX_DELAY);
    int ret = esp_codec_dev_set_out_vol(s_play_dev, volume_percent);
    xSemaphoreGive(s_codec_mutex);
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

void board_play_tick(void)
{
    if (!s_play_dev) return;
    static const int16_t tick_pcm[] = {
        8000, 8000, 8000, 8000, -8000, -8000, -8000, -8000,
        6000, 6000, -6000, -6000, 4000, -4000, 2000, -2000,
        8000, 8000, 8000, 8000, -8000, -8000, -8000, -8000,
        6000, 6000, -6000, -6000, 4000, -4000, 2000, -2000,
    };
    size_t written = 0;
    board_audio_play(tick_pcm, sizeof(tick_pcm) / sizeof(tick_pcm[0]), &written);
}

esp_err_t board_audio_get_volume(int *volume_percent)
{
    if (!s_play_dev || !volume_percent) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_codec_mutex, portMAX_DELAY);
    int ret = esp_codec_dev_get_out_vol(s_play_dev, volume_percent);
    xSemaphoreGive(s_codec_mutex);
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t board_audio_mute(bool mute)
{
    if (!s_play_dev) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_codec_mutex, portMAX_DELAY);
    int ret = esp_codec_dev_set_out_mute(s_play_dev, mute);
    xSemaphoreGive(s_codec_mutex);
    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t board_audio_reconfig(uint32_t sample_rate, uint8_t channels)
{
    if (!s_play_dev || !s_i2s_tx || !s_i2s_rx) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_codec_mutex, portMAX_DELAY);
    esp_codec_dev_close(s_play_dev);
    i2s_channel_disable(s_i2s_tx);
    i2s_channel_disable(s_i2s_rx);

    i2s_std_clk_config_t clk_cfg = {
        .sample_rate_hz = sample_rate,
        .clk_src = (sample_rate == 48000) ? I2S_CLK_SRC_PLL_240M : I2S_CLK_SRC_DEFAULT,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
    };

    i2s_channel_reconfig_std_clock(s_i2s_rx, &clk_cfg);
    i2s_channel_reconfig_std_clock(s_i2s_tx, &clk_cfg);
    i2s_channel_enable(s_i2s_rx);
    i2s_channel_enable(s_i2s_tx);

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = sample_rate,
    };
    esp_codec_dev_open(s_play_dev, &fs);
    xSemaphoreGive(s_codec_mutex);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "Audio reconfigured to %lu Hz (internal heap: %lu, largest: %lu)",
             (unsigned long)sample_rate,
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    return ESP_OK;
}

esp_err_t board_audio_reopen_adc(void)
{
    if (!s_record_dev || !s_i2s_rx) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_codec_mutex, portMAX_DELAY);
    esp_codec_dev_close(s_record_dev);
    i2s_channel_disable(s_i2s_rx);
    i2s_channel_enable(s_i2s_rx);

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = BOARD_AUDIO_SAMPLE_RATE,
    };
    esp_codec_dev_open(s_record_dev, &fs);
    xSemaphoreGive(s_codec_mutex);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "ADC reopened successfully at %d Hz", BOARD_AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

// ============================================================================
// User buttons (BOOT + IO expander)
// ============================================================================

esp_err_t board_buttons_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOARD_BOOT_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Button GPIO config failed");
    ESP_LOGI(TAG, "User buttons initialized (BOOT=%d)", BOARD_BOOT_BUTTON);
    return ESP_OK;
}

bool board_boot_button_pressed(void)
{
    return gpio_get_level(BOARD_BOOT_BUTTON) == 0;
}

bool board_user_button_pressed(int btn_num)
{
    if (s_io_exp == NULL) return false;
    int pin;
    switch (btn_num) {
    case 1: pin = BOARD_IOEXP_BTN1; break;
    case 2: pin = BOARD_IOEXP_BTN2; break;
    case 3: pin = BOARD_IOEXP_BTN3; break;
    default: return false;
    }
    uint32_t level = 0;
    esp_err_t ret = esp_io_expander_get_level(s_io_exp, 1 << pin, &level);
    if (ret != ESP_OK) return false;  /* I2C read failed — treat as not pressed */
    return (level & (1 << pin)) == 0;
}

// ============================================================================
// SD Card (SDMMC 1-bit)
// ============================================================================

esp_err_t board_sdcard_init(void)
{
    if (s_sdcard_mounted) return ESP_OK;

    ESP_LOGI(TAG, "Mounting SD card (SDMMC 1-bit)...");

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 10,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = BOARD_SD_CLK;
    slot_config.cmd = BOARD_SD_CMD;
    slot_config.d0 = BOARD_SD_D0;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &s_sdcard);
    if (ret != ESP_OK) {
        board_sd_cs(false);
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_sdcard_mounted = true;
    sdmmc_card_print_info(stdout, s_sdcard);
    ESP_LOGI(TAG, "SD card mounted at /sdcard");
    return ESP_OK;
}

esp_err_t board_sdcard_deinit(void)
{
    if (!s_sdcard_mounted) return ESP_OK;
    esp_err_t ret = esp_vfs_fat_sdcard_unmount("/sdcard", s_sdcard);
    if (ret == ESP_OK) {
        s_sdcard_mounted = false;
        s_sdcard = NULL;
        ESP_LOGI(TAG, "SD card unmounted");
    }
    return ret;
}

bool board_sdcard_is_inserted(void)
{
    return s_sdcard_mounted;
}

// ============================================================================
// Battery (placeholder)
// ============================================================================

uint16_t board_battery_get_voltage_mv(void) { return 0; }
uint8_t board_battery_get_percent(void) { return 0; }

bool board_battery_is_charging(void)
{
    return false;
}

// ============================================================================
// IO expander / PA / SPI2
// ============================================================================

void *board_get_io_expander(void)
{
    return (void *)s_io_exp;
}

esp_err_t board_pa_enable(bool enable)
{
    if (s_io_exp == NULL) return ESP_ERR_INVALID_STATE;
    return esp_io_expander_set_level(s_io_exp, 1 << BOARD_IOEXP_PA_EN, enable ? 1 : 0);
}

esp_err_t board_sd_cs(bool enable)
{
    // SDMMC mode does not use CS pin
    (void)enable;
    return ESP_OK;
}

static bool s_spi2_initialized = false;

esp_err_t board_spi2_init(void)
{
    if (s_spi2_initialized) return ESP_OK;

    const spi_bus_config_t spi2_cfg = {
        .mosi_io_num = BOARD_SPI2_MOSI,
        .miso_io_num = BOARD_SPI2_MISO,
        .sclk_io_num = BOARD_SPI2_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 65536,
    };
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &spi2_cfg, SPI_DMA_CH_AUTO);
    if (ret == ESP_OK) {
        s_spi2_initialized = true;
        ESP_LOGI(TAG, "SPI2 bus initialized");
    }
    gpio_config_t sd_cs_cfg = {
        .pin_bit_mask = (1ULL << BOARD_SD_CS),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&sd_cs_cfg);
    gpio_set_level(BOARD_SD_CS, 1);
    return ret;
}

// ============================================================================
// Full board init
// ============================================================================

esp_err_t board_init(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " Board: %s (%s)", BOARD_NAME, BOARD_MCU);
    ESP_LOGI(TAG, "========================================");

    esp_err_t ret = board_i2c0_init();
    if (ret != ESP_OK) return ret;

    ret = board_io_expander_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "IO expander init failed: %s - continuing with limited functionality", esp_err_to_name(ret));
    }

    board_pa_enable(true);

    ret = board_rgb_init();
    if (ret != ESP_OK) ESP_LOGW(TAG, "RGB init failed: %s", esp_err_to_name(ret));

    ret = board_display_init();
    if (ret != ESP_OK) ESP_LOGW(TAG, "Display init failed: %s", esp_err_to_name(ret));

    ret = board_lvgl_init();
    if (ret != ESP_OK) ESP_LOGW(TAG, "LVGL init failed: %s", esp_err_to_name(ret));

    ret = board_touch_init();
    if (ret != ESP_OK) ESP_LOGW(TAG, "Touch init failed: %s", esp_err_to_name(ret));

    ret = board_audio_init();
    if (ret != ESP_OK) ESP_LOGW(TAG, "Audio init failed: %s", esp_err_to_name(ret));

    ret = board_buttons_init();
    if (ret != ESP_OK) ESP_LOGW(TAG, "Buttons init failed: %s", esp_err_to_name(ret));

    board_rgb_set(0, 0, 32);
    board_display_set_brightness(100);

    ESP_LOGI(TAG, "Board initialization complete");
    return ESP_OK;
}

void board_power_off(void)
{
    esp_restart();
}

void board_prepare_deep_sleep(void)
{
    for (int i = 0; i < 100 && gpio_get_level(GPIO_NUM_0) == 0; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
}

void board_reboot(void)
{
    esp_restart();
}