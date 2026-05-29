/*
 * SPDX-FileCopyrightText: 2024-2026 AIWearable Contributors
 * SPDX-License-Identifier: MIT
 *
 * Camera abstraction — DVP interface (OV2640/OV3660/OV5640)
 * Adapted from official ESP32-S3-CAM examples:
 * - /examples/ESP-IDF-v5.5.1/01_simple_video_server
 * - /examples/ESP-IDF-v5.5.1/04_dvp_camera_display
 */

#pragma once

#include "esp_err.h"
#include "esp_camera.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize camera (DVP interface via esp_camera API)
 * Powers on camera via IO expander, then initializes using official esp_camera_init().
 * Must be called after board_init() since it needs the IO expander.
 * @return ESP_OK on success
 */
esp_err_t camera_init(void);

/**
 * @brief Capture a single JPEG image (with memcpy to caller-owned buffer)
 * Allocates JPEG buffer in PSRAM. Caller must free with free().
 *
 * @param[out] jpeg_out    Pointer to receive JPEG data (PSRAM-allocated)
 * @param[out] jpeg_size   Size of JPEG data in bytes
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no image, ESP_FAIL on error
 */
esp_err_t camera_capture_jpeg(uint8_t **jpeg_out, size_t *jpeg_size);

/**
 * @brief Capture a JPEG frame directly (zero-copy, no memcpy)
 *
 * Returns the camera frame buffer directly. Caller MUST call
 * esp_camera_fb_return(fb) after consuming the data.
 * Faster than camera_capture_jpeg() for send-and-discard use cases.
 *
 * @param[out] fb_out  Pointer to receive camera_fb_t* (valid until fb_return)
 * @return ESP_OK on success
 */
esp_err_t camera_capture_jpeg_direct(camera_fb_t **fb_out);

/**
 * @brief Check if camera is initialized and ready
 * @return true if ready
 */
bool camera_is_ready(void);

/**
 * @brief Suspend camera (power down, stop GDMA/XCLK)
 *
 * Releases GDMA channel 3 and stops the DVP clock to prevent
 * interference with TLS/AES operations during voice recording.
 * Call camera_resume() or camera_init() to re-enable.
 */
void camera_suspend(void);

/**
 * @brief Resume camera after suspend (re-initializes)
 * @return ESP_OK on success
 */
esp_err_t camera_resume(void);

/**
 * @brief Deinitialize camera (power down)
 */
void camera_deinit(void);

/**
 * @brief Lock camera — prevent any re-initialization (e.g. during MP3 playback)
 *
 * While locked, camera_init() returns ESP_ERR_INVALID_STATE.
 * Use camera_unlock() to re-enable.
 */
void camera_lock(void);

/**
 * @brief Unlock camera — allow initialization again
 */
void camera_unlock(void);

#ifdef __cplusplus
}
#endif
