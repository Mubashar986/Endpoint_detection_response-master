// ============================================================================
// Heartbeat.cpp - Agent Health Monitoring System Implementation
// ============================================================================

#include "Heartbeat.hpp"
#include "Logger.hpp"
#include "HttpClient.hpp"
#include "ConfigReader.hpp"
#include <Windows.h>
#include <Psapi.h>
#include <chrono>
#include <sstream>
#include <iomanip>

// External references
extern std::string g_agentId;
extern std::string g_agentVersion;
extern HttpClient* g_httpClient;

#pragma comment(lib, "psapi.lib")

// ============================================================================
// HeartbeatData::toJson
// ============================================================================
nlohmann::json HeartbeatData::toJson() const {
    // Get current timestamp in ISO 8601 format
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_now;
    gmtime_s(&tm_now, &time_t_now);
    
    std::ostringstream oss;
    oss << std::put_time(&tm_now, "%Y-%m-%dT%H:%M:%SZ");
    
    return {
        {"agent_id", agentId},
        {"agent_version", agentVersion},
        {"hostname", hostname},
        {"timestamp", oss.str()},
        {"status", status},
        {"cpu_percent", cpuPercent},
        {"memory_mb", memoryMB},
        {"uptime_seconds", uptimeSeconds},
        {"events_sent", eventsSent},
        {"events_queued", eventsQueued},
        {"ws_reconnect_failures", wsReconnectFailures},
        {"http_failures", httpFailures}
    };
}

// ============================================================================
// HeartbeatResponse::fromJson
// ============================================================================
HeartbeatResponse HeartbeatResponse::fromJson(const nlohmann::json& json) {
    HeartbeatResponse response;
    response.success = true;
    response.updateAvailable = json.value("update_available", false);
    response.latestVersion = json.value("latest_version", "");
    response.updateUrl = json.value("update_url", "");
    response.updateChecksum = json.value("update_checksum", "");
    response.message = json.value("message", "");
    return response;
}

// ============================================================================
// HeartbeatManager Singleton
// ============================================================================
HeartbeatManager& HeartbeatManager::instance() {
    static HeartbeatManager instance;
    return instance;
}

HeartbeatManager::HeartbeatManager()
    : m_intervalSeconds(30)
    , m_startTime(GetTickCount64())
    , m_lastCpuTime(0)
    , m_lastSysTime(0) {
}

HeartbeatManager::~HeartbeatManager() {
    stop();
}

// ============================================================================
// Start/Stop
// ============================================================================
void HeartbeatManager::start(int intervalSeconds) {
    if (m_running) {
        LOG_WARN("[Heartbeat] Already running");
        return;
    }
    
    m_intervalSeconds = intervalSeconds;
    m_running = true;
    m_startTime = GetTickCount64();
    
    m_thread = std::thread(&HeartbeatManager::heartbeatLoop, this);
    
    LOG_INFO("[Heartbeat] Started with " + std::to_string(intervalSeconds) + "s interval");
}

void HeartbeatManager::stop() {
    if (!m_running) return;
    
    LOG_INFO("[Heartbeat] Stopping...");
    m_running = false;
    
    if (m_thread.joinable()) {
        m_thread.join();
    }
    
    LOG_INFO("[Heartbeat] Stopped");
}

bool HeartbeatManager::isRunning() const {
    return m_running;
}

// ============================================================================
// Metrics Update
// ============================================================================
void HeartbeatManager::incrementEventsSent(uint64_t count) {
    m_eventsSent += count;
}

void HeartbeatManager::incrementEventsQueued(uint64_t count) {
    m_eventsQueued += count;
}

void HeartbeatManager::decrementEventsQueued(uint64_t count) {
    // Atomic subtraction with underflow protection
    uint64_t current = m_eventsQueued.load();
    while (current >= count && 
           !m_eventsQueued.compare_exchange_weak(current, current - count)) {
        // Retry if compare_exchange failed
    }
}

void HeartbeatManager::recordWsReconnectFailure() {
    m_wsReconnectFailures++;
}

void HeartbeatManager::recordHttpFailure() {
    m_httpFailures++;
}

void HeartbeatManager::resetFailureCounters() {
    m_wsReconnectFailures = 0;
    m_httpFailures = 0;
}

// ============================================================================
// Callbacks
// ============================================================================
void HeartbeatManager::setUpdateCallback(UpdateCallback callback) {
    m_updateCallback = callback;
}

// ============================================================================
// Heartbeat Loop
// ============================================================================
void HeartbeatManager::heartbeatLoop() {
    LOG_DEBUG("[Heartbeat] Thread started");
    
    // Wait interval between heartbeats using interruptible sleep
    while (m_running) {
        // Collect metrics
        HeartbeatData data = collectMetrics();
        
        // Send heartbeat
        HeartbeatResponse response = sendHeartbeat(data);
        
        // Handle update notification
        if (response.success && response.updateAvailable) {
            LOG_INFO("[Heartbeat] Update available: v" + response.latestVersion);
            if (m_updateCallback) {
                m_updateCallback(response);
            }
        }
        
        // Interruptible sleep - check running flag every second
        for (int i = 0; i < m_intervalSeconds && m_running; i++) {
            Sleep(1000);
        }
    }
    
    LOG_DEBUG("[Heartbeat] Thread exiting");
}

