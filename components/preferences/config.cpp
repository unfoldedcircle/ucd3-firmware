// SPDX-FileCopyrightText: Copyright (c) 2022 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"

#include <string.h>

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "efuse.h"
#include "sdkconfig.h"
#include "uc_events.h"

// initializing config
Config::Config() {
    ESP_LOGD(m_ctx, "Creating Config");
    // hostname and serial number only need to be read once and not for every request
    char    dockHostName[22];
    uint8_t baseMac[6];
    esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
    snprintf(dockHostName, sizeof(dockHostName), "UCD3-%02X%02X%02X", baseMac[3], baseMac[4], baseMac[5]);

    m_hostname = dockHostName;

    // if no friendly name is set, use mac address
    if (getFriendlyName().empty()) {
        // get the default friendly name
        ESP_LOGD(m_ctx, "Setting default friendly name");
        setFriendlyName(m_hostname);
    }
}

// getter and setter for brightness value
int Config::getLedBrightness() {
    return getIntSetting(m_prefGeneral, "brightness", m_defaultLedBrightness);
}

void Config::setLedBrightness(int value) {
    if (value < 0 || value > 255) {
        ESP_LOGD(m_ctx, "Setting default brightness");
        value = m_defaultLedBrightness;
    }
    m_preferences.begin(m_prefGeneral, false);
    m_preferences.putInt("brightness", value);
    m_preferences.end();
}

int Config::getEthLedBrightness() {
    return getIntSetting(m_prefGeneral, "eth_brightness", m_defaultLedBrightness);
}

void Config::setEthLedBrightness(int value) {
    if (value < 0 || value > 255) {
        ESP_LOGD(m_ctx, "Setting default ETH LED brightness");
        value = m_defaultLedBrightness;
    }
    m_preferences.begin(m_prefGeneral, false);
    m_preferences.putInt("eth_brightness", value);
    m_preferences.end();
}

// getter and setter for dock friendly name
std::string Config::getFriendlyName() {
    std::string friendlyName = getStringSetting(m_prefGeneral, "friendly_name", "");

    // quick fix
    if (friendlyName == "null") {
        friendlyName.clear();
    }

    return friendlyName.empty() ? getHostName() : friendlyName;
}

void Config::setFriendlyName(std::string value) {
    // quick fix
    if (value == "null") {
        value.clear();
    }

    m_preferences.begin(m_prefGeneral, false);
    m_preferences.putString("friendly_name", value.substr(0, 40));
    m_preferences.end();
}

// getter and setter for wifi credentials
std::string Config::getWifiSsid() {
    return getStringSetting(m_prefWifi, "ssid", "");
}

std::string Config::getWifiPassword() {
    return getStringSetting(m_prefWifi, "password", "");
}

bool Config::setWifi(std::string ssid, std::string password) {
    if (ssid.length() > 32 || password.length() > 63) {
        return false;
    }
    if (!m_preferences.begin(m_prefWifi, false)) {
        return false;
    }
    m_preferences.putString("ssid", ssid);
    m_preferences.putString("password", password);
    m_preferences.end();

    return true;
}

bool Config::setLogLevel(UCLog::Level level) {
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }
    m_preferences.putUShort("log_level", level);

    m_preferences.end();
    return true;
}

bool Config::setSyslogServer(const std::string& server, uint16_t port) {
    if (server.length() > 64) {
        ESP_LOGW(m_ctx, "Ignoring syslog server: name too long");
        return false;
    }
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }
    m_preferences.putString("syslog_server", server);
    if (port == 0) {
        port = 514;
    }
    m_preferences.putUShort("syslog_port", port);

    m_preferences.end();
    return true;
}

bool Config::enableSyslog(bool enable) {
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }
    m_preferences.putBool("syslog_enabled", enable);

    m_preferences.end();
    return true;
}

bool Config::getTestMode() {
    return getBoolSetting(m_prefGeneral, "testmode", false);
}

bool Config::setTestMode(bool enable) {
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }
    m_preferences.putBool("testmode", enable);

    m_preferences.end();
    return true;
}

std::string Config::getToken() {
    return getStringSetting(m_prefGeneral, "token", m_defToken);
}

bool Config::setToken(std::string value) {
    if (value.length() > 64) {
        return false;
    }
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }
    m_preferences.putString("token", value);
    m_preferences.end();

    return true;
}

UCLog::Level Config::getLogLevel() {
    auto level = getUShortSetting(m_prefGeneral, "log_level", static_cast<uint16_t>(UCLog::Level::DEBUG));

    if (level <= 7) {
        return static_cast<UCLog::Level>(level);
    }

    return UCLog::Level::DEBUG;
}

std::string Config::getSyslogServer() {
    return getStringSetting(m_prefGeneral, "syslog_server", "");
}

uint16_t Config::getSyslogServerPort() {
    return getUShortSetting(m_prefGeneral, "syslog_port", 514);
}

