#include <iostream>
#include <fstream>
#include "nlohmann/json.hpp"
#include "ConfigReader.hpp"
#include "ErrorCodes.hpp"
#include "Logger.hpp"

// Store last config error for diagnostics
static AgentError g_lastConfigError = AgentError::Success;
static std::string g_lastConfigErrorMsg = "";

AgentError ConfigReader::getLastError() { return g_lastConfigError; }
std::string ConfigReader::getLastErrorMessage() { return g_lastConfigErrorMsg; }

ConfigReader::ConfigReader(
    const std::filesystem::path& configFilePath
) : configFilePath(configFilePath) {
    // ============================================
    // BOOTSTRAP LOGIC: "Bootstrap + Policy" Model
    // ============================================
    // 1. Load Bootstrap (config.json) - Always the base
    LOG_INFO("Loading bootstrap config: " + configFilePath.string());
    jsonObject = parseJsonFile(configFilePath);

    // 2. Load Policy (agent_policy.json) - If exists
    std::filesystem::path policyPath = configFilePath.parent_path() / "agent_policy.json";
    
    if (std::filesystem::exists(policyPath)) {
        LOG_INFO("Loading policy config: " + policyPath.string());
        try {
            nlohmann::json policyObject = parseJsonFile(policyPath);
            mergeJson(jsonObject, policyObject);
            LOG_INFO("Successfully merged policy into config");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to load/merge policy: " + std::string(e.what()));
            // Fallback: Continue with just bootstrap (Safe Mode)
        }
    } else {
        LOG_INFO("No policy file found (" + policyPath.string() + "). Using bootstrap defaults.");
    }
}

/*
 * NOTE: getAuthToken() is defined below (around line 188)
 * Do NOT add another definition here - C++ One Definition Rule (ODR)
 * requires each function to be defined exactly ONCE.
 */




nlohmann::json ConfigReader::parseJsonFile(const std::filesystem::path& configFilePath)
{
    nlohmann::json jsonObject;
    try {
        // Read the JSON file
        std::ifstream configFile(configFilePath);

        if (!configFile.is_open()) {
            LOG_ERROR("Failed to open config file: " + configFilePath.string());
            g_lastConfigError = AgentError::ConfigNotFound;
            g_lastConfigErrorMsg = "File not found: " + configFilePath.string();
            return nullptr;
        }
        configFile >> jsonObject;
        configFile.close();

        LOG_INFO("Successfully read config file: " + configFilePath.string());
        g_lastConfigError = AgentError::Success;
        g_lastConfigErrorMsg = "";

    }
    catch (const std::ifstream::failure& e) {
        LOG_ERROR(std::string("Exception opening/reading config: ") + e.what());
        g_lastConfigError = AgentError::ConfigNotFound;
        g_lastConfigErrorMsg = e.what();
        jsonObject = nullptr;
    }
    catch (const nlohmann::json::parse_error& e) {
        LOG_ERROR(std::string("JSON parsing error: ") + e.what());
        g_lastConfigError = AgentError::ConfigParseError;
        g_lastConfigErrorMsg = e.what();
        jsonObject = nullptr;
    }
    catch (const std::exception& e) {
        LOG_ERROR(std::string("Config exception: ") + e.what());
        g_lastConfigError = AgentError::ConfigInvalidValue;
        g_lastConfigErrorMsg = e.what();
        jsonObject = nullptr;
    }

    return jsonObject;
}

std::vector<std::pair<std::wstring, std::wstring>> ConfigReader::getPathQueryPairs()
{
    std::vector<std::pair<std::wstring, std::wstring>> pathQueryPairs;
    // Check if "event_processor" and "source" exist
    if (jsonObject.find("event_processor") != jsonObject.end() && 
        jsonObject["event_processor"].find("source") != jsonObject["event_processor"].end()) {
        auto sourceArray = jsonObject["event_processor"]["source"];

        // Iterate over the "source" array
        for (const auto& sourceObj : sourceArray) {
            // Check if "path" and "query" exist in sourceObj
            if (sourceObj.find("path") != sourceObj.end() && sourceObj.find("query") != sourceObj.end()) {
                std::string path = sourceObj["path"];
                std::wstring pwsPath = std::wstring(path.begin(), path.end());
                std::string query = sourceObj["query"];
                std::wstring pwsQuery = std::wstring(query.begin(), query.end());
                pathQueryPairs.push_back(std::make_pair(pwsPath, pwsQuery));
            } else {
                pathQueryPairs.clear();
                break;
            }
        }
    } else {
        pathQueryPairs.clear();
    }

    return pathQueryPairs;
}

