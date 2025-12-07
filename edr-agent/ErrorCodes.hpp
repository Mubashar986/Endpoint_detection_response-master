// ============================================
// ErrorCodes.hpp - Standardized Error Handling
// ============================================
// Provides AgentError enum and Result<T> template
// for consistent error handling across the agent.
// ============================================
#pragma once

#include <string>

// ============================================
// AgentError Enum
// ============================================
// Categorized error codes for the agent.
// Ranges:
//   0       = Success
//   100-199 = Config errors
//   200-299 = Network errors
//   300-399 = Event errors
//   400-499 = Command errors
//   500-599 = System errors
// ============================================
enum class AgentError {
    // Success
    Success = 0,
    
    // Config errors (100-199)
    ConfigNotFound = 100,
    ConfigParseError = 101,
    ConfigMissingField = 102,
    ConfigInvalidValue = 103,
    ConfigAuthMissing = 104,
    
    // Network errors (200-299)
    NetworkTimeout = 200,
    NetworkConnectionFailed = 201,
    NetworkAuthFailed = 202,
    NetworkSendFailed = 203,
    NetworkReceiveFailed = 204,
    NetworkDnsError = 205,
    
    // Event errors (300-399)
    EventParseError = 300,
    EventConversionFailed = 301,
    EventXmlError = 302,
    EventJsonError = 303,
    EventSubscriptionFailed = 304,
    
    // Command errors (400-499)
    CommandUnknown = 400,
    CommandNotAllowed = 401,
    CommandFailed = 402,
    CommandTimeout = 403,
    CommandInvalidParams = 404,
    
    // System errors (500-599)
    SystemResourceError = 500,
    SystemPermissionDenied = 501,
    SystemOutOfMemory = 502,
    SystemUnexpected = 503
};

// ============================================
// Error Code Utilities
// ============================================
// Convert AgentError to human-readable string
inline std::string errorToString(AgentError error) {
    switch (error) {
        case AgentError::Success: return "Success";
        
        // Config
        case AgentError::ConfigNotFound: return "Config file not found";
        case AgentError::ConfigParseError: return "Config parse error";
        case AgentError::ConfigMissingField: return "Required config field missing";
        case AgentError::ConfigInvalidValue: return "Invalid config value";
        case AgentError::ConfigAuthMissing: return "Auth token not configured";
        
        // Network
        case AgentError::NetworkTimeout: return "Network timeout";
        case AgentError::NetworkConnectionFailed: return "Connection failed";
        case AgentError::NetworkAuthFailed: return "Authentication failed";
        case AgentError::NetworkSendFailed: return "Failed to send data";
        case AgentError::NetworkReceiveFailed: return "Failed to receive data";
        case AgentError::NetworkDnsError: return "DNS resolution failed";
        
        // Event
        case AgentError::EventParseError: return "Event parse error";
        case AgentError::EventConversionFailed: return "Event conversion failed";
        case AgentError::EventXmlError: return "Event XML error";
        case AgentError::EventJsonError: return "Event JSON error";
        case AgentError::EventSubscriptionFailed: return "Event subscription failed";
        
        // Command
        case AgentError::CommandUnknown: return "Unknown command";
        case AgentError::CommandNotAllowed: return "Command not allowed";
        case AgentError::CommandFailed: return "Command execution failed";
        case AgentError::CommandTimeout: return "Command timed out";
        case AgentError::CommandInvalidParams: return "Invalid command parameters";
        
        // System
        case AgentError::SystemResourceError: return "System resource error";
        case AgentError::SystemPermissionDenied: return "Permission denied";
        case AgentError::SystemOutOfMemory: return "Out of memory";
        case AgentError::SystemUnexpected: return "Unexpected error";
        
        default: return "Unknown error";
    }
}

// Get error code as integer
inline int errorCode(AgentError error) {
    return static_cast<int>(error);
}

// ============================================
// Result<T> Template
// ============================================
// A result type that contains either a value
// or an error with a message.
// Usage:
//   Result<int> result = Result<int>::success(42);
//   Result<int> error = Result<int>::failure(AgentError::ConfigNotFound, "File missing");
// ============================================
template<typename T>
struct Result {
    T value;
    AgentError error;
    std::string message;
    
    // Check if operation succeeded
    bool isSuccess() const { return error == AgentError::Success; }
    
    // Check if operation failed
    bool isError() const { return error != AgentError::Success; }
    
    // Get error code as integer
    int code() const { return errorCode(error); }
    
    // Get full error description
    std::string errorDescription() const {
        if (message.empty()) {
            return errorToString(error);
        }
        return errorToString(error) + ": " + message;
    }
    
    // Factory: Create success result
    static Result<T> success(T val) {
        return Result<T>{val, AgentError::Success, ""};
    }
    
    // Factory: Create failure result
    static Result<T> failure(AgentError err, const std::string& msg = "") {
        return Result<T>{T{}, err, msg};
    }
};

// ============================================
// VoidResult - For void functions
// ============================================
// Use when the function doesn't return a value
// but you still want error handling.
// ============================================
struct VoidResult {
    AgentError error;
    std::string message;
    
    bool isSuccess() const { return error == AgentError::Success; }
    bool isError() const { return error != AgentError::Success; }
    int code() const { return errorCode(error); }
    
    std::string errorDescription() const {
        if (message.empty()) {
            return errorToString(error);
        }
        return errorToString(error) + ": " + message;
    }
    
    static VoidResult success() {
        return VoidResult{AgentError::Success, ""};
    }
    
    static VoidResult failure(AgentError err, const std::string& msg = "") {
        return VoidResult{err, msg};
    }
};
