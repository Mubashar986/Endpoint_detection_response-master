#include "Logger.hpp"
#include <Windows.h>  // For console colors

// ============================================
// Singleton Instance
// ============================================
Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

// ============================================
// Constructor
// ============================================
Logger::Logger() 
    : m_level(LogLevel::LVL_INFO)
    , m_consoleEnabled(true)
    , m_fileEnabled(false) {
}

// ============================================
// Destructor
// ============================================
Logger::~Logger() {
    if (m_logFile.is_open()) {
        m_logFile.close();
    }
}

// ============================================
// Set Log Level
// ============================================
void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_level = level;
}

// ============================================
// Set Log File
// ============================================
void Logger::setLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_logFile.is_open()) {
        m_logFile.close();
    }
    
    m_logFile.open(path, std::ios::app);
    if (m_logFile.is_open()) {
        m_fileEnabled = true;
        // Write header
        m_logFile << "\n========================================\n";
        m_logFile << "Log session started: " << getTimestamp() << "\n";
        m_logFile << "========================================\n";
    }
}

// ============================================
// Enable/Disable Console
// ============================================
void Logger::enableConsole(bool enable) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_consoleEnabled = enable;
}

// ============================================
// Core Logging Method
// ============================================
void Logger::log(LogLevel level, const std::string& message,
                 const char* file, int line) {
    // Skip if below current level
    if (level < m_level) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    // Build log line
    std::ostringstream oss;
    oss << "[" << getTimestamp() << "] ";
    oss << "[" << std::setw(5) << levelToString(level) << "] ";
    
    if (file != nullptr) {
        oss << "[" << formatFilename(file) << ":" << line << "] ";
    }
    
    oss << message;
    
    std::string logLine = oss.str();
    
    // Output to console with colors
    if (m_consoleEnabled) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        WORD color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;  // White default
        
        switch (level) {
            case LogLevel::LVL_TRACE: 
                color = FOREGROUND_INTENSITY;  // Gray
                break;
            case LogLevel::LVL_DEBUG: 
                color = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;  // Cyan
                break;
            case LogLevel::LVL_INFO:  
                color = FOREGROUND_GREEN | FOREGROUND_INTENSITY;  // Bright Green
                break;
            case LogLevel::LVL_WARN:  
                color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;  // Yellow
                break;
            case LogLevel::LVL_ERROR: 
                color = FOREGROUND_RED | FOREGROUND_INTENSITY;  // Bright Red
                break;
            case LogLevel::LVL_FATAL: 
                color = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;  // Magenta
                break;
        }
        
        SetConsoleTextAttribute(hConsole, color);
        std::cout << logLine << std::endl;
        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);  // Reset
    }
    
    // Output to file
    if (m_fileEnabled && m_logFile.is_open()) {
        m_logFile << logLine << std::endl;
        m_logFile.flush();  // Ensure immediate write
    }
}

// ============================================
// Helper: Level to String
// ============================================
std::string Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::LVL_TRACE: return "TRACE";
        case LogLevel::LVL_DEBUG: return "DEBUG";
        case LogLevel::LVL_INFO:  return "INFO";
        case LogLevel::LVL_WARN:  return "WARN";
        case LogLevel::LVL_ERROR: return "ERROR";
        case LogLevel::LVL_FATAL: return "FATAL";
        default: return "?????";
    }
}

// ============================================
// Helper: Get Timestamp
// ============================================
std::string Logger::getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// ============================================
// Helper: Format Filename (basename only)
// ============================================
std::string Logger::formatFilename(const char* path) {
    if (path == nullptr) return "";
    
    std::string fullPath(path);
    size_t lastSlash = fullPath.find_last_of("\\/");
    if (lastSlash != std::string::npos) {
        return fullPath.substr(lastSlash + 1);
    }
    return fullPath;
}
