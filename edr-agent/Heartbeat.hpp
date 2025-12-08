// ============================================================================
// Heartbeat.hpp - Agent Health Monitoring System
// ============================================================================
// Sends periodic heartbeat messages to the server with:
// - Agent status (running, degraded, etc.)
// - System metrics (CPU, memory, uptime)
// - Event statistics (sent, queued)
// - Agent version information
//
// Server can respond with:
// - Acknowledgment
// - Update instructions (new version available)
// - Configuration changes
// ============================================================================
#pragma once

#include <string>
#include <atomic>
#include <thread>
#include <functional>
#include <cstdint>
#include "nlohmann/json.hpp"

// ============================================================================
// HeartbeatData Structure
// ============================================================================
// Contains all metrics sent in each heartbeat
struct HeartbeatData {
    // Agent identification
    std::string agentId;
    std::string agentVersion;
    std::string hostname;
    
    // Status
    std::string status;  // "running", "degraded", "updating"
    
    // System metrics
    double cpuPercent;      // 0-100
    uint64_t memoryMB;      // Current process memory usage
    uint64_t uptimeSeconds; // How long agent has been running
    
    // Event statistics
    uint64_t eventsSent;    // Total events sent to server
    uint64_t eventsQueued;  // Events waiting in buffer
    
    // Connection health
    uint32_t wsReconnectFailures;  // WebSocket reconnect failure count
    uint32_t httpFailures;         // HTTP send failures
    
    // Convert to JSON for transmission
    nlohmann::json toJson() const;
};

// ============================================================================
// HeartbeatResponse Structure
// ============================================================================
// Parsed response from server after heartbeat
struct HeartbeatResponse {
    bool success;
    bool updateAvailable;
    std::string latestVersion;
    std::string updateUrl;
    std::string updateChecksum;  // SHA-256
    std::string message;
    
    // Parse from server JSON response
    static HeartbeatResponse fromJson(const nlohmann::json& json);
};

// ============================================================================
// HeartbeatManager Class
// ============================================================================
// Singleton that manages the heartbeat timer and metrics collection
class HeartbeatManager {
public:
    // Get singleton instance
    static HeartbeatManager& instance();
    
    // Prevent copying
    HeartbeatManager(const HeartbeatManager&) = delete;
    HeartbeatManager& operator=(const HeartbeatManager&) = delete;
    
    // ========================================================================
    // Lifecycle
    // ========================================================================
    
    // Start heartbeat timer (default: 30 seconds)
    void start(int intervalSeconds = 30);
    
    // Stop heartbeat timer
    void stop();
    
    // Check if heartbeat is running
    bool isRunning() const;
    
    // ========================================================================
    // Metrics Update
    // ========================================================================
    
    // Increment event counters (called when events are sent/queued)
    void incrementEventsSent(uint64_t count = 1);
    void incrementEventsQueued(uint64_t count = 1);
    void decrementEventsQueued(uint64_t count = 1);
    
    // Record connection failures
    void recordWsReconnectFailure();
    void recordHttpFailure();
    void resetFailureCounters();
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    
    // Set callback for when update is available
    using UpdateCallback = std::function<void(const HeartbeatResponse&)>;
    void setUpdateCallback(UpdateCallback callback);
    
private:
    HeartbeatManager();
    ~HeartbeatManager();
    
    // Heartbeat thread function
    void heartbeatLoop();
    
    // Collect current system metrics
    HeartbeatData collectMetrics();
    
    // Send heartbeat to server
    HeartbeatResponse sendHeartbeat(const HeartbeatData& data);
    
    // System metrics helpers
    double getCurrentCpuUsage();
    uint64_t getCurrentMemoryUsage();
    uint64_t getUptimeSeconds();
    
    // State
    std::atomic<bool> m_running{false};
    std::thread m_thread;
    int m_intervalSeconds;
    
    // Metrics counters
    std::atomic<uint64_t> m_eventsSent{0};
    std::atomic<uint64_t> m_eventsQueued{0};
    std::atomic<uint32_t> m_wsReconnectFailures{0};
    std::atomic<uint32_t> m_httpFailures{0};
    
    // Timestamp tracking
    uint64_t m_startTime;  // GetTickCount64() at start
    
    // CPU calculation state
    uint64_t m_lastCpuTime;
    uint64_t m_lastSysTime;
    
    // Update callback
    UpdateCallback m_updateCallback;
};

// ============================================================================
// Convenience Macros
// ============================================================================
#define HEARTBEAT HeartbeatManager::instance()
