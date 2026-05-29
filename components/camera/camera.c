/*
 * SPDX-FileCopyrightText: 2024-2026 AIWearable Contributors
 * SPDX-License-Identifier: MIT
 *
 * Camera module — adapted from official ESP32-S3-CAM example:
 * /examples/ESP-IDF-v5.5.1/01_simple_video_server and 04_dvp_camera_display
 */

#include "camera.h"
#include "board.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_io_expander.h"
#include "esp_camera.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "camera";

#if !BOARD_HAS_CAMERA
esp_err_t camera_init(void) { ESP_LOGI(TAG, "No camera on this board"); return ESP_OK; }
esp_err_t camera_capture_jpeg(uint8_t **jpeg_out, size_t *jpeg_size) { (void)jpeg_out; (void)jpeg_size; return ESP_ERR_NOT_SUPPORTED; }
bool camera_is_ready(void) { return false; }
void camera_suspend(void) {}
esp_err_t camera_resume(void) { return ESP_OK; }
void camera_deinit(void) {}
void camera_lock(void) {}
void camera_unlock(void) {}
#else

static bool s_initialized = false;
static bool s_locked = false;
static SemaphoreHandle_t s_fb_mutex = NULL;

/* ── Pin map — matches official DVP camera example ── */
// SCCB shares I2C0 bus (GPIO10/11) with audio codecs, IO expander, RTC
// Set pins to -1 to tell esp_camera to reuse existing I2C port
#define CAM_PIN_PWDN   BOARD_CAM_PWDN
#define CAM_PIN_RESET  BOARD_CAM_RESET
#define CAM_PIN_XCLK   BOARD_CAM_XCLK
#define CAM_PIN_SIOD   -1  // -1 = reuse existing I2C port (I2C0)
#define CAM_PIN_SIOC   -1  // -1 = reuse existing I2C port (I2C0)
#define CAM_PIN_D7     BOARD_CAM_D7
#define CAM_PIN_D6     BOARD_CAM_D6
#define CAM_PIN_D5     BOARD_CAM_D5
#define CAM_PIN_D4     BOARD_CAM_D4
#define CAM_PIN_D3     BOARD_CAM_D3
#define CAM_PIN_D2     BOARD_CAM_D2
#define CAM_PIN_D1     BOARD_CAM_D1
#define CAM_PIN_D0     BOARD_CAM_D0
#define CAM_PIN_VSYNC  BOARD_CAM_VSYNC
#define CAM_PIN_HREF   BOARD_CAM_HREF
#define CAM_PIN_PCLK   BOARD_CAM_PCLK

/* ── IO expander helpers (match official ESP32-S3-Audio-Board pinout) ── */
// Official pinout: Extend 104 = CAM PWDN (P0.4), Extend 106 = Camera SEL (P0.6)
static void Camera_Power_On(void) {
    esp_io_expander_handle_t io = (esp_io_expander_handle_t)board_get_io_expander();
    if (io) {
        // Set camera control pins as output
        uint32_t cam_pins = (1 << BOARD_IOEXP_CAM_EN) | (1 << BOARD_IOEXP_CAM_MUX);
        esp_io_expander_set_dir(io, cam_pins, IO_EXPANDER_OUTPUT);
        
        // Power on sequence:
        // P0.4 (CAM PWDN) = 0 (LOW) → Power on camera
        // P0.6 (Camera SEL) = 1 (HIGH) → Select camera mode  
        esp_io_expander_set_level(io, (1 << BOARD_IOEXP_CAM_EN), 0);  // P0.4 LOW = camera power on
        vTaskDelay(pdMS_TO_TICKS(10));
        esp_io_expander_set_level(io, (1 << BOARD_IOEXP_CAM_MUX), 1);  // P0.6 HIGH = camera mode
        vTaskDelay(pdMS_TO_TICKS(100));  // Wait for camera to stabilize
        
        ESP_LOGI(TAG, "Camera power: P0.%d=0 (ON) P0.%d=1 (CAM mode)", 
                 BOARD_IOEXP_CAM_EN, BOARD_IOEXP_CAM_MUX);
    } else {
        ESP_LOGW(TAG, "IO expander not available, camera power control skipped");
    }
}

