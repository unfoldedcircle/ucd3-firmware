// SPDX-FileCopyrightText: Copyright (c) 2022 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <nvs.h>
#include <nvs_flash.h>

#include <memory>
#include <string>

#include "driver/uart.h"

#include "ext_port_mode.h"
#include "net_config.h"
#include "preferences.h"
#include "uart_config.h"

#ifdef __cplusplus
extern "C" {
#endif

const char* getResetReason();

#ifdef __cplusplus
}
#endif

class UCLog {
 public:
    // Mapped to Syslog LOG_EMERG .. LOG_DEBUG
    enum Level {
        /* system is unusable */
        EMERG = 0,
        /* action must be taken immediately */
        ALERT = 1,
        /* critical conditions */
        CRIT = 2,
        /* error conditions */
        ERROR = 3,
        /* warning conditions */
        WARN = 4,
        /* normal but significant condition */
        NOTICE = 5,
        /* informational */
        INFO = 6,
        /* debug-level messages */
        DEBUG = 7
    };
};

class Config {
 public:
    static Config& instance();

    /**
     * Returns the LED brightness value. Range is 5..255.
     */
    int getLedBrightness();
    /**
     * Set LED brightness value between 5..255. Values outside that range will set the default value.
     */
    void setLedBrightness(int value);

    int  getEthLedBrightness();
    void setEthLedBrightness(int value);

    // getter and setter for dock friendly name
    std::string getFriendlyName();
    /**
     * Maximum length is 40 characters. Longer names will be cut.
     */
    void setFriendlyName(std::string value);

    // getter and setter for wifi settings
    std::string getWifiSsid();
    std::string getWifiPassword();

    /**
     * Sets the WiFi SSID and password. Returns false if ssid > 32 or password > 63.
     *
     * Returns true if the values were stored.
     */
    bool setWifi(std::string ssid, std::string password);

    std::string getToken();
    /**
     * Sets the WebSocket connection token. Maximum length is 64 characters. Longer tokens are ignored.
     *
     * Returns true if the token was stored.
     */
    bool setToken(std::string value);

    bool setLogLevel(UCLog::Level level);
    bool setSyslogServer(const std::string& server, uint16_t port = 514);
    bool enableSyslog(bool enable);

    bool getTestMode();
    bool setTestMode(bool enable);

    UCLog::Level getLogLevel();
    std::string  getSyslogServer();
    uint16_t     getSyslogServerPort();
    bool         isSyslogEnabled();

    // get hostname
    const char* getHostName() const;

    // get serial from device
    const char* getSerial() const;
    // get model number from device
    const char* getModel() const;
    // get device hardware revision
    const char* getRevision() const;
    // check if it is a charging dock
    bool hasChargingFeature() const;

    // get software version
    std::string getSoftwareVersion();

    // SNTP server
    bool        enableNtp(bool enable);
    bool        isNtpEnabled();
    bool        setNtpServer(const std::string& server1, const std::string& server2);
    std::string getNtpServer1();
    std::string getNtpServer2();

    // -- Static network configuration

    /// @brief Set static network
    /// @param cfg
    /// @return true if configuration could be stored
    bool          setNetwork(network_cfg_t cfg);
    network_cfg_t getNetwork();

    /// @brief Set static DNS server addresses
    /// @param server1 main DNS. Max length is 32 characters.
    /// @param server2 backup DNS. Max length is 32 characters.
    /// @return true if server addresses could be stored
    bool setDnsServer(const std::string& server1, const std::string& server2);
    /// @brief Get main DNS server address.
    std::string getDnsServer1();
    /// @brief Get backupt DNS server address.
    std::string getDnsServer2();

    // -- Sound settings

    /// @brief Get speaker volume.
    uint8_t getVolume();
    void    setVolume(uint8_t volume);

    // -- External port configuration

    /// @brief Get mode of external port.
    /// @param port port number, 1-based.
    ExtPortMode getExternalPortMode(uint8_t port);

    /// @brief Set mode of external port.
    /// @param port port number, 1-based.
    /// @param mode port configuration mode.
    /// @return true if configuration could be saved, false otherwise.
    bool setExternalPortMode(uint8_t port, ExtPortMode mode);

    /// @brief Get UART configuration of an external port.
    /// @param port port number, 1-based.
    /// @return UART setting in format "$BAUDRATE:$DATA_BITS$PARITY$STOP_BITS".
    std::string getExternalPortUart(uint8_t port);

    /// @brief Set UART configuration of an external port.
    /// @param port port number, 1-based.
    /// @param uart UART settings in format "$BAUDRATE:$DATA_BITS$PARITY$STOP_BITS".
    /// @return true if configuration could be saved, false otherwise.
    bool setExternalPortUart(uint8_t port, const char* uart);

    // IR sending configuration
    uint16_t getIrSendCore();
    bool     setIrSendCore(uint16_t core);
    uint16_t getIrSendPriority();
    bool     setIrSendPriority(uint16_t priority);
    uint16_t getIrLearnCore();
    bool     setIrLearnCore(uint16_t core);
    uint16_t getIrLearnPriority();
    bool     setIrLearnPriority(uint16_t priority);

    // GC iTach device emulation
    bool enableGcServer(bool enable);
    bool isGcServerEnabled();
    bool enableGcServerBeacon(bool enable);
    bool isGcServerBeaconEnabled();

    // reset config to defaults
    void reset();

 private:
    Config();
    ~Config() {}

    Config(const Config&) = delete;  // no copying
    Config& operator=(const Config&) = delete;

    std::string getStringSetting(const char* partition, const char* key,
                                 const std::string defaultValue = std::string());
    bool        getBoolSetting(const char* partition, const char* key, bool defaultValue = false);
    uint8_t     getUCharSetting(const char* partition, const char* key, uint8_t defaultValue = 0);
    uint16_t    getUShortSetting(const char* partition, const char* key, uint16_t defaultValue = 0);
    int         getIntSetting(const char* partition, const char* key, int defaultValue = 0);
    uint32_t    getUIntSetting(const char* partition, const char* key, uint32_t defaultValue = 0);

    Preferences m_preferences;
    int         m_defaultLedBrightness = 50;
    std::string m_hostname;
    std::string m_swVersion = DOCK_VERSION;

    const char* m_prefGeneral = "general";
    const char* m_prefWifi = "wifi";
    const char* m_defToken = "0000";
    const char* m_ctx = "CFG";

    static const uint16_t DEF_IRSEND_CORE = 1;   // doesn't work well on core 0, only with a very high priority
    static const uint16_t DEF_IRSEND_PRIO = 18;  // 12 works well on Dock Two, Dock 3 requires 18. Max priority: 24
    static const uint16_t DEF_IRLEARN_CORE = 1;  // never tested on core 0, seems to work well on 1
    static const uint16_t DEF_IRLEARN_PRIO = 5;
};
