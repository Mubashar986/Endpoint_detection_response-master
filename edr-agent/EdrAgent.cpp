// EdrAgent.cpp - Complete EDR Agent with HTTP (Active) and WebSocket (Ready)
#include <cstdint> // at top for uintptr_t
#include <cstdint>

#include "HttpClient.hpp"          // HTTP client for Django
#include "CommandProcessor.hpp"    // Response Actions
#include "EventConverter.hpp"      // Event format converter
#ifdef ENABLE_WEBSOCKET
#include "WebSocketClient.hpp"     // WebSocket for real-time commands
#endif
#include "ConfigReader.hpp"
#include "pugixml.hpp"

#include <Windows.h>
#include <winevt.h>
#pragma comment(lib, "wevtapi.lib")

#include <iostream>
#include <locale>
#include <conio.h>
#include <vector>

// ============================================
// Function Declarations
// ============================================
DWORD WINAPI SubscriptionCallback(EVT_SUBSCRIBE_NOTIFY_ACTION action, 
                                PVOID pContext, EVT_HANDLE hEvent);
DWORD ProcessEvent(EVT_HANDLE hEvent);
DWORD EventToEventXml(EVT_HANDLE hEvent, std::string& eventXml);
std::string EventXmlToEventJson(const std::string& xml);
std::string sanitizeUtf8(const std::string& input);

// ============================================
// Global Variables
// ============================================
#ifdef ENABLE_WEBSOCKET
WebSocketClient* g_webSocketClient = nullptr;  // WebSocket for real-time commands
#endif
HttpClient* g_httpClient = nullptr;                 // Active now

// ============================================
// Main Function
// ============================================
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  EDR Agent v1.0" << std::endl;
    std::cout << "  HTTP Mode (WebSocket added but fot the future )" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        // Step 1: Read Configuration
        std::cout << "\n[1/4] Reading configuration file..." << std::endl;
        ConfigReader configReader("config.json");
        
        // Check available modes
        bool hasHttp = configReader.hasHttpConfig();
        bool hasWebSocket = configReader.hasWebSocketConfig();
        
        std::cout << "\nConfiguration detected:" << std::endl;
        std::cout << "  HTTP: " << (hasHttp ? "✓ Available" : "✗ Not configured") << std::endl;
        std::cout << "  WebSocket: " << (hasWebSocket ? "✓ Available (not active)" : "✗ Not configured") << std::endl;
        
        if (!hasHttp) {
            std::cerr << "\n❌ ERROR: HTTP configuration not found!" << std::endl;
            std::cerr << "Please add http_server, http_port, api_path, and auth_token to config.json" << std::endl;
            return 1;
        }
        
        // Step 2: Initialize HTTP Client
        std::cout << "\n[2/4] Initializing HTTP client..." << std::endl;
        std::string httpServer = configReader.getHttpServer();
        int httpPort = configReader.getHttpPort();
        std::string apiPath = configReader.getApiPath();
        std::string authToken = configReader.getAuthToken();
        
        if (authToken.empty()) {
            std::cerr << "⚠️ WARNING: No authentication token configured!" << std::endl;
        }
        
        HttpClient httpClient(httpServer, httpPort, apiPath, authToken);
        g_httpClient = &httpClient;
        
        std::cout << "  ✓ HTTP client initialized" << std::endl;
        std::cout << "  → Target: " << httpServer << ":" << httpPort << apiPath << std::endl;
        
        // Step 2.5: Start Command Polling (unless disabled for WebSocket-only mode)
        bool disablePolling = configReader.isHttpPollingDisabled();
        if (!disablePolling) {
            std::cout << "\n[2.5/4] Starting Command Polling Service..." << std::endl;
            CommandProcessor::startCommandPolling();
        } else {
            std::cout << "\n[2.5/4] HTTP Command Polling DISABLED (WebSocket-only mode)" << std::endl;
            std::cout << "  ⚠️  Commands will only be received via WebSocket" << std::endl;
        }

        // Step 3: WebSocket (Real-time Commands)
#ifdef ENABLE_WEBSOCKET
        if (hasWebSocket) {
            std::cout << "\n[3/4] Initializing WebSocket client..." << std::endl;
            std::string wsUri = configReader.getServerUri();
            
            // Create WebSocket client on heap so it persists
            static WebSocketClient webSocketClient;
            g_webSocketClient = &webSocketClient;
            webSocketClient.connect(wsUri);
            
            std::cout << "  ✓ WebSocket connecting to: " << wsUri << std::endl;
            std::cout << "  → Commands will be received in real-time" << std::endl;
            
            // Give time for connection to establish
            Sleep(2000);
        }
#else
        if (hasWebSocket) {
            std::cout << "\n[WebSocket] Configuration found but not compiled" << std::endl;
            std::cout << "  To enable: Rebuild with -DENABLE_WEBSOCKET=ON" << std::endl;
        }
