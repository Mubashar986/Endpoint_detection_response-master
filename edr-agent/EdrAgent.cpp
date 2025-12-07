// EdrAgent.cpp - Complete EDR Agent with HTTP (Active) and WebSocket (Ready)
#include <cstdint> // at top for uintptr_t
#include <cstdint>

#include "HttpClient.hpp"          // HTTP client for Django
#include "CommandProcessor.hpp"    // Response Actions
#include "EventConverter.hpp"      // Event format converter
#include "AgentId.hpp"             // UUID-based agent identification
#include "Logger.hpp"              // Logging framework
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
#include "version.h"  // CMake-generated version header
#include <atomic>     // For graceful shutdown flag

std::string g_agentId;  // UUID-based agent identifier (set once at startup)
std::string g_agentVersion = AGENT_VERSION;  // From CMake (single source of truth)

// Graceful shutdown flag - set by console control handler
std::atomic<bool> g_shutdownRequested{false};

#ifdef ENABLE_WEBSOCKET
WebSocketClient* g_webSocketClient = nullptr;  // WebSocket for real-time commands
#endif
HttpClient* g_httpClient = nullptr;                 // Active now

// ============================================
// Console Control Handler for Graceful Shutdown
// ============================================
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    const char* signalName = "Unknown";
    switch (ctrlType) {
        case CTRL_C_EVENT:        signalName = "Ctrl+C"; break;
        case CTRL_BREAK_EVENT:    signalName = "Ctrl+Break"; break;
        case CTRL_CLOSE_EVENT:    signalName = "Console Close"; break;
        case CTRL_LOGOFF_EVENT:   signalName = "Logoff"; break;
        case CTRL_SHUTDOWN_EVENT: signalName = "Shutdown"; break;
    }
    
    LOG_WARN(std::string(signalName) + " received, initiating graceful shutdown...");
    g_shutdownRequested = true;
    
    // Return TRUE to tell Windows we're handling it
    // Windows will wait up to 5 seconds for cleanup
    return TRUE;
}

// ============================================
// Main Function
// ============================================
int main() {
    // Register console control handler for graceful shutdown
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    
    // Initialize logging framework
    Logger::instance().setLevel(LogLevel::LVL_DEBUG);
    Logger::instance().setLogFile("edr-agent.log");
    
    LOG_INFO("========================================");
    LOG_INFO("  EDR Agent v" + g_agentVersion);
    LOG_INFO("  HTTP Mode (WebSocket added for future)");
    LOG_INFO("========================================");
    
    try {
        // Step 1: Read Configuration
        LOG_INFO("[1/4] Reading configuration file...");
        ConfigReader configReader("config.json");
        
        // Check available modes
        bool hasHttp = configReader.hasHttpConfig();
        bool hasWebSocket = configReader.hasWebSocketConfig();
        
        LOG_INFO("Configuration detected:");
        LOG_INFO("  HTTP: " + std::string(hasHttp ? "Available" : "Not configured"));
        LOG_INFO("  WebSocket: " + std::string(hasWebSocket ? "Available" : "Not configured"));
        
        if (!hasHttp) {
            LOG_FATAL("HTTP configuration not found! Add http_server, http_port, api_path, auth_token to config.json");
        }
        
        // Step 1.5: Initialize Unique Agent ID
        LOG_INFO("[1.5/4] Initializing Agent ID...");
        std::filesystem::path configDir = std::filesystem::path("config.json").parent_path();
        if (configDir.empty()) {
            configDir = std::filesystem::current_path();
        }
        g_agentId = AgentId::getOrCreate(configDir);
        LOG_INFO("Agent ID: " + g_agentId);
        
        // Step 2: Initialize HTTP Client
        LOG_INFO("[2/4] Initializing HTTP client...");
        std::string httpServer = configReader.getHttpServer();
        int httpPort = configReader.getHttpPort();
        std::string apiPath = configReader.getApiPath();
        std::string authToken = configReader.getAuthToken();
        
        if (authToken.empty()) {
            LOG_WARN("No authentication token configured!");
        }
        
        HttpClient httpClient(httpServer, httpPort, apiPath, authToken);
        g_httpClient = &httpClient;
        
        LOG_INFO("HTTP client initialized");
        LOG_INFO("Target: " + httpServer + ":" + std::to_string(httpPort) + apiPath);
        
        // Step 2.5: Start Command Polling (unless disabled for WebSocket-only mode)
        bool disablePolling = configReader.isHttpPollingDisabled();
        if (!disablePolling) {
            LOG_INFO("[2.5/4] Starting Command Polling Service...");
            CommandProcessor::startCommandPolling();
        } else {
            LOG_INFO("[2.5/4] HTTP Command Polling DISABLED (WebSocket-only mode)");
            LOG_WARN("Commands will only be received via WebSocket");
        }

        // Step 3: WebSocket (Real-time Commands)
#ifdef ENABLE_WEBSOCKET
        if (hasWebSocket) {
            LOG_INFO("[3/4] Initializing WebSocket client...");
            std::string wsUri = configReader.getServerUri();
            
            // Create WebSocket client on heap so it persists
            static WebSocketClient webSocketClient;
            g_webSocketClient = &webSocketClient;
            webSocketClient.connect(wsUri);
            
            LOG_INFO("WebSocket connecting to: " + wsUri);
            LOG_INFO("Commands will be received in real-time");
            
            // Give time for connection to establish
            Sleep(2000);
        }
#else
        if (hasWebSocket) {
            LOG_WARN("WebSocket configuration found but not compiled. Rebuild with -DENABLE_WEBSOCKET=ON");
        }
#endif
        
        // Step 4: Subscribe to Windows Event Logs
        LOG_INFO("[3/4] Subscribing to Windows Event Logs...");
        std::vector<std::pair<std::wstring, std::wstring>> pathQueryPairs = configReader.getPathQueryPairs();
        
        if (pathQueryPairs.empty()) {
            LOG_FATAL("No event sources configured!");
            return 1;
        }
        
        DWORD status = ERROR_SUCCESS;
        std::vector<EVT_HANDLE> subscriptions;

        for (const auto& pair : pathQueryPairs) {
            std::wstring pwsPath = pair.first;
            std::wstring pwsQuery = pair.second;
            LOG_INFO("Subscribing to: " + std::string(pwsPath.begin(), pwsPath.end()));

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
                    LOG_WARN("Channel not found: " + std::string(pwsPath.begin(), pwsPath.end()));
                } else if (ERROR_EVT_INVALID_QUERY == status) {
                    LOG_WARN("Invalid query");
                } else {
                    LOG_ERROR("Subscribe failed with error: " + std::to_string(status));
                }
                
                continue; // Try next subscription
            }
            
            subscriptions.push_back(hSubscription);
            LOG_INFO("Subscribed successfully");
        }
        
        if (subscriptions.empty()) {
            LOG_FATAL("No successful subscriptions! Make sure Sysmon is installed.");
            return 1;
        }

        // Step 5: Monitor Events
        LOG_INFO("[4/4] Agent is now monitoring events");
        LOG_INFO("Active mode: HTTP | Target: " + httpServer + ":" + std::to_string(httpPort));
        LOG_INFO("Monitoring " + std::to_string(subscriptions.size()) + " event source(s)");
        LOG_INFO("Press any key or Ctrl+C to stop...");

        // Main event loop - exits on keyboard press OR shutdown signal
        while (!g_shutdownRequested && !_kbhit()) {
            Sleep(100); // Sleep 100ms
        }

        // ==========================================
        // Graceful Shutdown
        // ==========================================
        LOG_INFO("[Shutdown] Stopping services...");

        // Stop command polling
        LOG_INFO("Stopping command polling...");
        CommandProcessor::stopCommandPolling();

        // Close event subscriptions
        LOG_INFO("Closing event subscriptions...");
        for (auto hSub : subscriptions) {
            if (hSub) {
                EvtClose(hSub);
            }
        }
        
