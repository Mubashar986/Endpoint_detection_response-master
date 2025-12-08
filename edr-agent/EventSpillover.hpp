// ============================================================================
// EventSpillover.hpp - Disk-based Event Persistence
// ============================================================================
// When the server is unavailable, events are saved to disk to prevent data loss.
// On reconnection, events are read back and sent to the server.
//
// Features:
// - Atomic file writes (write to temp, then rename)
// - FIFO ordering (oldest events sent first)
// - Automatic cleanup of sent events
// - Configurable max disk usage
// ============================================================================
#pragma once

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <filesystem>
#include "nlohmann/json.hpp"

// ============================================================================
// SpilloverConfig
// ============================================================================
struct SpilloverConfig {
    std::filesystem::path spillDir;    // Directory for spill files
    size_t maxDiskMB;                  // Max disk usage in MB (default 100MB)
    size_t maxEventsPerFile;           // Events per spill file (default 1000)
    
    SpilloverConfig()
        : spillDir("C:\\ProgramData\\EDRAgent\\spill")
        , maxDiskMB(100)
        , maxEventsPerFile(1000) {}
};

// ============================================================================
// EventSpillover Class
// ============================================================================
class EventSpillover {
public:
    // Get singleton instance
    static EventSpillover& instance();
    
    // Prevent copying
    EventSpillover(const EventSpillover&) = delete;
    EventSpillover& operator=(const EventSpillover&) = delete;
    
    // ========================================================================
    // Configuration
    // ========================================================================
    void configure(const SpilloverConfig& config);
    
    // ========================================================================
    // Spill Operations
    // ========================================================================
    
    // Save events to disk when server is unavailable
    // Returns true if successfully saved
    bool spillEvents(const std::vector<nlohmann::json>& events);
    
    // Check if there are spilled events waiting
    bool hasSpilledEvents() const;
    
    // Get count of spilled events
    size_t getSpilledEventCount() const;
    
    // Get total disk usage in bytes
    size_t getDiskUsage() const;
    
    // ========================================================================
    // Recovery Operations
    // ========================================================================
    
    // Get next batch of spilled events to send
    // limit: max events to return (0 = all available in oldest file)
    std::vector<nlohmann::json> recoverEvents(size_t limit = 0);
    
    // Mark events as successfully sent (removes from disk)
    void confirmEventsSent(size_t count);
    
    // ========================================================================
    // Cleanup
    // ========================================================================
    
    // Delete oldest spill files to stay under disk limit
    void enforceQuota();
    
    // Clear all spilled events (use with caution!)
    void clearAll();

private:
    EventSpillover();
    ~EventSpillover();
    
    // Generate unique filename for spill file
    std::string generateSpillFilename() const;
    
    // Get sorted list of spill files (oldest first)
    std::vector<std::filesystem::path> getSpillFiles() const;
    
    // State
    SpilloverConfig m_config;
    mutable std::mutex m_mutex;
    std::atomic<size_t> m_spilledCount{0};
    
    // Track current recovery file
    std::filesystem::path m_currentRecoveryFile;
    size_t m_recoveredFromCurrentFile{0};
};

// ============================================================================
// Convenience Macro
// ============================================================================
#define SPILL EventSpillover::instance()