#endif
        
        // Step 4: Subscribe to Windows Event Logs
        std::cout << "\n[3/4] Subscribing to Windows Event Logs..." << std::endl;
        std::vector<std::pair<std::wstring, std::wstring>> pathQueryPairs = configReader.getPathQueryPairs();
        
        if (pathQueryPairs.empty()) {
            std::cerr << "❌ ERROR: No event sources configured!" << std::endl;
            return 1;
        }
        
        DWORD status = ERROR_SUCCESS;
        std::vector<EVT_HANDLE> subscriptions;

        for (const auto& pair : pathQueryPairs) {
            std::wstring pwsPath = pair.first;
            std::wstring pwsQuery = pair.second;
            std::wcout << L"  → Subscribing to: " << pwsPath << std::endl;

            EVT_HANDLE hSubscription = EvtSubscribe(
                NULL, 
                NULL, 
                pwsPath.c_str(), 
                pwsQuery.c_str(), 
                NULL, 
                NULL,
                (EVT_SUBSCRIBE_CALLBACK)SubscriptionCallback, 
                EvtSubscribeToFutureEvents
            );

            if (NULL == hSubscription) {
                status = GetLastError();
                
                if (ERROR_EVT_CHANNEL_NOT_FOUND == status) {
                    std::wcout << L"  ⚠️ Channel not found: " << pwsPath << std::endl;
                } else if (ERROR_EVT_INVALID_QUERY == status) {
                    std::wcout << L"  ⚠️ Invalid query: " << pwsQuery << std::endl;
                } else {
                    std::wcout << L"  ❌ Subscribe failed with error: " << status << std::endl;
                }
                
                continue; // Try next subscription
            }
            
            subscriptions.push_back(hSubscription);
            std::wcout << L"  ✓ Subscribed successfully" << std::endl;
        }
        
        if (subscriptions.empty()) {
            std::cerr << "\n❌ ERROR: No successful subscriptions!" << std::endl;
            std::cerr << "Make sure Sysmon is installed and running." << std::endl;
            return 1;
        }

        // Step 5: Monitor Events
        std::cout << "\n[4/4] ========================================" << std::endl;
        std::cout << "✓ Agent is now monitoring events" << std::endl;
        std::cout << "  Active mode: HTTP" << std::endl;
        std::cout << "  Target: " << httpServer << ":" << httpPort << std::endl;
        std::cout << "  Monitoring " << subscriptions.size() << " event source(s)" << std::endl;
        std::cout << "\nPress any key to stop monitoring..." << std::endl;
        std::cout << "========================================\n" << std::endl;

        // Main event loop
        while (!_kbhit()) {
            Sleep(100); // Sleep 100ms
        }

        // Cleanup
        std::cout << "\n\nShutting down agent..." << std::endl;
        
        CommandProcessor::stopCommandPolling();

        for (auto hSub : subscriptions) {
            if (hSub) {
                EvtClose(hSub);
            }
        }
        
        // Close WebSocket if active
        /*
        if (g_webSocketClient != nullptr) {
            g_webSocketClient->close();
            std::cout << "✓ WebSocket connection closed" << std::endl;
        }
        */
        
        std::cout << "✓ Agent stopped successfully" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\n❌ FATAL ERROR: " << e.what() << std::endl;
        return 1;
    }
}

// ============================================
// Subscription Callback
// ============================================
DWORD WINAPI SubscriptionCallback(EVT_SUBSCRIBE_NOTIFY_ACTION action, 
                                PVOID pContext, 
                                EVT_HANDLE hEvent) {
    UNREFERENCED_PARAMETER(pContext);
    DWORD status = ERROR_SUCCESS;

    switch (action) {
        case EvtSubscribeActionError:
            if (ERROR_EVT_QUERY_RESULT_STALE == (uintptr_t)hEvent) {
                std::wcout << L"⚠️ Event records are missing" << std::endl;
            } else {
                std::wcout << L"❌ Subscription error: " << (uintptr_t)hEvent << std::endl;
            }
            break;

        case EvtSubscribeActionDeliver:
            status = ProcessEvent(hEvent);
            if (ERROR_SUCCESS != status) {
                std::cerr << "❌ Failed to process event" << std::endl;
            }
            break;

        default:
            std::wcout << L"⚠️ Unknown subscription action" << std::endl;
            break;
    }

    return status;
}

