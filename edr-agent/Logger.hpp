#pragma once
// ============================================
// Logger.hpp - Thread-safe logging framework
// ============================================

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <iostream>

// ============================================
// Log Levels (prefixed to avoid Windows macro conflicts)
// ============================================
enum class LogLevel {
    LVL_TRACE = 0,  // Very detailed - function entry/exit
    LVL_DEBUG = 1,  // Debug info - variable values
    LVL_INFO  = 2,  // Normal operations
    LVL_WARN  = 3,  // Warning - recoverable issues
    LVL_ERROR = 4,  // Error - operation failed
    LVL_FATAL = 5   // Fatal - cannot continue
};

// ============================================
// Logger Class (Singleton)
// ============================================
class Logger {
public:
    // Get singleton instance
    static Logger& instance();
    
    // Configuration
    void setLevel(LogLevel level);
    void setLogFile(const std::string& path);
    void enableConsole(bool enable);
    
    // Log rotation configuration
    void setMaxLogSize(size_t maxBytes);    // Max size before rotation (default 10MB)
    void setMaxLogFiles(int maxFiles);      // Max rotated files to keep (default 5)
    
    // Core logging method
    void log(LogLevel level, const std::string& message,
             const char* file = nullptr, int line = 0);
    
    // Prevent copying
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger();
    
    std::mutex m_mutex;              // Thread safety
    LogLevel m_level;                // Current log level
    std::ofstream m_logFile;         // File output stream
    std::string m_logPath;           // Path to current log file
    bool m_consoleEnabled;           // Console output flag
    bool m_fileEnabled;              // File output flag
    
    // Log rotation settings
    size_t m_maxLogSize;             // Max log size in bytes (default 10MB)
    int m_maxLogFiles;               // Max rotated files to keep
    size_t m_currentLogSize;         // Current log file size
    
    // Helper methods
    std::string levelToString(LogLevel level);
    std::string getTimestamp();
    std::string formatFilename(const char* path);
    
    // Log rotation
    void checkRotation();
    void rotateLog();
};

// ============================================
// Convenience Macros
// ============================================
#define LOG_TRACE(msg) Logger::instance().log(LogLevel::LVL_TRACE, msg, __FILE__, __LINE__)
#define LOG_DEBUG(msg) Logger::instance().log(LogLevel::LVL_DEBUG, msg, __FILE__, __LINE__)
#define LOG_INFO(msg)  Logger::instance().log(LogLevel::LVL_INFO, msg, __FILE__, __LINE__)
#define LOG_WARN(msg)  Logger::instance().log(LogLevel::LVL_WARN, msg, __FILE__, __LINE__)
#define LOG_ERROR(msg) Logger::instance().log(LogLevel::LVL_ERROR, msg, __FILE__, __LINE__)
#define LOG_FATAL(msg) Logger::instance().log(LogLevel::LVL_FATAL, msg, __FILE__, __LINE__)
