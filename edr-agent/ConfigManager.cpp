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
    LOG_INFO("[DEBUG] ===== downloadAndApply() STARTED =====");
    LOG_INFO("[DEBUG] Current s_currentVersion: " + std::to_string(s_currentVersion));
    LOG_INFO("[DEBUG] Config path: " + s_configPath);
    
    // Read server config to construct full URL
    ConfigReader reader(s_configPath);
    std::string server = reader.getHttpServer();
    int port = reader.getHttpPort();
    std::string authToken = reader.getAuthToken();
    
    std::string fullUrl = "http://" + server + ":" + std::to_string(port) + "/api/v1/config/";
    LOG_INFO("[DEBUG] Requesting config from: " + fullUrl);
    
    HttpClient client;
    client.addHeader("Authorization", "AgentToken " + authToken);
    client.addHeader("Content-Type", "application/json");
    
    std::string responseBody = client.GET(fullUrl);
    
    if (responseBody.empty()) {
        LOG_ERROR("[DEBUG] Config download FAILED: Empty response from server");
        return false;
    }
    
    LOG_INFO("[DEBUG] Received response: " + responseBody.substr(0, 200) + "...");

    try {
        nlohmann::json newConfig = nlohmann::json::parse(responseBody);
        
        if (newConfig.contains("config")) {
            newConfig = newConfig["config"];
            LOG_INFO("[DEBUG] Extracted 'config' object from response");
        }
        
        LOG_INFO("[DEBUG] New config keys: " + std::to_string(newConfig.size()));
        if (newConfig.contains("_config_version")) {
            LOG_INFO("[DEBUG] New _config_version: " + std::to_string(newConfig["_config_version"].get<int>()));
        }

        if (!validateConfig(newConfig)) {
            LOG_ERROR("[DEBUG] Config VALIDATION FAILED. Aborting.");
            return false;
        }
        LOG_INFO("[DEBUG] Config validation PASSED");

        LOG_INFO("[DEBUG] Calling saveConfig()...");
        if (saveConfig(newConfig)) {
            LOG_INFO("[DEBUG] saveConfig() returned TRUE");
            
            // Verify the file was actually written
            std::filesystem::path policyPath = std::filesystem::path(s_configPath).parent_path() / "agent_policy.json";
            if (std::filesystem::exists(policyPath)) {
                auto fileSize = std::filesystem::file_size(policyPath);
                LOG_INFO("[DEBUG] agent_policy.json exists, size: " + std::to_string(fileSize) + " bytes");
                
                // Read first 100 chars to verify content
                std::ifstream verifyFile(policyPath);
                std::string firstChars;
                std::getline(verifyFile, firstChars);
                LOG_INFO("[DEBUG] agent_policy.json first line: " + firstChars.substr(0, 100));
            } else {
                LOG_ERROR("[DEBUG] agent_policy.json DOES NOT EXIST after saveConfig!");
            }
            
            LOG_INFO("[DEBUG] Reloading config via ConfigReader...");
            ConfigReader reloadReader(s_configPath);
            s_config = reloadReader.getJson();
            
            // FIX: Server sends "config_version", agent was looking for "_config_version"
            // Check for both keys for compatibility
            if (s_config.contains("config_version")) {
                s_currentVersion = s_config["config_version"].get<int>();
            } else if (s_config.contains("_config_version")) {
                s_currentVersion = s_config["_config_version"].get<int>();
            } else {
                s_currentVersion = 0;  // Default if neither found
            }
            LOG_INFO("[DEBUG] Reloaded config version: " + std::to_string(s_currentVersion));
            
            LOG_INFO("[DEBUG] Calling applyChanges()...");
            applyChanges(s_config);
            
            LOG_INFO("[DEBUG] ===== downloadAndApply() SUCCESS =====");
            return true;
        } else {
            LOG_ERROR("[DEBUG] saveConfig() returned FALSE");
            return false;
        }

    } catch (const std::exception& e) {
        LOG_ERROR("[DEBUG] EXCEPTION in downloadAndApply: " + std::string(e.what()));
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
    LOG_INFO("[DEBUG] ===== saveConfig() STARTED =====");
    
    try {
        std::filesystem::path configPath(s_configPath);
        std::filesystem::path policyPath = configPath.parent_path() / "agent_policy.json";
        std::filesystem::path policyBak = configPath.parent_path() / "agent_policy.json.bak";
        std::string tmpPath = policyPath.string() + ".tmp";
        
        LOG_INFO("[DEBUG] Policy path: " + policyPath.string());
        LOG_INFO("[DEBUG] Backup path: " + policyBak.string());
        LOG_INFO("[DEBUG] Temp path: " + tmpPath);
        
        // Check current state
        LOG_INFO("[DEBUG] agent_policy.json exists: " + std::string(std::filesystem::exists(policyPath) ? "YES" : "NO"));
        LOG_INFO("[DEBUG] agent_policy.json.bak exists: " + std::string(std::filesystem::exists(policyBak) ? "YES" : "NO"));

        // 1. Backup existing policy if it exists
        if (std::filesystem::exists(policyPath)) {
            LOG_INFO("[DEBUG] Step 1: Backing up existing policy...");
            try {
                std::filesystem::copy(policyPath, policyBak, std::filesystem::copy_options::overwrite_existing);
                LOG_INFO("[DEBUG] Step 1: Backup created successfully");
            } catch (const std::exception& backupErr) {
                LOG_ERROR("[DEBUG] Step 1 FAILED: Backup failed: " + std::string(backupErr.what()));
                // Continue anyway - backup failure shouldn't block update
            }
        } else {
            LOG_INFO("[DEBUG] Step 1: No existing policy to backup (first sync)");
        }

        // 2. Write new policy to .tmp
        LOG_INFO("[DEBUG] Step 2: Writing new config to .tmp file...");
        std::ofstream outFile(tmpPath);
        if (!outFile.is_open()) {
            DWORD err = GetLastError();
            LOG_ERROR("[DEBUG] Step 2 FAILED: Cannot open .tmp file for writing. Error: " + std::to_string(err));
            return false;
        }
        
        std::string configStr = newConfig.dump(4);
        outFile << configStr;
        outFile.flush();
        
        if (outFile.fail()) {
            LOG_ERROR("[DEBUG] Step 2 FAILED: Write to .tmp file failed");
            outFile.close();
            return false;
        }
        outFile.close();
        
        // Verify .tmp was written
        if (std::filesystem::exists(tmpPath)) {
            auto tmpSize = std::filesystem::file_size(tmpPath);
            LOG_INFO("[DEBUG] Step 2: .tmp file created, size: " + std::to_string(tmpSize) + " bytes");
        } else {
            LOG_ERROR("[DEBUG] Step 2 FAILED: .tmp file does not exist after write!");
            return false;
        }

        // 3. Atomic move
        LOG_INFO("[DEBUG] Step 3: Atomic move .tmp -> agent_policy.json...");
        
        // First, delete target if exists (MoveFileEx sometimes fails on Windows Service)
        if (std::filesystem::exists(policyPath)) {
            LOG_INFO("[DEBUG] Step 3a: Deleting existing target file for clean rename...");
            if (!DeleteFileA(policyPath.string().c_str())) {
                DWORD delErr = GetLastError();
                LOG_WARN("[DEBUG] Step 3a: DeleteFile failed (might be OK): Error " + std::to_string(delErr));
                // Try to proceed anyway - MoveFileEx might still work
            }
        }
        
        if (MoveFileExA(tmpPath.c_str(), policyPath.string().c_str(), MOVEFILE_REPLACE_EXISTING) == 0) {
            DWORD moveErr = GetLastError();
            LOG_ERROR("[DEBUG] Step 3 FAILED: MoveFileExA error: " + std::to_string(moveErr));
            
            // Decode common error codes
            if (moveErr == 5) LOG_ERROR("[DEBUG] Error 5 = ACCESS_DENIED (file locked or permissions)");
            if (moveErr == 32) LOG_ERROR("[DEBUG] Error 32 = SHARING_VIOLATION (file in use by another process)");
            if (moveErr == 183) LOG_ERROR("[DEBUG] Error 183 = ALREADY_EXISTS (shouldn't happen with REPLACE flag)");
            
            // FALLBACK: Try std::filesystem::rename
            LOG_INFO("[DEBUG] Trying fallback: std::filesystem::rename...");
            try {
                std::filesystem::rename(tmpPath, policyPath);
                LOG_INFO("[DEBUG] Fallback rename SUCCEEDED!");
            } catch (const std::exception& renameErr) {
                LOG_ERROR("[DEBUG] Fallback rename FAILED: " + std::string(renameErr.what()));
                return false;
            }
        } else {
            LOG_INFO("[DEBUG] Step 3: MoveFileExA SUCCEEDED");
        }
        
        // Final verification
        if (std::filesystem::exists(policyPath)) {
            auto finalSize = std::filesystem::file_size(policyPath);
            LOG_INFO("[DEBUG] FINAL: agent_policy.json exists, size: " + std::to_string(finalSize) + " bytes");
        } else {
            LOG_ERROR("[DEBUG] FINAL: agent_policy.json STILL MISSING after all attempts!");
            return false;
        }

        LOG_INFO("[DEBUG] ===== saveConfig() SUCCESS =====");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("[DEBUG] EXCEPTION in saveConfig: " + std::string(e.what()));
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
