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

HttpClient::HttpClient() : port(0) {}

HttpClient::~HttpClient() {}

void HttpClient::addHeader(const std::string& key, const std::string& value) {
    customHeaders.push_back({stringToWstring(key), stringToWstring(value)});
}

std::string HttpClient::GET(const std::string& endpoint) {
    // Parse endpoint to get path (assuming server/port already set or extracted from endpoint)
    // For MVP, we assume endpoint is just the path part OR we handle full URL if needed.
    // Given usage: client.GET(serverUrl + "/api/v1/commands/poll/");
    // We need to handle full URL parsing or just use the path if server/port are set.
    // But wait, the usage in CommandProcessor creates a NEW client with default constructor?
    // No, it creates `HttpClient client;` then sets headers.
    // It doesn't set server/port!
    
    // Correction: The CommandProcessor usage is:
    // HttpClient client;
    // client.addHeader(...)
    // client.GET(serverUrl + "/api/v1/...")
    
    // So GET needs to parse the full URL to extract server, port, and path.
    
    std::wstring fullUrl = stringToWstring(endpoint);
    URL_COMPONENTS urlComp;
    ZeroMemory(&urlComp, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    
    wchar_t hostName[256];
    wchar_t urlPath[1024];
    
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = ARRAYSIZE(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = ARRAYSIZE(urlPath);
    
    if (!WinHttpCrackUrl(fullUrl.c_str(), (DWORD)fullUrl.length(), 0, &urlComp)) {
        std::cerr << "[HTTP] Invalid URL: " << endpoint << std::endl;
        return "";
    }
    
    HINTERNET hSession = WinHttpOpen(L"EDR-Agent/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";
    
    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath, NULL,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    // Add Custom Headers
    std::wstring headersStr;
    for (const auto& header : customHeaders) {
        headersStr += header.first + L": " + header.second + L"\r\n";
    }
    
    if (!headersStr.empty()) {
        WinHttpAddRequestHeaders(hRequest, headersStr.c_str(), -1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        if (WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;
            std::string response;
            
            do {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                if (!dwSize) break;
                
                char* pszOutBuffer = new char[dwSize + 1];
                if (!pszOutBuffer) break;
                
                ZeroMemory(pszOutBuffer, dwSize + 1);
                if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                    response.append(pszOutBuffer, dwDownloaded);
                }
                delete[] pszOutBuffer;
            } while (dwSize > 0);
            
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return response;
        }
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return "";
}

std::string HttpClient::POST(const std::string& endpoint, const std::string& data) {
    std::wstring fullUrl = stringToWstring(endpoint);
    URL_COMPONENTS urlComp;
    ZeroMemory(&urlComp, sizeof(urlComp));
    urlComp.dwStructSize = sizeof(urlComp);
    
    wchar_t hostName[256];
    wchar_t urlPath[1024];
    
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = ARRAYSIZE(hostName);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = ARRAYSIZE(urlPath);
    
    if (!WinHttpCrackUrl(fullUrl.c_str(), (DWORD)fullUrl.length(), 0, &urlComp)) {
        std::cerr << "[HTTP] Invalid URL: " << endpoint << std::endl;
        return "";
    }
    
    HINTERNET hSession = WinHttpOpen(L"EDR-Agent/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";
    
    HINTERNET hConnect = WinHttpConnect(hSession, hostName, urlComp.nPort, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", urlPath, NULL,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }
    
    // Add Custom Headers
    std::wstring headersStr;
    for (const auto& header : customHeaders) {
        headersStr += header.first + L": " + header.second + L"\r\n";
    }
    // Ensure Content-Type is set if not present
    if (headersStr.find(L"Content-Type") == std::string::npos) {
        headersStr += L"Content-Type: application/json\r\n";
    }
    
    if (!headersStr.empty()) {
        WinHttpAddRequestHeaders(hRequest, headersStr.c_str(), -1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)data.c_str(), (DWORD)data.length(), (DWORD)data.length(), 0)) {
        if (WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;
            std::string response;
            
            do {
                dwSize = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
                if (!dwSize) break;
                
                char* pszOutBuffer = new char[dwSize + 1];
                if (!pszOutBuffer) break;
                
                ZeroMemory(pszOutBuffer, dwSize + 1);
                if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                    response.append(pszOutBuffer, dwDownloaded);
                }
                delete[] pszOutBuffer;
            } while (dwSize > 0);
            
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);
            return response;
        }
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return "";
}

bool HttpClient::sendTelemetry(const nlohmann::json& eventData) {
    try {
        std::string jsonStr = eventData.dump();
        return sendHttpPost(jsonStr);
    } catch (const std::exception& e) {
        std::cerr << "[HTTP] Error: " << e.what() << std::endl;
        return false;
    }
}

bool HttpClient::sendTelemetryBatch(const std::vector<nlohmann::json>& events) {
    try {
        // 1. Convert the vector of JSON objects into a single JSON array.
        nlohmann::json batchJson = events;
        
        // 2. Serialize the JSON array into a string.
        std::string jsonStr = batchJson.dump();
        
        // 3. Compress the data
        std::vector<BYTE> compressedData;
        if (compressData(jsonStr, compressedData)) {
             std::cout << "[HTTP] Compressed " << jsonStr.size() << " bytes to " << compressedData.size() << " bytes" << std::endl;
             return sendCompressedHttpPost(compressedData);
        } else {
             std::cerr << "[HTTP] Compression failed, sending plain text" << std::endl;
             return sendHttpPost(jsonStr);
        }
    } catch (const std::exception& e) {
        std::cerr << "[HTTP] Error in batch send: " << e.what() << std::endl;
        return false;
    }
}

bool HttpClient::compressData(const std::string& data, std::vector<BYTE>& compressedData) {
    return SimpleGzip::compress(data, compressedData);
}

bool HttpClient::sendCompressedHttpPost(const std::vector<BYTE>& compressedData) {
    HINTERNET hSession = WinHttpOpen(L"EDR-Agent/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        std::cerr << "[HTTP] WinHttpOpen failed. Error: " << GetLastError() << std::endl;
        return false;
    }
    
    HINTERNET hConnect = WinHttpConnect(hSession, server.c_str(), (INTERNET_PORT)port, 0);
    if (!hConnect) {
        std::cerr << "[HTTP] WinHttpConnect failed. Error: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        std::cerr << "[HTTP] WinHttpOpenRequest failed. Error: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Add Headers using WinHttpAddRequestHeaders (Safer)
    // IMPORTANT: WinHTTP requires each header to be terminated by \r\n.
    // Fix 1: Removed extra "Token " since authToken already has it.
    std::wstring headers = L"Content-Type: application/json\r\nAuthorization: " + authToken + L"\r\nContent-Encoding: gzip\r\n";
    
    // Debug: Print headers to check for weird characters
    // std::wcout << L"[HTTP] Headers: " << headers << std::endl; 

    // Pass -1L as length so WinHTTP calculates it from the null-terminator.
    if (!WinHttpAddRequestHeaders(hRequest, headers.c_str(), -1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        std::cerr << "[HTTP] WinHttpAddRequestHeaders failed. Error: " << GetLastError() << std::endl;
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Send Request with Data
    // Note: We pass WINHTTP_NO_ADDITIONAL_HEADERS because we already added them.
    bool bResults = WinHttpSendRequest(hRequest, 
                                       WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       (LPVOID)compressedData.data(), (DWORD)compressedData.size(),
                                       (DWORD)compressedData.size(), 0);
                                       
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }
    
    if (bResults) {
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        LPSTR pszOutBuffer;
        
        // Check Status Code
        DWORD dwStatusCode = 0;
        DWORD dwSizeSize = sizeof(dwStatusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
                            WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSizeSize, WINHTTP_NO_HEADER_INDEX);
                            
        if (dwStatusCode != 200 && dwStatusCode != 201) {
            std::cerr << "[HTTP] Server returned error status: " << dwStatusCode << std::endl;
            bResults = false; // Mark as failed so we don't clear the buffer
        }

        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (!dwSize) break;
            
            pszOutBuffer = new char[dwSize + 1];
            if (!pszOutBuffer) break;
            
            ZeroMemory(pszOutBuffer, dwSize + 1);
            if (WinHttpReadData(hRequest, (LPVOID)pszOutBuffer, dwSize, &dwDownloaded)) {
                 // std::cout << pszOutBuffer; 
            }
            delete[] pszOutBuffer;
        } while (dwSize > 0);
    } else {
        std::cerr << "[HTTP] Failed to send compressed request. Error: " << GetLastError() << std::endl;
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return bResults;
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
    // Fix 2: Do not include null terminator in the size
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size);
    return wstr;
}