static void Camera_Power_Off(void) {
    esp_io_expander_handle_t io = (esp_io_expander_handle_t)board_get_io_expander();
    if (io) {
        // Power off: P0.4 (CAM PWDN) = 1 (HIGH) → Power down camera
        esp_io_expander_set_dir(io, (1 << BOARD_IOEXP_CAM_EN), IO_EXPANDER_OUTPUT);
        esp_io_expander_set_level(io, (1 << BOARD_IOEXP_CAM_EN), 1);  // P0.4 HIGH = power down
        ESP_LOGI(TAG, "Camera power: P0.%d=1 (OFF)", BOARD_IOEXP_CAM_EN);
    }
}

/* ── Camera config — matches official DVP example with OV2640 ── */
static camera_config_t camera_config = {
    .pin_pwdn     = CAM_PIN_PWDN,
    .pin_reset    = CAM_PIN_RESET,
    .pin_xclk     = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href  = CAM_PIN_HREF,
    .pin_pclk  = CAM_PIN_PCLK,

    .xclk_freq_hz = 20000000,                 // 20MHz XCLK (OV2640 max)
    .ledc_timer   = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG,          // JPEG for web streaming
    .frame_size   = FRAMESIZE_QVGA,          // 320x240
    .jpeg_quality = 22,                      // 0-63, lower=better; 22 balances size/quality for AI
    .fb_count     = 3,                       // Triple buffer (avoids NO-EOI on busy system)
    .fb_location  = CAMERA_FB_IN_PSRAM,      // PSRAM
    .grab_mode    = CAMERA_GRAB_LATEST,      // Always get the most recent frame
    .sccb_i2c_port = 0,                       // Reuse I2C0 bus
};

