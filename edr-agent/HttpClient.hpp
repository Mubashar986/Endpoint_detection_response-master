#ifndef HTTPCLIENT_HPP
#define HTTPCLIENT_HPP

#include <string>
#include <windows.h>
#include <winhttp.h>
#include "nlohmann/json.hpp"

#pragma comment(lib, "winhttp.lib")

class HttpClient {
private:
    std::wstring server;
    int port;
    std::wstring path;
    std::wstring authToken;
    
public:
    HttpClient(const std::string& serverHost, int serverPort, 
               const std::string& apiPath, const std::string& token);
    ~HttpClient();
    
    bool sendTelemetry(const nlohmann::json& eventData);
    
private:
    bool sendHttpPost(const std::string& jsonData);
    std::wstring stringToWstring(const std::string& str);
};

#endif // HTTPCLIENT_HPP