bool Config::isSyslogEnabled() {
    return getBoolSetting(m_prefGeneral, "syslog_enabled", false);
}

const char* Config::getHostName() const {
    return m_hostname.c_str();
}

const char* Config::getSerial() const {
    return Efuse::instance().getSerial();
}

const char* Config::getModel() const {
    auto efuseModel = Efuse::instance().getModel();
    return strlen(efuseModel) ? efuseModel : UCD_HW_MODEL_NAME;
}

const char* Config::getRevision() const {
    auto efuseRev = Efuse::instance().getHwRevision();
    return strlen(efuseRev) ? efuseRev : UCD_HW_REVISION_NAME;
}

bool Config::hasChargingFeature() const {
    return Efuse::instance().hasChargingFeature();
}

std::string Config::getSoftwareVersion() {
    if (m_swVersion.rfind("v", 0) == 0) {
        return m_swVersion.substr(1);
    } else {
        return m_swVersion;
    }
}

bool Config::enableNtp(bool enable) {
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }
    m_preferences.putBool("ntp_enabled", enable);

    m_preferences.end();
    return true;
}

bool Config::isNtpEnabled() {
    return getBoolSetting(m_prefGeneral, "ntp_enabled", false);
}

bool Config::setNtpServer(const std::string& server1, const std::string& server2) {
    if (server1.length() > 32 || server2.length() > 32) {
        ESP_LOGW(m_ctx, "Ignoring ntp server: name too long");
        return false;
    }
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }
    m_preferences.putString("ntp_server1", server1);
    m_preferences.putString("ntp_server2", server2);

    m_preferences.end();
    return true;
}

std::string Config::getNtpServer1() {
    return getStringSetting(m_prefGeneral, "ntp_server1", "pool.ntp.org");
}

std::string Config::getNtpServer2() {
    return getStringSetting(m_prefGeneral, "ntp_server2", "");
}

uint8_t Config::getVolume() {
    return getUCharSetting(m_prefGeneral, "volume", 0);
}

void Config::setVolume(uint8_t volume) {
    m_preferences.begin(m_prefGeneral, false);
    m_preferences.putUChar("volume", volume);
    m_preferences.end();
}

ExtPortMode Config::getExternalPortMode(uint8_t port) {
    char keyname[8];
    snprintf(keyname, sizeof(keyname), "port%u", port);
    uint8_t mode = getUCharSetting(m_prefGeneral, keyname, 0);
    if (mode >= ExtPortMode::PORT_MODE_MAX) {
        return ExtPortMode::NOT_CONFIGURED;
    }

    return static_cast<ExtPortMode>(mode);
}

bool Config::setExternalPortMode(uint8_t port, ExtPortMode mode) {
    char keyname[8];
    snprintf(keyname, sizeof(keyname), "port%u", port);
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }
    m_preferences.putUChar(keyname, mode);

    m_preferences.end();
    return true;
}

std::string Config::getExternalPortUart(uint8_t port) {
    char keyname[14];
    snprintf(keyname, sizeof(keyname), "port%u_uart", port);
    return getStringSetting(m_prefGeneral, keyname, DEF_EXT_PORT_UART_CFG);
}

bool Config::setExternalPortUart(uint8_t port, const char* uart) {
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }

    char keyname[14];
    snprintf(keyname, sizeof(keyname), "port%u_uart", port);

    size_t len = m_preferences.putString(keyname, uart);
    m_preferences.end();
    return len > 0;
}

uint16_t Config::getIrSendCore() {
    return getUShortSetting(m_prefGeneral, "irsend_core", DEF_IRSEND_CORE);
}

bool Config::setIrSendCore(uint16_t core) {
    if (core > 1) {
        core = 1;
    }
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }
    m_preferences.putUShort("irsend_core", core);
    m_preferences.end();
    return true;
}

uint16_t Config::getIrSendPriority() {
    return getUShortSetting(m_prefGeneral, "irsend_prio", DEF_IRSEND_PRIO);
}

bool Config::setIrSendPriority(uint16_t priority) {
    if (priority >= configMAX_PRIORITIES) {
        priority = configMAX_PRIORITIES - 1;
    }
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }
    m_preferences.putUShort("irsend_prio", priority);
    m_preferences.end();
    return true;
}

uint16_t Config::getIrLearnCore() {
    return getUShortSetting(m_prefGeneral, "irlearn_core", DEF_IRLEARN_CORE);
}

bool Config::setIrLearnCore(uint16_t core) {
    if (core > 1) {
        core = 1;
    }
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }
    m_preferences.putUShort("irlearn_core", core);
    m_preferences.end();
    return true;
}

uint16_t Config::getIrLearnPriority() {
    return getUShortSetting(m_prefGeneral, "irlearn_prio", DEF_IRLEARN_PRIO);
}

bool Config::setIrLearnPriority(uint16_t priority) {
    if (priority >= configMAX_PRIORITIES) {
        priority = configMAX_PRIORITIES - 1;
    }
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }
    m_preferences.putUShort("irlearn_prio", priority);
    m_preferences.end();
    return true;
}

