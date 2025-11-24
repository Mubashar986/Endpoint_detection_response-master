#ifndef HTTPCLIENT_HPP
#define HTTPCLIENT_HPP

#include <string>
#include <windows.h>
#include <winhttp.h>
#include <vector>
#include "nlohmann/json.hpp"
#include "SimpleGzip.hpp"

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
    bool sendHttpPost(const std::string& jsonData);
    bool sendCompressedHttpPost(const std::vector<BYTE>& compressedData); // New helper
    bool compressData(const std::string& data, std::vector<BYTE>& compressedData); // New helper
    std::wstring stringToWstring(const std::string& str);
};

#endif // HTTPCLIENT_HPP
