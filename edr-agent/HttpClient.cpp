#include "HttpClient.hpp"
#include <iostream>

HttpClient::HttpClient(const std::string& serverHost, int serverPort, 
                       const std::string& apiPath, const std::string& token) {
    server = stringToWstring(serverHost);
    port = serverPort;
    path = stringToWstring(apiPath);
    authToken = stringToWstring("Token " + token);
    
    std::cout << "[HTTP] Client initialized for Django backend" << std::endl;
}

HttpClient::~HttpClient() {}

bool HttpClient::sendTelemetry(const nlohmann::json& eventData) {
    try {
        std::string jsonStr = eventData.dump();
        return sendHttpPost(jsonStr);
    } catch (const std::exception& e) {
        std::cerr << "[HTTP] Error: " << e.what() << std::endl;
        return false;
    }
}

bool HttpClient::sendHttpPost(const std::string& jsonData) {
    HINTERNET hSession = WinHttpOpen(L"EDR-Agent/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;
    
    HINTERNET hConnect = WinHttpConnect(hSession, server.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    std::wstring headers = L"Content-Type: application/json\r\nAuthorization: " + authToken + L"\r\n";
    DWORD dataSize = static_cast<DWORD>(jsonData.size());
    
    BOOL result = WinHttpSendRequest(hRequest, headers.c_str(), -1L,
                                     (LPVOID)jsonData.c_str(), dataSize, dataSize, 0);
    
    if (result) result = WinHttpReceiveResponse(hRequest, NULL);
    
    DWORD statusCode = 0;
    DWORD size = sizeof(statusCode);
    if (result) {
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           NULL, &statusCode, &size, NULL);
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    if (statusCode == 201) {
        std::cout << "[HTTP] ✅ Sent to Django (HTTP 201)" << std::endl;
        return true;
    } else {
        std::cerr << "[HTTP] ❌ Failed (HTTP " << statusCode << ")" << std::endl;
        return false;
    }
}

std::wstring HttpClient::stringToWstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    return wstr;
}
