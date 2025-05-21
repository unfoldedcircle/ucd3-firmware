// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ota.h"

#include <sys/param.h>  // For MIN/MAX(a, b)

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_tls_crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "WebServer.h"
#include "config.h"
#include "mem_util.h"
#include "sdkconfig.h"
#include "uc_events.h"

static const char *const TAG = "OTA";

/**
 * Enhanced ESP_GOTO_ON_ERROR macro to set an additional msg variable with the log message for further use.
 *
 * Macro which can be used to check the error code. If the code is not ESP_OK, it prints the message,
 * sets the local variable 'ret' to the code, `msg` to the log message and then exits by jumping to 'goto_tag'.
 * Message log parameters are not supported.
 */
#define ESP_GOTO_ON_ERROR_MSG(x, goto_tag, log_tag, log_msg)               \
    do {                                                                   \
        esp_err_t err_rc_ = (x);                                           \
        if (unlikely(err_rc_ != ESP_OK)) {                                 \
            ESP_LOGE(log_tag, "%s(%d): " log_msg, __FUNCTION__, __LINE__); \
            ret = err_rc_;                                                 \
            msg = log_msg;                                                 \
            goto goto_tag;                                                 \
        }                                                                  \
    } while (0)

/**
 * Enhanced ESP_GOTO_ON_FALSE macro to set an additional msg variable.
 *
 * Macro which can be used to check the condition. If the condition is not 'true', it prints the message,
 * sets the local variable 'ret' to the supplied 'err_code', `msg` to the log message and then exits by jumping to
 * 'goto_tag'.
 */
#define ESP_GOTO_ON_FALSE_MSG(a, err_code, goto_tag, log_tag, log_msg)     \
    do {                                                                   \
        if (unlikely(!(a))) {                                              \
            ESP_LOGE(log_tag, "%s(%d): " log_msg, __FUNCTION__, __LINE__); \
            ret = err_code;                                                \
            msg = log_msg;                                                 \
            goto goto_tag;                                                 \
        }                                                                  \
    } while (0)

/**
 * Create a base64 encoded basic auth value string.
 * @link https://github.com/espressif/esp-idf/issues/5646
 */
static char *http_auth_basic(const char *username, const char *password) {
    int    out;
    char  *user_info = NULL;
    char  *digest = NULL;
    size_t n = 0;
    asprintf(&user_info, "%s:%s", username, password);
    esp_crypto_base64_encode(NULL, 0, &n, (const unsigned char *)user_info, strlen(user_info));
    digest = static_cast<char *>(calloc(1, 6 + n + 1));
    strcpy(digest, "Basic ");
    esp_crypto_base64_encode((unsigned char *)digest + 6, n, (size_t *)&out, (const unsigned char *)user_info,
                             strlen(user_info));
    free(user_info);
    return digest;
}

/**
 * Perform basic authentication check on the given request.
 * @return ESP_OK if authenticed, ESP_FAIL otherwise.
 */
esp_err_t check_auth(httpd_req_t *req) {
    esp_err_t   ret = ESP_FAIL;
    char       *buf = NULL;
    char       *auth_credentials = NULL;
    const char *user = "admin";
    std::string password = Config::instance().getToken();

    size_t buf_len = httpd_req_get_hdr_value_len(req, "Authorization") + 1;

    ESP_GOTO_ON_FALSE(buf_len > 0, ESP_FAIL, err, TAG, "No Authorization header received");

    buf = static_cast<char *>(calloc(1, buf_len));
    if (httpd_req_get_hdr_value_str(req, "Authorization", buf, buf_len)) {
        ESP_LOGE(TAG, "No auth value received");
    }

    auth_credentials = http_auth_basic(user, password.c_str());
    ESP_GOTO_ON_FALSE(strncmp(auth_credentials, buf, buf_len) == 0, ESP_FAIL, err, TAG, "Not authenticated");
    ESP_LOGD(TAG, "Authenticated!");

    free(auth_credentials);
    free(buf);

    return ESP_OK;
err:
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Dock\"");
    httpd_resp_send_json_err(req, HTTPD_401_UNAUTHORIZED, "Not authorized");
    return ret;
}