// ============================================================================
// Collect Metrics
// ============================================================================
HeartbeatData HeartbeatManager::collectMetrics() {
    HeartbeatData data;
    
    // Agent identification
    data.agentId = g_agentId;
    data.agentVersion = g_agentVersion;
    
    // Get hostname
    char hostname[256];
    DWORD size = sizeof(hostname);
    if (GetComputerNameA(hostname, &size)) {
        data.hostname = hostname;
    } else {
        data.hostname = "unknown";
    }
    
    // Status (could be enhanced with health checks)
    data.status = "running";
    
    // System metrics
    data.cpuPercent = getCurrentCpuUsage();
    data.memoryMB = getCurrentMemoryUsage();
    data.uptimeSeconds = getUptimeSeconds();
    
    // Event statistics
    data.eventsSent = m_eventsSent.load();
    data.eventsQueued = m_eventsQueued.load();
    
    // Connection health
    data.wsReconnectFailures = m_wsReconnectFailures.load();
    data.httpFailures = m_httpFailures.load();
    
    return data;
}

// ============================================================================
// Send Heartbeat
// ============================================================================
HeartbeatResponse HeartbeatManager::sendHeartbeat(const HeartbeatData& data) {
    HeartbeatResponse response;
    response.success = false;
    
    try {
        // Read config for heartbeat endpoint
        ConfigReader config("config.json");
        std::string server = config.getHttpServer();
        int port = config.getHttpPort();
        std::string token = config.getAuthToken();
        
        // Build full URL - POST method expects full URL with scheme
        std::string fullUrl = "http://" + server + ":" + std::to_string(port) + "/api/v1/heartbeat/";
        
        // Create HTTP client and add auth header
        HttpClient client;
        client.addHeader("Authorization", "Token " + token);
        client.addHeader("Content-Type", "application/json");
        
        // Send heartbeat
        std::string jsonData = data.toJson().dump();
        LOG_DEBUG("[Heartbeat] Sending to: " + fullUrl);
        std::string responseStr = client.POST(fullUrl, jsonData);
        
        if (!responseStr.empty()) {
            try {
                nlohmann::json responseJson = nlohmann::json::parse(responseStr);
                response = HeartbeatResponse::fromJson(responseJson);
                response.success = true;  // Mark as success if we got valid JSON
                LOG_DEBUG("[Heartbeat] Sent successfully");
            } catch (const std::exception& e) {
                LOG_WARN("[Heartbeat] Failed to parse response: " + std::string(e.what()));
            }
        } else {
            LOG_WARN("[Heartbeat] Empty response from server");
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("[Heartbeat] Send failed: " + std::string(e.what()));
        m_httpFailures++;
    }
    
    return response;
}

// ============================================================================
// System Metrics Helpers
// ============================================================================

double HeartbeatManager::getCurrentCpuUsage() {
    // Get process times
    FILETIME createTime, exitTime, kernelTime, userTime;
    if (!GetProcessTimes(GetCurrentProcess(), &createTime, &exitTime, &kernelTime, &userTime)) {
        return 0.0;
    }
    
    // Convert to 64-bit values
    ULONGLONG kernel = (((ULONGLONG)kernelTime.dwHighDateTime) << 32) | kernelTime.dwLowDateTime;
    ULONGLONG user = (((ULONGLONG)userTime.dwHighDateTime) << 32) | userTime.dwLowDateTime;
    ULONGLONG cpuTime = kernel + user;
    
    // Get current system time
    FILETIME sysTime;
    GetSystemTimeAsFileTime(&sysTime);
    ULONGLONG sysTimeNow = (((ULONGLONG)sysTime.dwHighDateTime) << 32) | sysTime.dwLowDateTime;
    
    // Calculate CPU percentage
    if (m_lastCpuTime == 0) {
        // First call - just store values
        m_lastCpuTime = cpuTime;
        m_lastSysTime = sysTimeNow;
        return 0.0;
    }
    
    ULONGLONG cpuDelta = cpuTime - m_lastCpuTime;
    ULONGLONG sysDelta = sysTimeNow - m_lastSysTime;
    
    m_lastCpuTime = cpuTime;
    m_lastSysTime = sysTimeNow;
    
    if (sysDelta == 0) return 0.0;
    
    // Get number of processors for accurate percentage
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    
    double cpuPercent = (100.0 * cpuDelta) / (sysDelta * sysInfo.dwNumberOfProcessors);
    return cpuPercent;
}

uint64_t HeartbeatManager::getCurrentMemoryUsage() {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        // Return working set in MB
        return pmc.WorkingSetSize / (1024 * 1024);
    }
    return 0;
}

uint64_t HeartbeatManager::getUptimeSeconds() {
    ULONGLONG now = GetTickCount64();
    return (now - m_startTime) / 1000;
}
