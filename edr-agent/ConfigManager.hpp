#pragma once

#include <string>
#include <filesystem>
#include "nlohmann/json.hpp"
#include "ConfigReader.hpp"

/**
 * @class ConfigManager
 * @brief Manages dynamic agent configuration updates.
 * 
 * Responsibilities:
 * - Compare local config version with server version
 * - Download new configuration from server
 * - Validate configuration values
 * - Atomically save configuration to disk
 * - Apply changes to running agent components
 */
class ConfigManager {
public:
    /**
     * @brief Initialize the ConfigManager.
     * Reads the current config version from disk.
     */
    static void init();

    /**
     * @brief Check if server has a newer config version and update if needed.
     * Called periodically after heartbeat response.
     * 
     * @param serverVersion The config version reported by the server.
     * @return true if config was updated, false otherwise.
     */
    static bool checkAndUpdate(int serverVersion);

    /**
     * @brief Get the current heartbeat interval in seconds.
     * @return Interval in seconds (default: 30)
     */
    static int getHeartbeatInterval();

    /**
     * @brief Get the current telemetry batch size.
     * @return Batch size (default: 100)
     */
    static int getBatchSize();

    /**
     * @brief Check if HTTP polling is enabled.
     * @return true if enabled, false otherwise.
     */
    static bool isHttpPollingEnabled();

    /**
     * @brief Get the HTTP command polling interval.
     * @return Interval in seconds (default: 5)
     */
    static int getHttpPollInterval();

    /**
     * @brief Check if a specific module is enabled.
     * @param moduleName Name of the module (e.g., "process_monitor")
     * @return true if enabled
     */
    static bool isModuleEnabled(const std::string& moduleName);
    static bool isEventEnabled(int eventId);

    /**
     * @brief Get the currently loaded configuration version.
     */
    static int getCurrentVersion();

private:
    static int s_currentVersion;
    static nlohmann::json s_config;
    static std::string s_configPath;

    /**
     * @brief Download new config from server, validate, and save.
     * @return true if successful
     */
    static bool downloadAndApply();

    /**
     * @brief Validate configuration values against constraints.
     * @param config The JSON config object to validate.
     * @return true if valid
     */
    static bool validateConfig(const nlohmann::json& config);

    /**
     * @brief Atomically save configuration to disk using "read-modify-write".
     * Preserves critical bootstrap fields (server_url, enrollment_token).
     * @param newConfig The new configuration to save.
     * @return true if successful
     */
    static bool saveConfig(const nlohmann::json& newConfig);

    /**
     * @brief Apply configuration changes to the running agent.
     * Updates global variables and signals threads.
     * @param newConfig The new configuration.
     */
    static void applyChanges(const nlohmann::json& newConfig);

    /**
     * @brief Restore previous configuration from backup.
     */
    static void rollback();

    /**
     * @brief Reload Sysmon with the new configuration file.
     * @param configPath Path to the generated Sysmon XML.
     */
    static void reloadSysmon(const std::string& configPath);
    
    // Helper to get nested value safely
    template<typename T>
    static T getJsonValue(const nlohmann::json& j, const std::string& key, T defaultValue);
};
