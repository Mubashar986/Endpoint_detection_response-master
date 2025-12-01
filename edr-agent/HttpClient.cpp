#include "HttpClient.hpp"
#include "ConfigReader.hpp"
#include "SimpleZstd.hpp"
#include <iostream>
#include <vector>

HttpClient::HttpClient(const std::string& serverHost, int serverPort, 
                       const std::string& apiPath, const std::string& token) {
    server = stringToWstring(serverHost);
    port = serverPort;
    path = stringToWstring(apiPath);
    authToken = stringToWstring("Token " + token);
    
    // Initialize handles to NULL
    hSession = NULL;
    hConnect = NULL;
    
    std::cout << "[HTTP] Client initialized for Django backend (Keep-Alive)" << std::endl;
}

HttpClient::HttpClient() : port(0), hSession(NULL), hConnect(NULL) {}

HttpClient::~HttpClient() {
    disconnect();
}

void HttpClient::addHeader(const std::string& key, const std::string& value) {
    customHeaders.push_back({stringToWstring(key), stringToWstring(value)});
}

bool HttpClient::connect() {
    if (hSession && hConnect) return true;
    
    // 1. Open Session
    if (!hSession) {
        hSession = WinHttpOpen(L"EDR-Agent/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) {
            std::cerr << "[HTTP] WinHttpOpen failed. Error: " << GetLastError() << std::endl;
            return false;
        }
    }
    
    // 2. Connect
    if (!hConnect) {
        hConnect = WinHttpConnect(hSession, server.c_str(), (INTERNET_PORT)port, 0);
        if (!hConnect) {
            std::cerr << "[HTTP] WinHttpConnect failed. Error: " << GetLastError() << std::endl;
            return false;
        }
    }
    
    return true;
}

void HttpClient::disconnect() {
    if (hConnect) {
        WinHttpCloseHandle(hConnect);
        hConnect = NULL;
    }
    if (hSession) {
        WinHttpCloseHandle(hSession);
        hSession = NULL;
    }
}

bool HttpClient::ensureConnection() {
    if (connect()) return true;
    
    // Retry once
    std::cerr << "[HTTP] Connection lost, retrying..." << std::endl;
    disconnect();
    return connect();
}

std::string HttpClient::GET(const std::string& endpoint) {
    // GET requests usually go to different endpoints, so we might need a temporary request handle
    // BUT we can reuse the Session and Connect handles if the host is the same.
    // However, the current usage of GET in CommandProcessor seems to use full URLs.
    // For simplicity in this refactor, we will stick to the previous implementation for GET
    // because it's used for "polling" or "downloading" from potentially different URLs?
    // Actually, CommandProcessor uses it for polling commands from the SAME server.
    // So we SHOULD reuse the connection if possible.
    
    // BUT, `endpoint` passed here is a full URL string.
    // Our persistent connection is to `server` and `port`.
    // If `endpoint` matches our server/port, we can reuse.
    // For MVP stability, let's just make GET robust but maybe not strictly reuse the *same* handle 
    // if the logic is complex to parse.
    // Actually, let's just use the old "one-off" logic for GET for now to minimize risk,
    // as the critical part is the high-volume POST telemetry.
    
    // ... (Keep original GET implementation for now, or minimal fix) ...
    // To save space and focus on the requested fix (Telemetry POST), I will keep GET as is 
    // but wrapped in a standalone session to avoid interfering with our persistent one.
    
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
        return "";
    }
    
    HINTERNET hTempSession = WinHttpOpen(L"EDR-Agent/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hTempSession) return "";
    
    HINTERNET hTempConnect = WinHttpConnect(hTempSession, hostName, urlComp.nPort, 0);
    if (!hTempConnect) {
        WinHttpCloseHandle(hTempSession);
        return "";
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hTempConnect, L"GET", urlPath, NULL,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hTempConnect);
        WinHttpCloseHandle(hTempSession);
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
    
    std::string response;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        if (WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD dwSize = 0;
            DWORD dwDownloaded = 0;
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
        }
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hTempConnect);
    WinHttpCloseHandle(hTempSession);
    return response;
}

std::string HttpClient::POST(const std::string& endpoint, const std::string& data) {
    // Generic POST method used by CommandProcessor to report action results
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
        std::cerr << "[HTTP] Failed to parse URL: " << endpoint << std::endl;
        return "";
    }
    
    HINTERNET hTempSession = WinHttpOpen(L"EDR-Agent/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hTempSession) {
        std::cerr << "[HTTP] WinHttpOpen failed" << std::endl;
        return "";
    }
    
    HINTERNET hTempConnect = WinHttpConnect(hTempSession, hostName, urlComp.nPort, 0);
    if (!hTempConnect) {
        WinHttpCloseHandle(hTempSession);
        std::cerr << "[HTTP] WinHttpConnect failed" << std::endl;
        return "";
    }
    
    HINTERNET hRequest = WinHttpOpenRequest(hTempConnect, L"POST", urlPath, NULL,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hTempConnect);
        WinHttpCloseHandle(hTempSession);
        std::cerr << "[HTTP] WinHttpOpenRequest failed" << std::endl;
        return "";
    }
    
    // Add headers (including custom headers like Authorization)
    std::wstring headersStr = L"Content-Type: application/json\r\n";
    for (const auto& header : customHeaders) {
        headersStr += header.first + L": " + header.second + L"\r\n";
    }
    
    if (!headersStr.empty()) {
        WinHttpAddRequestHeaders(hRequest, headersStr.c_str(), -1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    
    std::string response;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          (LPVOID)data.c_str(), (DWORD)data.length(),
                          (DWORD)data.length(), 0)) {
        if (WinHttpReceiveResponse(hRequest, NULL)) {
            DWORD dwStatusCode = 0;
            DWORD dwSize = sizeof(dwStatusCode);
            WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
            
            std::cout << "[HTTP] Command result reported: " << dwStatusCode << std::endl;
            
            // Read response body
            DWORD dwDownloaded = 0;
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
        }
    } else {
        std::cerr << "[HTTP] POST request failed" << std::endl;
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hTempConnect);
    WinHttpCloseHandle(hTempSession);
    return response;
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
        nlohmann::json batchJson = events;
        std::string jsonStr = batchJson.dump();
        
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