bool Config::enableGcServer(bool enable) {
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }
    m_preferences.putBool("gc_srv", enable);

    m_preferences.end();
    return true;
}

bool Config::isGcServerEnabled() {
    return getBoolSetting(m_prefGeneral, "gc_srv", false);
}

bool Config::enableGcServerBeacon(bool enable) {
    if (!m_preferences.begin(m_prefGeneral, false)) {
        return false;
    }
    m_preferences.putBool("gc_amxb", enable);

    m_preferences.end();
    return true;
}

bool Config::isGcServerBeaconEnabled() {
    return getBoolSetting(m_prefGeneral, "gc_amxb", false);
}

// reset config to defaults
void Config::reset() {
    ESP_LOGW(m_ctx, "Resetting configuration.");

    ESP_LOGD(m_ctx, "Resetting general.");
    m_preferences.begin(m_prefGeneral, false);
    m_preferences.clear();
    m_preferences.end();

    ESP_LOGD(m_ctx, "Resetting general done.");

    vTaskDelay(500 / portTICK_PERIOD_MS);

    ESP_LOGD(m_ctx, "Resetting wifi.");
    m_preferences.begin(m_prefWifi, false);
    m_preferences.clear();
    m_preferences.end();

    ESP_LOGD(m_ctx, "Resetting wifi done.");

    vTaskDelay(500 / portTICK_PERIOD_MS);

    ESP_LOGD(m_ctx, "Erasing flash.");
    int err;
    err = nvs_flash_init();
    ESP_LOGD(m_ctx, "nvs_flash_init: %d", err);
    err = nvs_flash_erase();
    ESP_LOGD(m_ctx, "nvs_flash_erase: %d", err);

    // FIXME: since this function is called from the same event loop, the new event won't be processed!
    // reset() would have to be independent from the system event loop...
    // --> see workaround with a timer in the display state machine
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_post(UC_DOCK_EVENTS, UC_EVENT_REBOOT, NULL, 0, pdMS_TO_TICKS(500)));

    vTaskDelay(500 / portTICK_PERIOD_MS);

    esp_restart();
}

std::string Config::getStringSetting(const char* partition, const char* key, const std::string defaultValue) {
    m_preferences.begin(partition, false);
    auto value = m_preferences.getString(key, defaultValue);
    m_preferences.end();

    return value;
}

bool Config::getBoolSetting(const char* partition, const char* key, bool defaultValue) {
    m_preferences.begin(partition, false);
    auto value = m_preferences.getBool(key, defaultValue);
    m_preferences.end();

    return value;
}

uint8_t Config::getUCharSetting(const char* partition, const char* key, uint8_t defaultValue) {
    m_preferences.begin(partition, false);
    auto value = m_preferences.getUChar(key, defaultValue);
    m_preferences.end();

    return value;
}

uint16_t Config::getUShortSetting(const char* partition, const char* key, uint16_t defaultValue) {
    m_preferences.begin(partition, false);
    auto value = m_preferences.getUShort(key, defaultValue);
    m_preferences.end();

    return value;
}

int Config::getIntSetting(const char* partition, const char* key, int defaultValue) {
    m_preferences.begin(partition, false);
    auto value = m_preferences.getInt(key, defaultValue);
    m_preferences.end();

    return value;
}

Config& Config::instance() {
    // https://laristra.github.io/flecsi/src/developer-guide/patterns/meyers_singleton.html
    static Config instance;
    return instance;
}

const char* getResetReason() {
    switch (esp_reset_reason()) {
        case ESP_RST_UNKNOWN:
            return "Reset reason can not be determined";
        case ESP_RST_POWERON:
            return "Reset due to power-on event";
        case ESP_RST_EXT:
            return "Reset by external pin (not applicable for ESP32)";
        case ESP_RST_SW:
            return "Software reset via esp_restart";
        case ESP_RST_PANIC:
            return "Software reset due to exception/panic";
        case ESP_RST_INT_WDT:
            return "Reset (software or hardware) due to interrupt watchdog";
        case ESP_RST_TASK_WDT:
            return "Reset due to task watchdog";
        case ESP_RST_WDT:
            return "Reset due to other watchdogs";
        case ESP_RST_DEEPSLEEP:
            return "Reset after exiting deep sleep mode";
        case ESP_RST_BROWNOUT:
            return "Brownout reset (software or hardware)";
        case ESP_RST_SDIO:
            return "Reset over SDIO";
        case ESP_RST_USB:
            return "Reset by USB peripheral";
        case ESP_RST_JTAG:
            return "Reset by JTAG";
        case ESP_RST_EFUSE:
            return "Reset due to efuse error";
        case ESP_RST_PWR_GLITCH:
            return "Reset due to power glitch detected";
        case ESP_RST_CPU_LOCKUP:
            return "Reset due to CPU lock up";
        default:
            return "Unknown reset reason";
    }
}