esp_err_t on_ota_upload(httpd_req_t *req) {
    /// @brief Upload progress reporting chunk size
    static const size_t chunkSize = 102400;
    // Store the next milestone to output
    size_t next = chunkSize;

    esp_err_t              ret = ESP_OK;
    const char            *msg = "";
    httpd_err_code_t       response_code = HTTPD_500_INTERNAL_SERVER_ERROR;
    esp_ota_handle_t       ota_handle = 0;
    const esp_partition_t *ota_partition = NULL;
    size_t                 remaining = req->content_len;
    uint8_t                retry = 0;
    uint8_t                percent = 0;

    if (check_auth(req)) {
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_OTA_START, NULL, 0, pdMS_TO_TICKS(200)));

    char *buf = (char *)malloc_init_external(CONFIG_UCD_OTA_PSRAM_BUFSIZE);
    ESP_GOTO_ON_FALSE_MSG(buf != NULL, ESP_FAIL, err, TAG, "Not enough memory");

    ota_partition = esp_ota_get_next_update_partition(NULL);
    ESP_GOTO_ON_FALSE_MSG(ota_partition != NULL, ESP_FAIL, err, TAG, "No OTA partition found");

    response_code = HTTPD_400_BAD_REQUEST;
    ESP_GOTO_ON_FALSE_MSG(req->content_len <= ota_partition->size, ESP_FAIL, err, TAG, "Firmware file too big");
    response_code = HTTPD_500_INTERNAL_SERVER_ERROR;

    ESP_LOGI(TAG, "Starting firmware update");
    ESP_GOTO_ON_ERROR_MSG(esp_ota_begin(ota_partition, req->content_len, &ota_handle), err, TAG, "Failed to start OTA");

    while (remaining > 0) {
        int recv_len = httpd_req_recv(req, buf, MIN(remaining, CONFIG_UCD_OTA_PSRAM_BUFSIZE));

        // Timeout, abort after 3 consecutive timeouts
        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
            retry++;
            ESP_GOTO_ON_FALSE_MSG(retry < 3, ESP_FAIL, err, TAG, "Read timeout");
            continue;
        }
        retry = 0;

        // Abort: unrecoverable error
        ESP_GOTO_ON_FALSE_MSG(recv_len > 0, ESP_FAIL, err, TAG, "Socket read error");

        remaining -= recv_len;

        // Check if we need to output a milestone (100k 200k 300k)
        if (req->content_len - remaining >= next) {
            ESP_LOGI(TAG, "Flashing firmware update: %u/%u KB", next / 1024, req->content_len / 1024);
            next += chunkSize;
        }

        ESP_GOTO_ON_ERROR_MSG(esp_ota_write(ota_handle, (const void *)buf, recv_len), err, TAG, "OTA write error");

        // Since this loop is blocking lower priority tasks, suspend for a short time in each iteration
        // for the scheduler to be able to still run them.
        // Flashing over ethernet is around 100 kB/s, i.e. this loop is running for around 20s.
        // With a 2MB firmware and a 4kB read buffer, this loop is running for around 20s and called 500 times.
        // A 10ms delay will only add a few seconds. The OTA update doesn't need to be as fast as possible.
        //
        // TODO a proper solution would split the upload into smaller tasks and would not block other webserver sockets!
        if (CONFIG_UCD_OTA_UPLOAD_DELAY) {
            vTaskDelay(pdMS_TO_TICKS(CONFIG_UCD_OTA_UPLOAD_DELAY));
        }

        uint8_t current_percent = (req->content_len - remaining) * 100 / req->content_len;
        if (current_percent > percent) {
            percent = current_percent;
            uc_event_ota_progress_t event_ota = {.percent = percent};
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_OTA_PROGRESS, &event_ota,
                                                         sizeof(event_ota), pdMS_TO_TICKS(200)));
        }
    }

    ESP_LOGI(TAG, "Firmware update written, starting validation");

    // Validate and switch to new OTA image
    ESP_GOTO_ON_ERROR_MSG(esp_ota_end(ota_handle), err, TAG, "OTA validation error");
    ESP_GOTO_ON_ERROR_MSG(esp_ota_set_boot_partition(ota_partition), err, TAG, "Error setting boot partition");

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_OTA_SUCCESS, NULL, 0, pdMS_TO_TICKS(200)));

    httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    httpd_resp_sendstr(req, "{\"code\": 200, \"msg\":\"Firmware update complete, rebooting now!\"}");

    // Time to reboot into updated partition
    ESP_LOGI(TAG, "Firmware update successful, rebooting");
    vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_REBOOT, NULL, 0, pdMS_TO_TICKS(200)));
    vTaskDelay(pdMS_TO_TICKS(200));

    esp_restart();

    return ESP_OK;
err:
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_OTA_FAIL, NULL, 0, pdMS_TO_TICKS(200)));

    FREE_AND_NULL(buf);
    esp_ota_abort(ota_handle);
    httpd_resp_send_json_err(req, response_code, msg);
    return ret;
}