// ============================================
// Process Event
// ============================================
DWORD ProcessEvent(EVT_HANDLE hEvent) {
    DWORD status = ERROR_SUCCESS;
    std::string eventXml;
    std::string eventJson;

    try {
        // Step 1: Convert event to XML
        status = EventToEventXml(hEvent, eventXml);
        if (status != ERROR_SUCCESS) {
            std::cerr << "❌ Failed to convert event to XML (Error: " << status << ")" << std::endl;
            goto cleanup;
        }

        // Step 2: Sanitize XML
        eventXml = sanitizeUtf8(eventXml);
        
        // Step 3: Convert XML to Sysmon JSON
        eventJson = EventXmlToEventJson(eventXml);
        if (eventJson.empty()) {
            std::cerr << "⚠️ Event JSON conversion returned empty" << std::endl;
            goto cleanup;
        }
        
        // Step 4: Send via WebSocket (if active)
        /*
        if (g_webSocketClient != nullptr) {
            g_webSocketClient->send(eventJson);
            std::cout << "📡 Sent to WebSocket server" << std::endl;
        }
        */
        
        // Step 5: Send via HTTP (active)
        if (g_httpClient != nullptr) {
            try {
                // Parse Sysmon JSON
                nlohmann::json sysmonEvent = nlohmann::json::parse(eventJson);
                
                // Convert to Django format
                nlohmann::json djangoEvent = EventConverter::sysmonEventToDjangoFormat(sysmonEvent);
                
                if (djangoEvent.empty()) {
                    std::cerr << "⚠️ Django format conversion returned empty" << std::endl;
                    goto cleanup;
                }
                
                // ==================================================================================
                // BATCHING LOGIC (Optimization Phase 1)
                // ==================================================================================
                // We use 'static' here so these variables persist across function calls.
                static std::vector<nlohmann::json> eventBuffer; 
                
                // TEMPORARY: Set to 1 for debugging
                static const size_t BATCH_SIZE = 100; 
                
                eventBuffer.push_back(djangoEvent);
                std::cout << "  [Buffer] Added event. Size: " << eventBuffer.size() << "/" << BATCH_SIZE << std::endl;
                
                if (eventBuffer.size() >= BATCH_SIZE) {
                    std::cout << "  [Batch] Sending " << eventBuffer.size() << " events..." << std::endl;
                    
                    if (g_httpClient->sendTelemetryBatch(eventBuffer)) {
                        std::cout << "✅ Batch sent successfully" << std::endl;
                        eventBuffer.clear();
                    } else {
                        std::cerr << "❌ Failed to send batch" << std::endl;
                        eventBuffer.clear(); 
                    }
                }
                // If buffer isn't full yet, we do nothing and wait for the next event.
                // ==================================================================================
                
                std::cout << "---" << std::endl;
                
            } catch (const nlohmann::json::parse_error& e) {
                std::cerr << "❌ JSON parse error: " << e.what() << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "❌ Exception: " << e.what() << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception in ProcessEvent: " << e.what() << std::endl;
        status = ERROR_UNHANDLED_EXCEPTION;
    }

cleanup:
    if (hEvent) {
        EvtClose(hEvent);
    }
    return status;
}

// ============================================
// Convert Event to XML
// ============================================
DWORD EventToEventXml(EVT_HANDLE hEvent, std::string& eventXml) {
    DWORD status = ERROR_SUCCESS;
    DWORD dwBufferSize = 0;
    DWORD dwBufferUsed = 0;
    DWORD dwPropertyCount = 0;
    std::vector<WCHAR> pContent;

    // First call to get required buffer size
    if (!EvtRender(NULL, hEvent, EvtRenderEventXml, dwBufferSize, 
                   pContent.data(), &dwBufferUsed, &dwPropertyCount)) {
        status = GetLastError();
        
        if (ERROR_INSUFFICIENT_BUFFER == status) {
            dwBufferSize = dwBufferUsed;
            pContent.resize(dwBufferSize, 0);
            
            // Second call with correct buffer size
            if (!EvtRender(NULL, hEvent, EvtRenderEventXml, dwBufferSize, 
                          pContent.data(), &dwBufferUsed, &dwPropertyCount)) {
                status = GetLastError();
                std::wcout << L"EvtRender failed with error: " << status << std::endl;
                goto cleanup;
            }
            
            status = ERROR_SUCCESS;
        } else {
            std::wcout << L"EvtRender failed with error: " << status << std::endl;
            goto cleanup;
        }
    }

    // Convert wide string to regular string
    if (!pContent.empty()) {
        int size = WideCharToMultiByte(CP_UTF8, 0, pContent.data(), -1, NULL, 0, NULL, NULL);
        if (size > 0) {
            std::vector<char> buffer(size);
            WideCharToMultiByte(CP_UTF8, 0, pContent.data(), -1, buffer.data(), size, NULL, NULL);
            eventXml = std::string(buffer.begin(), buffer.end() - 1); // -1 to remove null terminator
        }
    }

cleanup:
    pContent.clear();
    return status;
}

// ============================================
// Convert XML to JSON (Sysmon Format)
// ============================================
std::string EventXmlToEventJson(const std::string& xml) {
    try {
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_string(xml.c_str());

        if (!result) {
            std::cerr << "XML parsing failed: " << result.description() << std::endl;
            return "";
        }

        nlohmann::json systemJson;
        nlohmann::json eventDataJson;
        nlohmann::json eventJson;

        // Parse System section
        for (pugi::xml_node node : doc.child("Event").children()) {
            if (std::string(node.name()) == "System") {
                for (pugi::xml_node child : node.children()) {
                    std::string nodeName = child.name();
                    
                    if (nodeName == "Channel") {
                        systemJson["Channel"] = child.text().as_string();
                    }
                    else if (nodeName == "Computer") {
                        systemJson["Computer"] = child.text().as_string();
                    }
                    else if (nodeName == "Correlation") {
                        systemJson["Correlation"] = nlohmann::json::object();
                        if (child.attribute("ActivityID")) {
                            systemJson["Correlation"]["ActivityID"] = child.attribute("ActivityID").value();
                        }
                    }
                    else if (nodeName == "EventID") {
                        systemJson["EventID"] = child.text().as_int();
                    }
                    else if (nodeName == "EventRecordID") {
                        systemJson["EventRecordID"] = child.text().as_int();
                    }
                    else if (nodeName == "Execution") {
                        systemJson["Execution"]["ProcessID"] = child.attribute("ProcessID").as_int();
                        systemJson["Execution"]["ThreadID"] = child.attribute("ThreadID").as_int();
                    }
                    else if (nodeName == "Keywords") {
                        systemJson["Keywords"] = child.text().as_string();
                    }
                    else if (nodeName == "Level") {
                        systemJson["Level"] = child.text().as_int();
                    }
                    else if (nodeName == "Provider") {
                        systemJson["Provider"]["Name"] = child.attribute("Name").value();
                        if (child.attribute("Guid")) {
                            systemJson["Provider"]["Guid"] = child.attribute("Guid").value();
                        }
                    }
                    else if (nodeName == "Security") {
                        if (child.attribute("UserID")) {
                            systemJson["Security"]["UserID"] = child.attribute("UserID").value();
                        }
                    }
                    else if (nodeName == "TimeCreated") {
                        systemJson["TimeCreated"]["SystemTime"] = child.attribute("SystemTime").value();
                    }
                    else if (nodeName == "Version") {
                        systemJson["Version"] = child.text().as_int();
                    }
                }
            }
            // Parse EventData section
            else if (std::string(node.name()) == "EventData") {
                for (pugi::xml_node child : node.children()) {
                    std::string nodeAttr = child.attribute("Name").value();
                    
                    // Handle integer fields
                    if (nodeAttr == "DestinationPort" || nodeAttr == "SourcePort" || 
                        nodeAttr == "ProcessId" || nodeAttr == "TerminalSessionId") {
                        eventDataJson[nodeAttr] = child.text().as_int();
                    } else {
                        eventDataJson[nodeAttr] = child.text().as_string();
                    }
                }
            }
        }

        // Build final JSON
        eventJson["type"] = "event";
        eventJson["info"]["System"] = systemJson;
        eventJson["info"]["EventData"] = eventDataJson;

        return eventJson.dump(4);
        
    } catch (const std::exception& e) {
        std::cerr << "Error in EventXmlToEventJson: " << e.what() << std::endl;
        return "";
    }
}

// ============================================
// Sanitize UTF-8 String
// ============================================
std::string sanitizeUtf8(const std::string& input) {
    std::string output;
    output.reserve(input.length());

    for (size_t i = 0; i < input.length(); i++) {
        unsigned char c = input[i];
        
        if (c < 0x80) {
            // ASCII character
            output.push_back(c);
        } 
        else if ((c & 0xE0) == 0xC0) {
            // 2-byte UTF-8 sequence
            if (i + 1 < input.length() && (input[i + 1] & 0xC0) == 0x80) {
                output.push_back(c);
                output.push_back(input[++i]);
            }
        } 
        else if ((c & 0xF0) == 0xE0) {
            // 3-byte UTF-8 sequence
            if (i + 2 < input.length() && 
                (input[i + 1] & 0xC0) == 0x80 && 
                (input[i + 2] & 0xC0) == 0x80) {
                output.push_back(c);
                output.push_back(input[++i]);
                output.push_back(input[++i]);
            }
        } 
        else if ((c & 0xF8) == 0xF0) {
            // 4-byte UTF-8 sequence
            if (i + 3 < input.length() && 
                (input[i + 1] & 0xC0) == 0x80 && 
                (input[i + 2] & 0xC0) == 0x80 && 
                (input[i + 3] & 0xC0) == 0x80) {
                output.push_back(c);
                output.push_back(input[++i]);
                output.push_back(input[++i]);
                output.push_back(input[++i]);
            }
        }
        // Invalid UTF-8 character - skip it
    }

    return output;
}