esp_err_t camera_init(void)
{
    if (s_initialized) return ESP_OK;

    if (s_locked) {
        ESP_LOGW(TAG, "Camera locked (MP3 active), refusing init");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_fb_mutex) {
        s_fb_mutex = xSemaphoreCreateMutex();
    }

    ESP_LOGI(TAG, "Initializing camera following official ESP32-S3-CAM example...");
    
    // Step 1: Power on camera via IO expander (match official bsp_io_expander_init)
    Camera_Power_On();
    
    // Step 2: Initialize camera using esp_camera_init API
    // The esp_camera driver will automatically create its own I2C port for SCCB
    // This is the official approach - let the camera driver manage SCCB
    ESP_LOGI(TAG, "DVP Configuration:");
    ESP_LOGI(TAG, "  XCLK=%d (%d Hz)", CAM_PIN_XCLK, BOARD_CAM_XCLK_FREQ_HZ);
    ESP_LOGI(TAG, "  PCLK=%d VSYNC=%d HREF=%d", CAM_PIN_PCLK, CAM_PIN_VSYNC, CAM_PIN_HREF);
    ESP_LOGI(TAG, "  Data pins: D0=%d D1=%d D2=%d D3=%d", CAM_PIN_D0, CAM_PIN_D1, CAM_PIN_D2, CAM_PIN_D3);
    ESP_LOGI(TAG, "  Data pins: D4=%d D5=%d D6=%d D7=%d", CAM_PIN_D4, CAM_PIN_D5, CAM_PIN_D6, CAM_PIN_D7);
    ESP_LOGI(TAG, "  SCCB: Reusing I2C0 bus (GPIO10/11, shared with audio/IO-expander/RTC)");
    ESP_LOGI(TAG, "  Frame buffer: PSRAM, %d buffers, %s quality=%d", 
             camera_config.fb_count,
             camera_config.frame_size == FRAMESIZE_VGA ? "VGA" : 
             (camera_config.frame_size == FRAMESIZE_QVGA ? "QVGA" : "QQVGA"),
             camera_config.jpeg_quality);
    ESP_LOGI(TAG, "  XCLK: %d Hz, DMA optimization enabled", camera_config.xclk_freq_hz);
    
    /* Memory debug: snapshot internal DRAM before camera GDMA allocation */
    uint32_t pre_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t pre_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "MEMDBG: Before init — internal free=%lu largest_block=%lu", (unsigned long)pre_internal, (unsigned long)pre_largest);

    ESP_LOGI(TAG, "Calling esp_camera_init()...");
    ESP_LOGI(TAG, "Waiting for XCLK to stabilize (100ms)...");
    vTaskDelay(pdMS_TO_TICKS(100));  // Allow XCLK to stabilize

    esp_err_t err = esp_camera_init(&camera_config);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: %s (0x%X)", esp_err_to_name(err), err);
        ESP_LOGE(TAG, "Troubleshooting:");
        ESP_LOGE(TAG, "  1. Check camera hardware connection and cable");
        ESP_LOGE(TAG, "  2. Verify XCLK signal on GPIO%d with oscilloscope", CAM_PIN_XCLK);
        ESP_LOGE(TAG, "  3. Check I2C0 bus conflicts (GPIO10/11 shared with audio/IO-expander)");
        ESP_LOGE(TAG, "  4. Ensure camera sensor is powered via IO expander");
        ESP_LOGE(TAG, "  5. Try reducing XCLK frequency or changing frame size");
        
        Camera_Power_Off();
        return err;
    }

    // Get sensor info (match official example)
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        ESP_LOGI(TAG, "Camera sensor detected:");
        ESP_LOGI(TAG, "  PID: 0x%02X", s->id.PID);
        ESP_LOGI(TAG, "  Manufacturer: %s", 
                 s->id.PID == OV2640_PID ? "OmniVision OV2640" :
                 s->id.PID == OV3660_PID ? "OmniVision OV3660" :
                 s->id.PID == OV5640_PID ? "OmniVision OV5640" : "Unknown");
        
        // Apply default settings (match official example)
        // Try different flip combinations to correct image orientation
        s->set_vflip(s, 0);  // No vertical flip
        s->set_hmirror(s, 0);  // No horizontal mirror
        ESP_LOGI(TAG, "Applied default settings: vflip=0, hmirror=0 (no flip)");
    } else {
        ESP_LOGW(TAG, "Warning: Could not get sensor info after successful init");
    }

    s_initialized = true;

    /* Enable PSRAM DMA on ESP32-S3: DMA writes directly to PSRAM frame buffers,
     * bypassing internal SRAM DMA buffer + memcpy. This reduces CPU overhead
     * and improves frame throughput. */
    esp_camera_set_psram_mode(true);
    ESP_LOGI(TAG, "PSRAM DMA mode enabled (direct PSRAM transfer)");

    /* Memory debug: measure GDMA descriptor overhead in internal DRAM */
    uint32_t post_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t post_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "MEMDBG: After init — internal free=%lu largest_block=%lu (delta: -%lu / -%lu)",
             (unsigned long)post_internal, (unsigned long)post_largest,
             (unsigned long)(pre_internal - post_internal), (unsigned long)(pre_largest - post_largest));

    ESP_LOGI(TAG, "Camera initialization successful!");
    return ESP_OK;
}

/* Validate JPEG: check for EOI marker (0xFFD9) at end of buffer */
static bool jpeg_is_valid(const uint8_t *buf, size_t len)
{
    if (len < 4) return false;
    /* SOI marker at start */
    if (buf[0] != 0xFF || buf[1] != 0xD8) return false;
    /* EOI marker at end */
    if (buf[len - 2] != 0xFF || buf[len - 1] != 0xD9) return false;
    return true;
}