#ifdef ENABLE_WEBSOCKET
        // Close WebSocket with proper CLOSE frame
        if (g_webSocketClient != nullptr) {
            LOG_INFO("Closing WebSocket connection...");
            g_webSocketClient->close();
        }
#endif

        // Close HTTP client
        if (g_httpClient != nullptr) {
            LOG_INFO("Closing HTTP client...");
            delete g_httpClient;
            g_httpClient = nullptr;
        }
        
        LOG_INFO("Agent shutdown complete");
        return 0;
        
    } catch (const std::exception& e) {
        LOG_FATAL(std::string("FATAL ERROR: ") + e.what());
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
                LOG_WARN("Event records are missing");
            } else {
                LOG_ERROR("Subscription error: " + std::to_string((uintptr_t)hEvent));
            }
            break;

        case EvtSubscribeActionDeliver:
            status = ProcessEvent(hEvent);
            if (ERROR_SUCCESS != status) {
                LOG_ERROR("Failed to process event");
            }
            break;

        default:
            LOG_WARN("Unknown subscription action");
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
            LOG_ERROR("Failed to convert event to XML (Error: " + std::to_string(status) + ")");
            goto cleanup;
        }

        // Step 2: Sanitize XML
        eventXml = sanitizeUtf8(eventXml);
        
        // Step 3: Convert XML to Sysmon JSON
        eventJson = EventXmlToEventJson(eventXml);
        if (eventJson.empty()) {
            LOG_WARN("Event JSON conversion returned empty");
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
                    LOG_WARN("Django format conversion returned empty");
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
                
                // DEBUG: Log what agent_version is in the buffered event
                LOG_INFO("[BUFFER DEBUG] agent_version in event: " + djangoEvent.value("agent_version", std::string("NOT FOUND")));
                
                LOG_DEBUG("[Buffer] Added event. Size: " + std::to_string(eventBuffer.size()) + "/" + std::to_string(BATCH_SIZE));
                
                if (eventBuffer.size() >= BATCH_SIZE) {
                    LOG_INFO("[Batch] Sending " + std::to_string(eventBuffer.size()) + " events...");
                    
                    auto result = g_httpClient->sendTelemetryBatch(eventBuffer);
                    if (result.isSuccess()) {
                        LOG_INFO("Batch sent successfully");
                        eventBuffer.clear();
                    } else {
                        // Use error codes for specific error handling
                        LOG_ERROR("Failed to send batch: " + result.errorDescription() + " (code: " + std::to_string(result.code()) + ")");
                        eventBuffer.clear(); 
                    }
                }
                // If buffer isn't full yet, we do nothing and wait for the next event.
                // ==================================================================================
                
                LOG_DEBUG("---");
                
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
