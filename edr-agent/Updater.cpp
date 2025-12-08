// ============================================================================
// Updater.cpp - Agent Self-Update System Implementation
// ============================================================================

#include "Updater.hpp"
#include "Logger.hpp"
#include "ServiceManager.hpp"
#include <Windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>  // for std::transform
#include <cctype>     // for ::tolower

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

// ============================================================================
// Singleton
// ============================================================================
Updater& Updater::instance() {
    static Updater instance;
    return instance;
}

Updater::Updater()
    : m_updatePending(false) {
}

Updater::~Updater() {
}

// ============================================================================
// Update State Management
// ============================================================================
void Updater::setUpdateAvailable(const UpdateInfo& info) {
    if (info.isValid()) {
        m_pendingUpdate = info;
        m_updatePending = true;
        LOG_INFO("[Updater] Update available: v" + info.version);
    }
}

bool Updater::isUpdatePending() const {
    return m_updatePending;
}

UpdateInfo Updater::getPendingUpdate() const {
    return m_pendingUpdate;
}

void Updater::cancelUpdate() {
    m_updatePending = false;
    m_pendingUpdate = UpdateInfo{};
    LOG_INFO("[Updater] Update cancelled");
}

void Updater::setProgressCallback(ProgressCallback callback) {
    m_progressCallback = callback;
}

// ============================================================================
// Perform Update
// ============================================================================
UpdateResult Updater::performUpdate() {
    if (!m_updatePending || !m_pendingUpdate.isValid()) {
        return UpdateResult::NoUpdateAvailable;
    }
    
    LOG_INFO("[Updater] Starting update to v" + m_pendingUpdate.version);
    
    // Step 1: Download the new binary
    LOG_INFO("[Updater] Downloading...");
    ByteVector binaryData;
    if (!downloadBinary(m_pendingUpdate.downloadUrl, binaryData)) {
        LOG_ERROR("[Updater] Download failed");
        return UpdateResult::DownloadFailed;
    }
    LOG_INFO("[Updater] Downloaded " + std::to_string(binaryData.size()) + " bytes");
    
    // Step 2: Verify checksum (if provided)
    if (!m_pendingUpdate.checksum.empty()) {
        LOG_INFO("[Updater] Verifying checksum...");
        std::string calculatedHash = calculateSha256(binaryData);
        
        // Case-insensitive comparison
        std::string expectedHash = m_pendingUpdate.checksum;
        std::transform(calculatedHash.begin(), calculatedHash.end(), calculatedHash.begin(), ::tolower);
        std::transform(expectedHash.begin(), expectedHash.end(), expectedHash.begin(), ::tolower);
        
        if (calculatedHash != expectedHash) {
            LOG_ERROR("[Updater] Checksum mismatch!");
            LOG_ERROR("  Expected: " + expectedHash);
            LOG_ERROR("  Got:      " + calculatedHash);
            return UpdateResult::ChecksumMismatch;
        }
        LOG_INFO("[Updater] Checksum verified");
    } else {
        LOG_WARN("[Updater] No checksum provided, skipping verification");
    }
    
    // Step 3: Write to temp file
    LOG_INFO("[Updater] Writing to temp file...");
    std::wstring tempPath;
    if (!writeToTempFile(binaryData, tempPath)) {
        LOG_ERROR("[Updater] Failed to write temp file");
        return UpdateResult::WriteFailed;
    }
    
    // Step 4: Get current executable path
    wchar_t currentPath[MAX_PATH];
    GetModuleFileNameW(NULL, currentPath, MAX_PATH);
    
    // Step 5: Atomic replace
    // Note: This might fail if the process is running
    // For production, consider using a helper process or scheduled task
    LOG_INFO("[Updater] Replacing binary...");
    if (!atomicReplace(tempPath, currentPath)) {
        LOG_ERROR("[Updater] Failed to replace binary");
        return UpdateResult::ReplaceFailed;
    }
    
    // Step 6: Restart service
    LOG_INFO("[Updater] Restarting service...");
    if (!restartService()) {
        LOG_WARN("[Updater] Failed to restart service");
        // Don't return error - binary was replaced successfully
    }
    
    LOG_INFO("[Updater] Update complete!");
    m_updatePending = false;
    return UpdateResult::Success;
}

