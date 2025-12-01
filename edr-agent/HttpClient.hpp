#ifndef HTTPCLIENT_HPP
#define HTTPCLIENT_HPP

#include <string>
#include <windows.h>
#include <winhttp.h>
#include <vector>
#include "nlohmann/json.hpp"
#include "SimpleZstd.hpp"

#pragma comment(lib, "winhttp.lib")

class HttpClient {
private:
    std::wstring server;
    int port;
    std::wstring path;
    std::wstring authToken;
    std::vector<std::pair<std::wstring, std::wstring>> customHeaders;
public:
    HttpClient(); // Default constructor
    HttpClient(const std::string& serverHost, int serverPort, 
               const std::string& apiPath, const std::string& token);
    ~HttpClient();
   
    void addHeader(const std::string& key, const std::string& value);
    std::string GET(const std::string& endpoint);
    std::string POST(const std::string& endpoint, const std::string& data);

    bool sendTelemetry(const nlohmann::json& eventData);
    bool sendTelemetryBatch(const std::vector<nlohmann::json>& events);
    
private:
    // Persistent Connection Handles
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    
    bool connect();
    void disconnect();
    bool ensureConnection();

    bool sendHttpPost(const std::string& jsonData);
    bool compressData(const std::string& data, std::vector<BYTE>& compressedData);
    bool sendCompressedHttpPost(const std::vector<BYTE>& compressedData);
    
    std::wstring stringToWstring(const std::string& str);
};

#endif // HTTPCLIENT_HPP
