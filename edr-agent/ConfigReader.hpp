#ifndef CONFIGREADER_HPP
#define CONFIGREADER_HPP

#include <filesystem>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

class ConfigReader {
public:
    explicit ConfigReader(const std::filesystem::path& configFilePath);
    std::vector<std::pair<std::wstring, std::wstring>> getPathQueryPairs();
    
    // WebSocket methods
    std::string getServerUri();
    std::string getServerReverseShellIp();
    int getServerReverseShellPort();
    
    // HTTP methods
    std::string getHttpServer();
    int getHttpPort();
    std::string getApiPath();
    std::string getAuthToken();
    
    bool hasHttpConfig();
    bool hasWebSocketConfig();

private:
    std::filesystem::path configFilePath;
    nlohmann::json jsonObject;
    static nlohmann::json parseJsonFile(
        const std::filesystem::path& configFilePath);
};

#endif // CONFIGREADER_HPP
