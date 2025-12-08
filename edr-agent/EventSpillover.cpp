// ============================================================================
// EventSpillover.cpp - Disk-based Event Persistence Implementation
// ============================================================================

#include "EventSpillover.hpp"
#include "Logger.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <algorithm>

// ============================================================================
// Singleton
// ============================================================================
EventSpillover& EventSpillover::instance() {
    static EventSpillover instance;
    return instance;
}

EventSpillover::EventSpillover() {
    // Default config will be set
}

EventSpillover::~EventSpillover() {
}

// ============================================================================
// Configuration
// ============================================================================
void EventSpillover::configure(const SpilloverConfig& config) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_config = config;
    
    // Create spill directory if it doesn't exist
    try {
        if (!std::filesystem::exists(m_config.spillDir)) {
            std::filesystem::create_directories(m_config.spillDir);
            LOG_INFO("[Spillover] Created spill directory: " + m_config.spillDir.string());
        }
    } catch (const std::exception& e) {
        LOG_ERROR("[Spillover] Failed to create spill directory: " + std::string(e.what()));
    }
    
    // Count existing spilled events
    m_spilledCount = 0;
    for (const auto& file : getSpillFiles()) {
        try {
            std::ifstream ifs(file);
            nlohmann::json j = nlohmann::json::parse(ifs);
            if (j.is_array()) {
                m_spilledCount += j.size();
            }
        } catch (...) {
            // Ignore malformed files
        }
    }
    
    if (m_spilledCount > 0) {
        LOG_INFO("[Spillover] Found " + std::to_string(m_spilledCount) + " spilled events to recover");
    }
}

// ============================================================================
// Spill Events to Disk
// ============================================================================
bool EventSpillover::spillEvents(const std::vector<nlohmann::json>& events) {
    if (events.empty()) return true;
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    try {
        // Ensure directory exists
        if (!std::filesystem::exists(m_config.spillDir)) {
            std::filesystem::create_directories(m_config.spillDir);
        }
        
        // Generate filename
        std::string filename = generateSpillFilename();
        std::filesystem::path spillPath = m_config.spillDir / filename;
        std::filesystem::path tempPath = spillPath;
        tempPath += ".tmp";
        
        // Write to temp file first (atomic write pattern)
        {
            std::ofstream ofs(tempPath);
            if (!ofs.is_open()) {
                LOG_ERROR("[Spillover] Failed to open temp file: " + tempPath.string());
                return false;
            }
            
            nlohmann::json j = events;
            ofs << j.dump();
            ofs.close();
        }
        
        // Rename temp to final (atomic on most filesystems)
        std::filesystem::rename(tempPath, spillPath);
        
        m_spilledCount += events.size();
        LOG_INFO("[Spillover] Saved " + std::to_string(events.size()) + 
                 " events to disk. Total spilled: " + std::to_string(m_spilledCount.load()));
        
        // Enforce quota
        enforceQuota();
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("[Spillover] Failed to spill events: " + std::string(e.what()));
        return false;
    }
}

// ============================================================================
// Check for Spilled Events
// ============================================================================
bool EventSpillover::hasSpilledEvents() const {
    return m_spilledCount > 0;
}

size_t EventSpillover::getSpilledEventCount() const {
    return m_spilledCount;
}

size_t EventSpillover::getDiskUsage() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    size_t totalBytes = 0;
    try {
        for (const auto& file : getSpillFiles()) {
            totalBytes += std::filesystem::file_size(file);
        }
    } catch (...) {
        // Ignore errors
    }
    return totalBytes;
}

// ============================================================================
// Recover Events from Disk
// ============================================================================
std::vector<nlohmann::json> EventSpillover::recoverEvents(size_t limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    std::vector<nlohmann::json> events;
    auto spillFiles = getSpillFiles();
    
    if (spillFiles.empty()) {
        return events;
    }
    
    // Read from oldest file first
    std::filesystem::path oldestFile = spillFiles.front();
    
    try {
        std::ifstream ifs(oldestFile);
        nlohmann::json j = nlohmann::json::parse(ifs);
        
        if (j.is_array()) {
            for (const auto& event : j) {
                events.push_back(event);
                if (limit > 0 && events.size() >= limit) {
                    break;
                }
            }
        }
        
        m_currentRecoveryFile = oldestFile;
        m_recoveredFromCurrentFile = events.size();
        
        LOG_DEBUG("[Spillover] Recovered " + std::to_string(events.size()) + 
                  " events from: " + oldestFile.filename().string());
                  
    } catch (const std::exception& e) {
        LOG_ERROR("[Spillover] Failed to read spill file: " + std::string(e.what()));
        // Delete corrupted file
        try {
            std::filesystem::remove(oldestFile);
        } catch (...) {}
    }
    
    return events;
}

