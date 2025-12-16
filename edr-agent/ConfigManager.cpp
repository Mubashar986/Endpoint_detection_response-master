#include "ConfigManager.hpp"
#include "HttpClient.hpp"
#include "Logger.hpp"
#include <fstream>
#include <filesystem>
#include <windows.h> // For MoveFileEx

// External references to global variables (from EdrAgent.cpp/Heartbeat.cpp)
extern HttpClient* g_httpClient;
extern std::string g_agentId;

// Dynamic config globals (to be added to EdrAgent.cpp)
extern int g_batchSize;
extern int g_commandPollInterval;
extern bool g_httpPollingEnabled;

// Static member initialization
int ConfigManager::s_currentVersion = 0;
nlohmann::json ConfigManager::s_config = nlohmann::json::object();
std::string ConfigManager::s_configPath = "config.json";

void ConfigManager::init() {
    try {
        ConfigReader reader(s_configPath);
        s_config = reader.getJson();
        s_currentVersion = reader.getConfigVersion();
        
        // Initial application of config values
        applyChanges(s_config);
        
        LOG_INFO("ConfigManager initialized. Version: " + std::to_string(s_currentVersion));
    } catch (const std::exception& e) {
        LOG_ERROR("ConfigManager init failed: " + std::string(e.what()));
        // Fallback defaults are handled by getters
    }
}

bool ConfigManager::checkAndUpdate(int serverVersion) {
    if (serverVersion > s_currentVersion) {
        LOG_INFO("New config version available: " + std::to_string(serverVersion) + 
                 " (Current: " + std::to_string(s_currentVersion) + ")");
        return downloadAndApply();
    }
    return false;
}

// Getters with defaults
int ConfigManager::getHeartbeatInterval() {
    return getJsonValue<int>(s_config, "heartbeat_interval_seconds", 30);
}

int ConfigManager::getBatchSize() {
    return getJsonValue<int>(s_config, "batch_size", 100);
}

bool ConfigManager::isHttpPollingEnabled() {
    return getJsonValue<bool>(s_config, "enable_http_polling", true);
}

int ConfigManager::getHttpPollInterval() {
    return getJsonValue<int>(s_config, "command_poll_interval_seconds", 5);
}

bool ConfigManager::isModuleEnabled(const std::string& moduleName) {
    if (s_config.contains("modules") && s_config["modules"].contains(moduleName)) {
        return s_config["modules"][moduleName].value("enabled", true); 
    }
    return true; // Default enabled
}

int ConfigManager::getCurrentVersion() {
    return s_currentVersion;
}

// ----------------------------------------------------------------------------
// Internal Logic
// ----------------------------------------------------------------------------