esp_err_t camera_capture_jpeg(uint8_t **jpeg_out, size_t *jpeg_size)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!jpeg_out || !jpeg_size) return ESP_ERR_INVALID_ARG;

    /* Retry up to 3 times to work around NO-EOI / corrupt frames */
    for (int attempt = 0; attempt < 3; attempt++) {
        if (xSemaphoreTake(s_fb_mutex, pdMS_TO_TICKS(5000)) != pdTRUE)
            return ESP_ERR_TIMEOUT;

        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            xSemaphoreGive(s_fb_mutex);
            if (attempt < 2) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            return ESP_FAIL;
        }

        size_t sz = fb->len;
        bool valid = jpeg_is_valid(fb->buf, sz);

        if (!valid) {
            ESP_LOGW(TAG, "Invalid JPEG frame (attempt %d): len=%zu, first=%02X%02X last=%02X%02X",
                     attempt + 1, sz,
                     sz > 0 ? fb->buf[0] : 0, sz > 1 ? fb->buf[1] : 0,
                     sz > 1 ? fb->buf[sz - 2] : 0, sz > 1 ? fb->buf[sz - 1] : 0);
            esp_camera_fb_return(fb);
            xSemaphoreGive(s_fb_mutex);
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        uint8_t *buf = heap_caps_malloc(sz + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!buf) buf = malloc(sz + 1);
        if (!buf) {
            esp_camera_fb_return(fb);
            xSemaphoreGive(s_fb_mutex);
            return ESP_ERR_NO_MEM;
        }
        memcpy(buf, fb->buf, sz);
        esp_camera_fb_return(fb);
        xSemaphoreGive(s_fb_mutex);

        *jpeg_out = buf;
        *jpeg_size = sz;
        return ESP_OK;
    }

    return ESP_FAIL;
}

esp_err_t camera_capture_jpeg_direct(camera_fb_t **fb_out)
{
    if (!s_initialized) return ESP_ERR_INVALID_STATE;
    if (!fb_out) return ESP_ERR_INVALID_ARG;

    for (int attempt = 0; attempt < 3; attempt++) {
        if (xSemaphoreTake(s_fb_mutex, pdMS_TO_TICKS(5000)) != pdTRUE)
            return ESP_ERR_TIMEOUT;

        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            xSemaphoreGive(s_fb_mutex);
            if (attempt < 2) {
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
            return ESP_FAIL;
        }

        if (!jpeg_is_valid(fb->buf, fb->len)) {
            ESP_LOGW(TAG, "Invalid JPEG (attempt %d): len=%zu", attempt + 1, fb->len);
            esp_camera_fb_return(fb);
            xSemaphoreGive(s_fb_mutex);
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        /* Caller gets the fb directly — must call esp_camera_fb_return(fb) after use */
        xSemaphoreGive(s_fb_mutex);
        *fb_out = fb;
        ESP_LOGD(TAG, "Direct capture: %zu bytes", fb->len);
        return ESP_OK;
    }

    return ESP_FAIL;
}

bool camera_is_ready(void) { return s_initialized; }

void camera_suspend(void)
{
    if (!s_initialized) return;

    ESP_LOGI(TAG, "Suspending camera (power down, release GDMA)");

    /* Memory debug: snapshot before deinit */
    uint32_t pre_deinit_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t pre_deinit_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    esp_camera_deinit();
    s_initialized = false;
    Camera_Power_Off();
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Memory debug: verify GDMA memory was fully released */
    uint32_t post_deinit_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    uint32_t post_deinit_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "MEMDBG: After deinit — internal free=%lu largest_block=%lu (delta: +%lu / +%lu)",
             (unsigned long)post_deinit_internal, (unsigned long)post_deinit_largest,
             (unsigned long)(post_deinit_internal - pre_deinit_internal),
             (unsigned long)(post_deinit_largest - pre_deinit_largest));

    ESP_LOGI(TAG, "Camera suspended — GDMA channel 3 released");
}

esp_err_t camera_resume(void)
{
    if (s_initialized) return ESP_OK;

    ESP_LOGI(TAG, "Resuming camera...");
    esp_err_t err = camera_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Camera resumed successfully");
    }
    return err;
}

void camera_deinit(void)
{
    if (s_initialized) {
        esp_camera_deinit();
        s_initialized = false;
        ESP_LOGI(TAG, "Camera deinitialized");
    }

    // Power off camera
    Camera_Power_Off();

    if (s_fb_mutex) {
        vSemaphoreDelete(s_fb_mutex);
        s_fb_mutex = NULL;
    }
}

void camera_lock(void)
{
    s_locked = true;
    ESP_LOGI(TAG, "Camera locked (will refuse init)");
}

void camera_unlock(void)
{
    s_locked = false;
    ESP_LOGI(TAG, "Camera unlocked");
}

#endif /* BOARD_HAS_CAMERA */
