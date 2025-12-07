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
    jsonObject = parseJsonFile(configFilePath);
}

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
    // Check if HTTP polling is disabled (for WebSocket-only testing)
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