#include "SimpleZstd.hpp"

bool HttpClient::compressData(const std::string& data, std::vector<BYTE>& compressedData) {
    return SimpleZstd::compress(data, compressedData);
}

bool HttpClient::sendCompressedHttpPost(const std::vector<BYTE>& compressedData) {
    if (!ensureConnection()) return false;
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/api/v1/telemetry/",
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            0);
    if (!hRequest) {
        std::cerr << "[HTTP] WinHttpOpenRequest failed: " << GetLastError() << std::endl;
        // If request creation fails, maybe connection is dead?
        disconnect();
        return false;
    }
    
    // Add Headers: Content-Type: application/json, Content-Encoding: zstd, Authorization
    std::wstring headers = L"Content-Type: application/json\r\nContent-Encoding: zstd\r\nAuthorization: " + authToken;
    
    // The WinHttpAddRequestHeaders call is implicitly handled by passing headers to WinHttpSendRequest
    // if WINHTTP_NO_ADDITIONAL_HEADERS is not used.
    // However, the original code explicitly called WinHttpAddRequestHeaders.
    // The snippet provided replaces the WinHttpAddRequestHeaders with passing headers directly to WinHttpSendRequest.
    // I will follow the snippet's instruction for the first send, and keep the explicit WinHttpAddRequestHeaders
    // for the retry logic for consistency with the original structure.
    
    bool bResults = WinHttpSendRequest(hRequest,
                                       headers.c_str(), (DWORD)headers.length(),
                                       (LPVOID)compressedData.data(), (DWORD)compressedData.size(),
                                       (DWORD)compressedData.size(), 0);
                                       
    // Auto-Retry Logic on Failure
    if (!bResults) {
        std::cerr << "[HTTP] Send failed (" << GetLastError() << "). Reconnecting..." << std::endl;
        WinHttpCloseHandle(hRequest);
        disconnect();
        if (ensureConnection()) {
            // Re-create request
            hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL,
                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
            if (hRequest) {
                WinHttpAddRequestHeaders(hRequest, headers.c_str(), -1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
                bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                              (LPVOID)compressedData.data(), (DWORD)compressedData.size(),
                                              (DWORD)compressedData.size(), 0);
            }
        }
    }

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }
    
    if (bResults) {
        DWORD dwStatusCode = 0;
        DWORD dwSize = sizeof(dwStatusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
                            WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
                            
        if (dwStatusCode == 200 || dwStatusCode == 201) {
            // Success!
            // Drain response to keep connection alive
            DWORD dwDownloaded = 0;
            DWORD dwAvailable = 0;
            do {
                dwAvailable = 0;
                if (!WinHttpQueryDataAvailable(hRequest, &dwAvailable)) break;
                if (dwAvailable == 0) break;
                char* buffer = new char[dwAvailable];
                WinHttpReadData(hRequest, buffer, dwAvailable, &dwDownloaded);
                delete[] buffer;
            } while (dwAvailable > 0);
        } else {
            std::cerr << "[HTTP] Server returned error: " << dwStatusCode << std::endl;
            bResults = false;
        }
    }
    
    WinHttpCloseHandle(hRequest);
    return bResults;
}

bool HttpClient::sendHttpPost(const std::string& jsonData) {
    if (!ensureConnection()) return false;
    
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        disconnect();
        return false;
    }
    
    std::wstring headers = L"Content-Type: application/json\r\nAuthorization: " + authToken + L"\r\n";
    WinHttpAddRequestHeaders(hRequest, headers.c_str(), -1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    
    bool bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       (LPVOID)jsonData.c_str(), (DWORD)jsonData.size(),
                                       (DWORD)jsonData.size(), 0);
                                       
    if (!bResults) {
        // Retry logic
        WinHttpCloseHandle(hRequest);
        disconnect();
        if (ensureConnection()) {
             hRequest = WinHttpOpenRequest(hConnect, L"POST", path.c_str(), NULL,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
             if (hRequest) {
                 WinHttpAddRequestHeaders(hRequest, headers.c_str(), -1L, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
                 bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                               (LPVOID)jsonData.c_str(), (DWORD)jsonData.size(),
                                               (DWORD)jsonData.size(), 0);
             }
        }
    }
    
    if (bResults) bResults = WinHttpReceiveResponse(hRequest, NULL);
    
    if (bResults) {
        DWORD dwStatusCode = 0;
        DWORD dwSize = sizeof(dwStatusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, 
                            WINHTTP_HEADER_NAME_BY_INDEX, &dwStatusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
        if (dwStatusCode == 201) {
            std::cout << "[HTTP] ✅ Sent (201)" << std::endl;
        } else {
            std::cerr << "[HTTP] ❌ Failed (" << dwStatusCode << ")" << std::endl;
            bResults = false;
        }
    }
    
    WinHttpCloseHandle(hRequest);
    return bResults;
}

std::wstring HttpClient::stringToWstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size);
    return wstr;
}