// ============================================
// WebSocket Methods (Keep for future)
// ============================================

std::string ConfigReader::getServerUri()
{
    if (jsonObject.find("uri") != jsonObject.end()) {
        return jsonObject["uri"];
    } else {
        return "";
    }
}

std::string ConfigReader::getServerReverseShellIp()
{
    if (jsonObject.find("command_processor") != jsonObject.end() &&
        jsonObject["command_processor"].find("reverse_shell") != jsonObject["command_processor"].end()) {
        return jsonObject["command_processor"]["reverse_shell"]["ip"];
    } else {
        return "";
    }
}

int ConfigReader::getServerReverseShellPort()
{
    if (jsonObject.find("command_processor") != jsonObject.end() &&
        jsonObject["command_processor"].find("reverse_shell") != jsonObject["command_processor"].end()) {
        return jsonObject["command_processor"]["reverse_shell"]["port"];
    } else {
        return -1;
    }
}

// ============================================
// HTTP Methods (NEW - for Django)
// ============================================

std::string ConfigReader::getHttpServer()
{
    if (jsonObject.find("http_server") != jsonObject.end()) {
        return jsonObject["http_server"];
    } else {
        // Default to localhost if not specified
        return "localhost";
    }
}

int ConfigReader::getHttpPort()
{
    if (jsonObject.find("http_port") != jsonObject.end()) {
        return jsonObject["http_port"];
    } else {
        // Default to 8000 (Django default)
        return 8000;
    }
}

std::string ConfigReader::getApiPath()
{
    if (jsonObject.find("api_path") != jsonObject.end()) {
        return jsonObject["api_path"];
    } else {
        // Default Django telemetry endpoint
        return "/api/v1/telemetry/";
    }
}

std::string ConfigReader::getAuthToken()
{
    // 1. Priority: Check Environment Variable
    const char* envToken = std::getenv("EDR_AUTH_TOKEN");
    if (envToken != nullptr) {
        std::string token(envToken);
        if (!token.empty()) {
            std::cout << "[ConfigReader] Using Auth Token from Environment Variable" << std::endl;
            return token;
        }
    }

    // 2. Priority: Check auth.secret file (Secure local persistence)
    std::filesystem::path secretPath = configFilePath.parent_path() / "auth.secret";
    if (std::filesystem::exists(secretPath)) {
        std::ifstream secretFile(secretPath);
        std::string token;
        if (std::getline(secretFile, token)) {
            // Trim whitespace
            token.erase(token.find_last_not_of(" \n\r\t") + 1);
            if (!token.empty()) {
                std::cout << "[ConfigReader] Using Auth Token from auth.secret file" << std::endl;
                return token;
            }
        }
    }

    // 3. Fallback: Check config.json (Legacy/Dev)
    if (jsonObject.find("auth_token") != jsonObject.end()) {
        std::string token = jsonObject["auth_token"];
        
        // REJECT PLACEHOLDER
        if (token == "PLACEHOLDER_USE_ENV_VAR_EDR_AUTH_TOKEN") {
             std::cerr << "[ConfigReader] ERROR: Config contains placeholder token. Please set EDR_AUTH_TOKEN environment variable." << std::endl;
             return "";
        }

        if (!token.empty()) {
            std::cerr << "[ConfigReader] WARNING: Using hardcoded token from config.json. This is insecure." << std::endl;
            return token;
        }
    }

    // 3. Failure
    std::cerr << "[ConfigReader] CRITICAL ERROR: No Auth Token found in Environment (EDR_AUTH_TOKEN) or config.json" << std::endl;
    return "";
}

