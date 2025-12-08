// ============================================
// CommandAllowlist.hpp - Security Allowlist
// ============================================
// Validates incoming commands against a compile-time whitelist.
// Prevents execution of unauthorized commands.
// ============================================
#pragma once

#include <string>
#include <unordered_set>
#include "ErrorCodes.hpp"
#include "Logger.hpp"

class CommandAllowlist {
public:
    // ========================================
    // Check if a command type is allowed
    // ========================================
    // Returns true if command is in the whitelist
    // O(1) average time complexity using hash set
    static bool isAllowed(const std::string& commandType) {
        return ALLOWED_COMMANDS.count(commandType) > 0;
    }
    
    // ========================================
    // Validate with error reporting
    // ========================================
    // Returns VoidResult with CommandNotAllowed error if blocked
    static VoidResult validate(const std::string& commandType) {
        if (commandType.empty()) {
            LOG_WARN("[SECURITY] Blocked empty command type");
            return VoidResult::failure(AgentError::CommandNotAllowed, "Empty command type");
        }
        
        if (!isAllowed(commandType)) {
            LOG_WARN("[SECURITY] Blocked unauthorized command: " + commandType);
            return VoidResult::failure(AgentError::CommandNotAllowed, 
                "Unauthorized command: " + commandType);
        }
        
        return VoidResult::success();
    }
    
    // ========================================
    // Validate action (for "command" wrapper type)
    // ========================================
    static VoidResult validateAction(const std::string& action) {
        if (action.empty()) {
            LOG_WARN("[SECURITY] Blocked empty action");
            return VoidResult::failure(AgentError::CommandNotAllowed, "Empty action");
        }
        
        if (!isAllowedAction(action)) {
            LOG_WARN("[SECURITY] Blocked unauthorized action: " + action);
            return VoidResult::failure(AgentError::CommandNotAllowed,
                "Unauthorized action: " + action);
        }
        
        return VoidResult::success();
    }
    
    static bool isAllowedAction(const std::string& action) {
        return ALLOWED_ACTIONS.count(action) > 0;
    }

private:
    // ========================================
    // COMPILE-TIME WHITELIST OF ALLOWED COMMANDS
    // ========================================
    // To add a new command: add it here and rebuild
    // This is intentionally NOT configurable at runtime for security
    static inline const std::unordered_set<std::string> ALLOWED_COMMANDS = {
        // Protocol commands
        "ping",             // Health check
        "auth",             // Authentication
        "echo",             // Echo test
        "event",            // Event notification
        
        // WebSocket command wrapper
        "command",          // Container for response actions
        
        // Information gathering
        "system_info",      // System information
        
        // Response actions (direct type for HTTP polling)
        "kill_process",     // Terminate a process
        "isolate_host",     // Network isolation
        "deisolate_host",   // Remove network isolation
        
        // Advanced (high security risk)
        "reverse_shell"     // Remote shell access
    };
    
    // ========================================
    // ALLOWED ACTIONS (for "command" wrapper)
    // ========================================
    static inline const std::unordered_set<std::string> ALLOWED_ACTIONS = {
        "kill_process",
        "isolate_host",
        "deisolate_host"
    };
};