// ============================================================================
// Confirm Events Sent
// ============================================================================
void EventSpillover::confirmEventsSent(size_t count) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_currentRecoveryFile.empty()) return;
    
    try {
        // Read the file
        std::ifstream ifs(m_currentRecoveryFile);
        nlohmann::json j = nlohmann::json::parse(ifs);
        ifs.close();
        
        if (j.is_array() && j.size() <= count) {
            // All events in file were sent, delete the file
            std::filesystem::remove(m_currentRecoveryFile);
            size_t removed = j.size();
            m_spilledCount = (m_spilledCount > removed) ? (m_spilledCount - removed) : 0;
            LOG_INFO("[Spillover] Deleted completed spill file. Remaining: " + 
                     std::to_string(m_spilledCount.load()));
        } else if (j.is_array()) {
            // Partial send, rewrite file with remaining events
            nlohmann::json remaining = nlohmann::json::array();
            for (size_t i = count; i < j.size(); i++) {
                remaining.push_back(j[i]);
            }
            
            std::ofstream ofs(m_currentRecoveryFile);
            ofs << remaining.dump();
            ofs.close();
            
            m_spilledCount = (m_spilledCount > count) ? (m_spilledCount - count) : 0;
            LOG_DEBUG("[Spillover] Updated spill file. Remaining in file: " + 
                      std::to_string(remaining.size()));
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("[Spillover] Failed to confirm events: " + std::string(e.what()));
    }
    
    m_currentRecoveryFile.clear();
    m_recoveredFromCurrentFile = 0;
}

// ============================================================================
// Enforce Disk Quota
// ============================================================================
void EventSpillover::enforceQuota() {
    // Must be called with lock held
    
    size_t maxBytes = m_config.maxDiskMB * 1024 * 1024;
    size_t currentBytes = 0;
    
    auto spillFiles = getSpillFiles();
    
    // Calculate current usage
    for (const auto& file : spillFiles) {
        try {
            currentBytes += std::filesystem::file_size(file);
        } catch (...) {}
    }
    
    // Delete oldest files if over quota
    for (const auto& file : spillFiles) {
        if (currentBytes <= maxBytes) break;
        
        try {
            size_t fileSize = std::filesystem::file_size(file);
            
            // Count events in file before deleting
            std::ifstream ifs(file);
            nlohmann::json j = nlohmann::json::parse(ifs);
            ifs.close();
            
            if (j.is_array()) {
                m_spilledCount = (m_spilledCount > j.size()) ? (m_spilledCount - j.size()) : 0;
            }
            
            std::filesystem::remove(file);
            currentBytes -= fileSize;
            
            LOG_WARN("[Spillover] Deleted oldest spill file due to quota: " + file.filename().string());
            
        } catch (...) {
            // Try to delete anyway
            try { std::filesystem::remove(file); } catch (...) {}
        }
    }
}

// ============================================================================
// Clear All
// ============================================================================
void EventSpillover::clearAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    for (const auto& file : getSpillFiles()) {
        try {
            std::filesystem::remove(file);
        } catch (...) {}
    }
    
    m_spilledCount = 0;
    LOG_INFO("[Spillover] Cleared all spilled events");
}

// ============================================================================
// Generate Spill Filename
// ============================================================================
std::string EventSpillover::generateSpillFilename() const {
    // Format: spill_YYYYMMDD_HHMMSS_NNNNN.json
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 100000;
    
    std::ostringstream oss;
    oss << "spill_";
    oss << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S");
    oss << "_" << std::setfill('0') << std::setw(5) << ms.count();
    oss << ".json";
    
    return oss.str();
}

// ============================================================================
// Get Spill Files (sorted oldest first)
// ============================================================================
std::vector<std::filesystem::path> EventSpillover::getSpillFiles() const {
    std::vector<std::filesystem::path> files;
    
    try {
        if (!std::filesystem::exists(m_config.spillDir)) {
            return files;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(m_config.spillDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                files.push_back(entry.path());
            }
        }
        
        // Sort by filename (which includes timestamp, so oldest first)
        std::sort(files.begin(), files.end());
        
    } catch (...) {
        // Ignore errors
    }
    
    return files;
}