// ============================================
// Utility Methods
// ============================================

bool ConfigReader::hasHttpConfig()
{
    // Check if HTTP configuration exists
    return jsonObject.find("http_server") != jsonObject.end() ||
           jsonObject.find("http_port") != jsonObject.end();
}

bool ConfigReader::hasWebSocketConfig()
{
    // Check if WebSocket configuration exists
    return jsonObject.find("uri") != jsonObject.end();
}

bool ConfigReader::isHttpPollingDisabled()
{
    // New field: enable_http_polling (true = enabled, false = disabled)
    if (jsonObject.find("enable_http_polling") != jsonObject.end()) {
        return !jsonObject["enable_http_polling"].get<bool>();  // Return opposite
    }
    
    // Legacy field: disable_http_polling (true = disabled, false = enabled)
    if (jsonObject.find("disable_http_polling") != jsonObject.end()) {
        return jsonObject["disable_http_polling"].get<bool>();
    }
    
    return false;  // Default: HTTP polling enabled
}

int ConfigReader::getConfigVersion()
{
    // Config version for schema migrations
    // Default to 1 for backward compatibility with old configs
    if (jsonObject.find("config_version") != jsonObject.end()) {
        return jsonObject["config_version"].get<int>();
    }
    return 1;  // Default version
}

bool ConfigReader::useHttps()
{
    // Check if HTTPS is enabled (for ngrok/production)
    // Default: false for backward compatibility with localhost dev
    if (jsonObject.find("use_https") != jsonObject.end()) {
        return jsonObject["use_https"].get<bool>();
    }
    return false;
}

// ============================================
// Enrollment Methods (Phase 2)
// ============================================

std::string ConfigReader::getEnrollmentToken()
{
    /*
     * TEACHING: What is an Enrollment Token?
     * ======================================
     * When you install a new agent, you don't want to hardcode the permanent
     * authentication token. Instead:
     * 1. Admin generates a one-time "enrollment_token" 
     * 2. Installer writes it to config.json
     * 3. Agent uses it ONCE to register with server
     * 4. Server returns permanent "identity_token"
     * 5. Agent saves identity_token to auth.secret
     * 
     * This is like a "guest pass" (enrollment) vs "employee badge" (identity).
     */
    
    if (jsonObject.find("enrollment_token") != jsonObject.end()) {
        return jsonObject["enrollment_token"];
    }
    return "";
}

bool ConfigReader::needsEnrollment()
{
    /*
     * TEACHING: How do we know if agent needs to enroll?
     * ==================================================
     * Simple logic:
     *   - If auth.secret exists AND has content → Already enrolled
     *   - If auth.secret missing or empty → Needs enrollment
     * 
     * TEACHING: std::filesystem
     * =========================
     * C++17 added <filesystem> for cross-platform file operations.
     * - std::filesystem::path - Represents file/folder paths
     * - std::filesystem::exists() - Check if file exists
     * 
     * TEACHING: Why parent_path()?
     * ============================
     * configFilePath = "C:/Program Files/EDR/config.json"
     * parent_path()  = "C:/Program Files/EDR/"
     * 
     * We want auth.secret in same folder as config.json.
     */
    
    std::filesystem::path secretPath = configFilePath.parent_path() / "auth.secret";
    
    // Check if file exists
    if (std::filesystem::exists(secretPath)) {
        // File exists - but is it empty?
        std::ifstream f(secretPath);
        std::string content;
        if (std::getline(f, content)) {
            // Trim whitespace
            content.erase(content.find_last_not_of(" \n\r\t") + 1);
            if (!content.empty()) {
                return false;  // Has token = already enrolled
            }
        }
    }
    
    return true;  // No valid token = needs enrollment
}

void ConfigReader::mergeJson(nlohmann::json& target, const nlohmann::json& source) {
    for (auto it = source.begin(); it != source.end(); ++it) {
        // If both are objects, recurse
        if (it.value().is_object() && target[it.key()].is_object()) {
            mergeJson(target[it.key()], it.value());
        } else {
            // Otherwise subscribe (overwrite)
            target[it.key()] = it.value();
        }
    }
}

