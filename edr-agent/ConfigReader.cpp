#include <iostream>
#include <fstream>
#include "nlohmann/json.hpp"
#include "ConfigReader.hpp"

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
            std::cerr << "Failed to open the file: " << configFilePath << std::endl;
            return nullptr;
        }
        configFile >> jsonObject;
        configFile.close();

        std::cout << "Successfully read the JSON file: " << configFilePath << std::endl;

    }
    catch (const std::ifstream::failure& e) {
        std::cerr << "Exception opening/reading/closing file: " << e.what() << std::endl;
        jsonObject = nullptr;
    }
    catch (const nlohmann::json::parse_error& e) {
        std::cerr << "JSON parsing error: " << e.what() << std::endl;
        jsonObject = nullptr;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
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
    if (jsonObject.find("auth_token") != jsonObject.end()) {
        return jsonObject["auth_token"];
    } else {
        std::cerr << "WARNING: No auth_token found in config!" << std::endl;
        return "";
    }
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