// ============================================================================
// Download Binary
// ============================================================================
bool Updater::downloadBinary(const std::string& url, ByteVector& data) {
    // Parse URL
    URL_COMPONENTSW urlComp = {};
    urlComp.dwStructSize = sizeof(urlComp);
    
    wchar_t hostName[256] = {};
    wchar_t urlPath[1024] = {};
    urlComp.lpszHostName = hostName;
    urlComp.dwHostNameLength = sizeof(hostName) / sizeof(wchar_t);
    urlComp.lpszUrlPath = urlPath;
    urlComp.dwUrlPathLength = sizeof(urlPath) / sizeof(wchar_t);
    
    std::wstring wUrl(url.begin(), url.end());
    if (!WinHttpCrackUrl(wUrl.c_str(), 0, 0, &urlComp)) {
        LOG_ERROR("[Updater] Failed to parse URL");
        return false;
    }
    
    // Open session
    HINTERNET hSession = WinHttpOpen(
        L"EDR-Agent-Updater/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    if (!hSession) return false;
    
    // Connect
    HINTERNET hConnect = WinHttpConnect(
        hSession,
        hostName,
        urlComp.nPort,
        0
    );
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Open request
    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        urlPath,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Send request
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, 
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Receive response
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }
    
    // Read data
    DWORD bytesAvailable;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        ByteVector buffer(bytesAvailable);
        DWORD bytesRead = 0;
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
            data.insert(data.end(), buffer.begin(), buffer.begin() + bytesRead);
            
            if (m_progressCallback) {
                m_progressCallback(data.size(), 0);  // Total unknown
            }
        }
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return !data.empty();
}

// ============================================================================
// Calculate SHA-256
// ============================================================================
std::string Updater::calculateSha256(const ByteVector& data) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    DWORD hashLength = 0;
    DWORD resultLength = 0;
    std::string result;
    
    // Open algorithm provider
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) {
        return "";
    }
    
    // Get hash length
    if (BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&hashLength, 
                          sizeof(hashLength), &resultLength, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }
    
    // Create hash
    if (BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }
    
    // Hash data
    if (BCryptHashData(hHash, (PBYTE)data.data(), (ULONG)data.size(), 0) != 0) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }
    
    // Finish hash
    ByteVector hash(hashLength);
    if (BCryptFinishHash(hHash, hash.data(), hashLength, 0) != 0) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        return "";
    }
    
    // Convert to hex string
    std::ostringstream oss;
    for (BYTE b : hash) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)b;
    }
    result = oss.str();
    
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    
    return result;
}

// ============================================================================
// Write to Temp File
// ============================================================================
bool Updater::writeToTempFile(const ByteVector& data, std::wstring& tempPath) {
    // Get temp directory
    wchar_t tempDir[MAX_PATH];
    GetTempPathW(MAX_PATH, tempDir);
    
    // Generate temp filename
    wchar_t tempFile[MAX_PATH];
    GetTempFileNameW(tempDir, L"EDR", 0, tempFile);
    
    // Add .exe extension
    tempPath = tempFile;
    tempPath += L".exe";
    
    // Write file
    std::ofstream file(tempPath, std::ios::binary);
    if (!file) return false;
    
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    file.close();
    
    return file.good();
}

// ============================================================================
// Atomic Replace
// ============================================================================
bool Updater::atomicReplace(const std::wstring& newPath, const std::wstring& currentPath) {
    // Create backup path
    std::wstring backupPath = currentPath + L".bak";
    
    // Delete old backup if exists
    DeleteFileW(backupPath.c_str());
    
    // Rename current to backup
    if (!MoveFileW(currentPath.c_str(), backupPath.c_str())) {
        // If we can't rename, the file might be locked
        // Try MoveFileEx with DELAY_UNTIL_REBOOT
        LOG_WARN("[Updater] Binary is locked, scheduling replacement for reboot");
        if (!MoveFileExW(newPath.c_str(), currentPath.c_str(), 
                         MOVEFILE_DELAY_UNTIL_REBOOT | MOVEFILE_REPLACE_EXISTING)) {
            return false;
        }
        return true;  // Will be replaced on reboot
    }
    
    // Move new to current
    if (!MoveFileW(newPath.c_str(), currentPath.c_str())) {
        // Rollback: restore backup
        MoveFileW(backupPath.c_str(), currentPath.c_str());
        return false;
    }
    
    // Success - optionally delete backup
    // DeleteFileW(backupPath.c_str());  // Keep backup for rollback
    
    return true;
}

// ============================================================================
// Restart Service
// ============================================================================
bool Updater::restartService() {
    // Open SCM
    SC_HANDLE hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (!hSCManager) return false;
    
    // Open service
    SC_HANDLE hService = OpenServiceW(hSCManager, SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (!hService) {
        CloseServiceHandle(hSCManager);
        return false;
    }
    
    // Stop service
    SERVICE_STATUS status;
    ControlService(hService, SERVICE_CONTROL_STOP, &status);
    
    // Wait for stop
    for (int i = 0; i < 30; i++) {
        QueryServiceStatus(hService, &status);
        if (status.dwCurrentState == SERVICE_STOPPED) break;
        Sleep(1000);
    }
    
    // Start service
    BOOL success = StartServiceW(hService, 0, NULL);
    
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    
    return success != FALSE;
}
