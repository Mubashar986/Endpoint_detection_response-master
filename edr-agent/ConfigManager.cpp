#include "ConfigManager.hpp"
#include "HttpClient.hpp"
#include "Logger.hpp"
#include "ConfigReader.hpp"
#include "SysmonConfigGenerator.hpp"
#include <fstream>
#include <filesystem>
#include <windows.h> // For MoveFileEx and GetModuleFileNameW

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
std::string ConfigManager::s_configPath = "config.json"; // Default fallback

void ConfigManager::init() {
    try {
        // Resolve absolute path to config.json based on executable location
        // This is critical when running as a Service where CWD is System32
        wchar_t exePath[MAX_PATH];
        if (GetModuleFileNameW(NULL, exePath, MAX_PATH) != 0) {
            std::filesystem::path path(exePath);
            s_configPath = (path.parent_path() / "config.json").string();
            LOG_INFO("ConfigManager: Resolved config path to " + s_configPath);
        } else {
            LOG_WARN("ConfigManager: Failed to get module path, using default: " + s_configPath);
        }

        ConfigReader reader(s_configPath);
        s_config = reader.getJson();
        s_currentVersion = reader.getConfigVersion();
        
        // REFACTOR FIX: Force sync if policy file is missing
        // Even if config.json says "version 1", if we don't have the policy file,
        // we should treat it as version 0 to trigger a download.
        std::filesystem::path policyPath = std::filesystem::path(s_configPath).parent_path() / "agent_policy.json";
        if (!std::filesystem::exists(policyPath)) {
            LOG_INFO("Policy file missing. Forcing initial sync (Version 0)");
            s_currentVersion = 0;
        }
        
        // Initial application of config values
        applyChanges(s_config);
        
        LOG_INFO("ConfigManager initialized. Version: " + std::to_string(s_currentVersion));
    } catch (const std::exception& e) {
        LOG_ERROR("ConfigManager init failed: " + std::string(e.what()));
        // Fallback defaults are handled by getters
    } catch (...) {
        LOG_ERROR("ConfigManager init failed with unknown error");
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
    try {
        if (!s_config.empty() && s_config.contains("modules") && s_config["modules"].contains(moduleName)) {
            return s_config["modules"][moduleName].value("enabled", true); 
        }
    } catch (...) {
        // Safe fallback
    }
    return true; // Default enabled
}

bool ConfigManager::isEventEnabled(int eventId) {
    // Map Sysmon Event ID to module name
    std::string moduleName;
    switch (eventId) {
        case 1:  // ProcessCreate
        case 5:  // ProcessTerminate
            moduleName = "process_monitor";
            break;
        case 3:  // NetworkConnect
            moduleName = "network_monitor";
            break;
        case 11: // FileCreate
            moduleName = "file_monitor";
            break;
        case 12: // RegistryEvent (Object create and delete)
        case 13: // RegistryEvent (Value Set)
        case 14: // RegistryEvent (Key and Value Rename)
            moduleName = "registry_monitor";
            break;
        case 22: // DnsQuery
            moduleName = "dns_monitor";
            break;
        default:
            return true;
    }
    return isModuleEnabled(moduleName);
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
    // Use s_configPath which is now absolute
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
            // RELOAD Strategy:
            // Instead of just assigning newConfig, we verify the full merged state
            // by reloading via ConfigManager logic (simulating a restart state).
            ConfigReader reader(s_configPath); // This now triggers merge logic
            s_config = reader.getJson();       // This contains Bootstrap + Policy

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
        std::filesystem::path configPath(s_configPath);
        std::filesystem::path policyPath = configPath.parent_path() / "agent_policy.json";
        std::filesystem::path policyBak = configPath.parent_path() / "agent_policy.json.bak";

        // 1. Backup existing policy if it exists
        if (std::filesystem::exists(policyPath)) {
            std::filesystem::copy(policyPath, policyBak, std::filesystem::copy_options::overwrite_existing);
        }

        // 2. Write new policy to .tmp
        std::string tmpPath = policyPath.string() + ".tmp";
        std::ofstream outFile(tmpPath);
        if (!outFile.is_open()) return false;
        
        // We write the received config directly as the policy
        outFile << newConfig.dump(4);
        outFile.close();

        // 3. Atomic move
        if (MoveFileExA(tmpPath.c_str(), policyPath.string().c_str(), MOVEFILE_REPLACE_EXISTING) == 0) {
            LOG_ERROR("Failed to rename temporary policy file. Error: " + std::to_string(GetLastError()));
            return false;
        }

        LOG_INFO("Successfully saved new policy to: " + policyPath.string());
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Exception saving policy: " + std::string(e.what()));
        rollback();
        return false;
    }
}

void ConfigManager::rollback() {
    try {
        std::filesystem::path configPath(s_configPath);
        std::filesystem::path policyPath = configPath.parent_path() / "agent_policy.json";
        std::filesystem::path policyBak = configPath.parent_path() / "agent_policy.json.bak";

        if (std::filesystem::exists(policyBak)) {
            std::filesystem::copy(policyBak, policyPath, std::filesystem::copy_options::overwrite_existing);
            LOG_INFO("Rolled back to previous policy backup.");
        } else {
            LOG_WARN("No policy backup found to rollback to.");
        }
    } catch (...) {
        LOG_ERROR("CRITICAL: Failed to rollback policy!");
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

    // NEW: Regenerate Sysmon config
    if (config.contains("modules")) {
        // Resolve path relative to config.json
        std::filesystem::path configDir = std::filesystem::path(s_configPath).parent_path();
        std::string sysmonConfigPath = (configDir / "sysmonconfig-live.xml").string();
        
        LOG_INFO("Generating Sysmon config at: " + sysmonConfigPath);
        
        if (SysmonConfigGenerator::generateConfig(config["modules"], sysmonConfigPath)) {
            reloadSysmon(sysmonConfigPath);
        } else {
            LOG_ERROR("Failed to generate Sysmon config");
        }
    }
}

void ConfigManager::reloadSysmon(const std::string& configPath) {
    // Dynamic Sysmon Path Resolution
    // 1. Check for bundled Sysmon64.exe in same directory as agent
    std::string sysmonCmd = "sysmon64"; // Default fallback (PATH)
    
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) != 0) {
        std::filesystem::path agentDir = std::filesystem::path(exePath).parent_path();
        std::filesystem::path bundledSysmon = agentDir / "Sysmon64.exe";
        
        if (std::filesystem::exists(bundledSysmon)) {
            // Use absolute path to bundled Sysmon
            sysmonCmd = "\"" + bundledSysmon.string() + "\"";
            LOG_INFO("Found bundled Sysmon at: " + bundledSysmon.string());
        }
    }

    std::string cmd = sysmonCmd + " -c \"" + configPath + "\"";
    
    LOG_INFO("Reloading Sysmon command: " + cmd);
    
    int result = system(cmd.c_str());
    
    if (result == 0) {
        LOG_INFO("Sysmon reloaded successfully.");
    } else {
        LOG_WARN("Sysmon reload command returned code: " + std::to_string(result));
        // Fallback or retry logic could go here
    }
}

// Helper implementation
template<typename T>
T ConfigManager::getJsonValue(const nlohmann::json& j, const std::string& key, T defaultValue) {
    try {
        if (j.contains(key)) {
            return j[key].get<T>();
        }
    } catch (...) {}
    return defaultValue;
}