bool ConfigManager::downloadAndApply() {
    LOG_INFO("Downloading new configuration...");
    
    // Read server config to construct full URL
    ConfigReader reader(s_configPath);
    std::string server = reader.getHttpServer();
    int port = reader.getHttpPort();
    std::string authToken = reader.getAuthToken();
    
    // Construct full URL for GET request
    std::string fullUrl = "http://" + server + ":" + std::to_string(port) + "/api/v1/config/";
    LOG_INFO("Config URL: " + fullUrl);
    
    // Create temporary HttpClient with auth token
    HttpClient client;
    client.addHeader("Authorization", "AgentToken " + authToken);
    client.addHeader("Content-Type", "application/json");
    
    // HttpClient::GET returns the response body as a string
    std::string responseBody = client.GET(fullUrl);
    
    if (responseBody.empty()) {
        LOG_ERROR("Config download failed: Empty response");
        return false;
    }

    try {
        nlohmann::json newConfig = nlohmann::json::parse(responseBody);
        
        // Extract config object if wrapped
        if (newConfig.contains("config")) {
            newConfig = newConfig["config"];
        }

        if (!validateConfig(newConfig)) {
            LOG_ERROR("Config validation failed. Aborting update.");
            return false;
        }

        if (saveConfig(newConfig)) {
            s_config = newConfig;
            s_currentVersion = getJsonValue<int>(s_config, "_config_version", 0);
            applyChanges(s_config);
            LOG_INFO("Configuration updated successfully to version " + std::to_string(s_currentVersion));
            return true;
        } else {
            LOG_ERROR("Failed to save config to disk");
            return false;
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Exception processing config update: " + std::string(e.what()));
        return false;
    }
}

bool ConfigManager::validateConfig(const nlohmann::json& config) {
    // 1. Heartbeat Interval (10s - 300s)
    if (config.contains("heartbeat_interval_seconds")) {
        int val = config["heartbeat_interval_seconds"];
        if (val < 10 || val > 300) {
            LOG_ERROR("Validation Error: heartbeat_interval_seconds out of range (10-300)");
            return false;
        }
    }

    // 2. Batch Size (10 - 1000)
    if (config.contains("batch_size")) {
        int val = config["batch_size"];
        if (val < 10 || val > 1000) {
            LOG_ERROR("Validation Error: batch_size out of range (10-1000)");
            return false;
        }
    }

    // 3. Poll Interval (1s - 60s)
    if (config.contains("command_poll_interval_seconds")) {
        int val = config["command_poll_interval_seconds"];
        if (val < 1 || val > 60) {
            LOG_ERROR("Validation Error: command_poll_interval_seconds out of range (1-60)");
            return false;
        }
    }

    return true;
}

bool ConfigManager::saveConfig(const nlohmann::json& newConfig) {
    try {
        // 1. Read existing config to preserve bootstrap settings
        std::ifstream inFile(s_configPath);
        nlohmann::json localConfig;
        if (inFile.is_open()) {
            inFile >> localConfig;
            inFile.close();
            
            // Backup existing config
            std::filesystem::copy(s_configPath, s_configPath + ".bak", std::filesystem::copy_options::overwrite_existing);
        }

        // 2. Prepare final config (Merge)
        nlohmann::json finalConfig = newConfig;

        // 3. Force overwrite bootstrap fields from local file
        if (localConfig.contains("server_url")) {
            finalConfig["server_url"] = localConfig["server_url"];
        }
        if (localConfig.contains("enrollment_token")) {
            finalConfig["enrollment_token"] = localConfig["enrollment_token"];
        }
        if (localConfig.contains("auth_file_path")) {
            finalConfig["auth_file_path"] = localConfig["auth_file_path"];
        }

        // 4. Atomic Write (.tmp -> rename)
        std::string tmpPath = s_configPath + ".tmp";
        std::ofstream outFile(tmpPath);
        if (!outFile.is_open()) return false;
        
        outFile << finalConfig.dump(4);
        outFile.close();

        // Atomic move
        if (MoveFileExA(tmpPath.c_str(), s_configPath.c_str(), MOVEFILE_REPLACE_EXISTING) == 0) {
            LOG_ERROR("Failed to rename temporary config file. Error: " + std::to_string(GetLastError()));
            return false;
        }

        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception saving config: " + std::string(e.what()));
        rollback();
        return false;
    }
}

void ConfigManager::rollback() {
    // Restore from .bak
    try {
        std::filesystem::copy(s_configPath + ".bak", s_configPath, std::filesystem::copy_options::overwrite_existing);
        LOG_INFO("Rolled back to previous configuration backup.");
    } catch (...) {
        LOG_ERROR("CRITICAL: Failed to rollback configuration!");
    }
}

void ConfigManager::applyChanges(const nlohmann::json& config) {
    // Update global variables for dynamic components
    if (config.contains("batch_size")) {
        g_batchSize = config["batch_size"];
    }
    
    if (config.contains("command_poll_interval_seconds")) {
        g_commandPollInterval = config["command_poll_interval_seconds"];
    }
    
    if (config.contains("enable_http_polling")) {
        g_httpPollingEnabled = config["enable_http_polling"];
    }
    
    // Heartbeat updates happen via HeartbeatManager checks
    // Module toggles checked by threads
}

// Helper implementation
template<typename T>
T ConfigManager::getJsonValue(const nlohmann::json& j, const std::string& key, T defaultValue) {
    if (j.contains(key)) {
        return j[key].get<T>();
    }
    return defaultValue;
}
