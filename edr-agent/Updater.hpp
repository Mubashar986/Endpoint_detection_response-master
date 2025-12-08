// ============================================================================
// Updater.hpp - Agent Self-Update System
// ============================================================================
// Provides automatic agent updates:
// - Version check triggered by heartbeat response
// - Secure download over HTTPS
// - SHA-256 checksum verification
// - Atomic file replacement
// - Service restart
// ============================================================================
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>

// Forward declare Windows types to avoid including Windows.h in header
// BYTE is unsigned char in Windows
using ByteVector = std::vector<unsigned char>;

// ============================================================================
// UpdateInfo Structure
// ============================================================================
// Contains information about an available update
struct UpdateInfo {
    std::string version;        // New version (e.g., "1.1.0")
    std::string downloadUrl;    // URL to download binary
    std::string checksum;       // SHA-256 of new binary
    std::string releaseNotes;   // Optional release notes
    
    bool isValid() const {
        return !version.empty() && !downloadUrl.empty();
    }
};

// ============================================================================
// UpdateResult Enum
// ============================================================================
enum class UpdateResult {
    Success,
    NoUpdateAvailable,
    DownloadFailed,
    ChecksumMismatch,
    WriteFailed,
    ReplaceFailed,
    RestartFailed
};

// Conversion to string
inline std::string updateResultToString(UpdateResult result) {
    switch (result) {
        case UpdateResult::Success: return "Success";
        case UpdateResult::NoUpdateAvailable: return "No update available";
        case UpdateResult::DownloadFailed: return "Download failed";
        case UpdateResult::ChecksumMismatch: return "Checksum mismatch";
        case UpdateResult::WriteFailed: return "Failed to write file";
        case UpdateResult::ReplaceFailed: return "Failed to replace binary";
        case UpdateResult::RestartFailed: return "Failed to restart service";
        default: return "Unknown error";
    }
}

// ============================================================================
// Updater Class
// ============================================================================
class Updater {
public:
    // Get singleton instance
    static Updater& instance();
    
    // Prevent copying
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;
    
    // ========================================================================
    // Update Operations
    // ========================================================================
    
    // Check if update is available (called when heartbeat returns update info)
    // Does NOT automatically download
    void setUpdateAvailable(const UpdateInfo& info);
    
    // Check if an update is pending
    bool isUpdatePending() const;
    
    // Get pending update info
    UpdateInfo getPendingUpdate() const;
    
    // Perform the update (download, verify, replace, restart)
    // This should be called when it's safe to restart (e.g., after flushing events)
    UpdateResult performUpdate();
    
    // Cancel pending update
    void cancelUpdate();
    
    // ========================================================================
    // Progress Callback
    // ========================================================================
    
    // Callback for download progress (bytes downloaded, total bytes)
    using ProgressCallback = std::function<void(size_t, size_t)>;
    void setProgressCallback(ProgressCallback callback);
    
private:
    Updater();
    ~Updater();
    
    // Download binary from URL
    bool downloadBinary(const std::string& url, ByteVector& data);
    
    // Calculate SHA-256 of data
    std::string calculateSha256(const ByteVector& data);
    
    // Write data to temp file
    bool writeToTempFile(const ByteVector& data, std::wstring& tempPath);
    
    // Atomic replace: rename current to .bak, rename new to current
    bool atomicReplace(const std::wstring& newPath, const std::wstring& currentPath);
    
    // Restart the service
    bool restartService();
    
    // State
    UpdateInfo m_pendingUpdate;
    bool m_updatePending;
    ProgressCallback m_progressCallback;
};
