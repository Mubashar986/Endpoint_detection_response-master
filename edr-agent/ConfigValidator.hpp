// ============================================
// ConfigValidator.hpp - Config Validation
// ============================================
// Validates config.json fields at startup.
// Uses AgentError codes for specific error reporting.
// ============================================
#pragma once

#include <string>
#include "nlohmann/json.hpp"
#include "ErrorCodes.hpp"
#include "Logger.hpp"

class ConfigValidator {
public:
    // ========================================
    // Main validation entry point
    // ========================================
    static VoidResult validate(const nlohmann::json& config) {
        if (config.is_null()) {
            return VoidResult::failure(AgentError::ConfigNotFound, "Config is null");
        }
        
        VoidResult result;
        
        // 1. Validate required fields exist
        result = validateRequiredFields(config);
        if (result.isError()) return result;
        
        // 2. Validate HTTP config
        result = validateHttpConfig(config);
        if (result.isError()) return result;
        
        // 3. Validate event processor
        result = validateEventProcessor(config);
        if (result.isError()) return result;
        
        LOG_INFO("Config validation: All checks passed");
        return VoidResult::success();
    }

private:
    // ========================================
    // Field presence validation
    // ========================================
    static VoidResult validateRequiredFields(const nlohmann::json& config) {
        const std::vector<std::string> required = {
            "http_server",
            "http_port"
            // "api_path" removed - it has a default fallback in ConfigReader::getApiPath()
            // "config_version" removed - it is optional in bootstrap config
            // "auth_token" removed - now stored in auth.secret only
        };
        
        for (const auto& field : required) {
            if (!config.contains(field)) {
                LOG_ERROR("Config missing required field: " + field);
                return VoidResult::failure(AgentError::ConfigMissingField, 
                    "Missing required field: " + field);
            }
        }
        
        // Optional validation for config_version if present
        if (config.contains("config_version")) {
            int version = config.value("config_version", 0);
            if (version < 1) {
                LOG_WARN("config_version is present but < 1: " + std::to_string(version));
            }
        } else if (config.contains("_config_version")) {
            // Check for the server-sent version key
            int version = config.value("_config_version", 0);
            if (version < 1) {
                LOG_WARN("_config_version is present but < 1: " + std::to_string(version));
            }
        }
        
        return VoidResult::success();
    }
    
    // ========================================
    // HTTP configuration validation
    // ========================================
    static VoidResult validateHttpConfig(const nlohmann::json& config) {
        // http_server: non-empty string
        std::string server = config.value("http_server", "");
        if (server.empty()) {
            LOG_ERROR("http_server cannot be empty");
            return VoidResult::failure(AgentError::ConfigInvalidValue,
                "http_server cannot be empty");
        }
        
        // http_port: 1-65535
        int port = config.value("http_port", 0);
        if (port < 1 || port > 65535) {
            LOG_ERROR("http_port must be 1-65535, got: " + std::to_string(port));
            return VoidResult::failure(AgentError::ConfigInvalidValue,
                "http_port must be between 1 and 65535");
        }
        
        // api_path: starts with "/" (but allow fallback to default in ConfigReader)
        std::string apiPath = config.value("api_path", "");
        if (!apiPath.empty() && apiPath[0] != '/') {
            LOG_ERROR("api_path must start with '/', got: " + apiPath);
            return VoidResult::failure(AgentError::ConfigInvalidValue,
                "api_path must start with '/'");
        }
        
        // NOTE: If api_path is empty, ConfigReader::getApiPath() provides the default: "/api/v1/telemetry/"
        // This allows server-sent configs to omit api_path without causing validation failures
        
        // auth_token: REMOVED check here. 
        // Logic moved to ConfigReader::getAuthToken() which checks auth.secret
        // This allows config.json to exist without a token.
        
        return VoidResult::success();
    }
    
    // ========================================
    // Event processor validation
    // ========================================
    static VoidResult validateEventProcessor(const nlohmann::json& config) {
        // event_processor.source must exist and have at least 1 entry
        if (!config.contains("event_processor")) {
            LOG_ERROR("event_processor section missing");
            return VoidResult::failure(AgentError::ConfigMissingField,
                "event_processor section required");
        }
        
        const auto& ep = config["event_processor"];
        if (!ep.contains("source") || !ep["source"].is_array()) {
            LOG_ERROR("event_processor.source must be an array");
            return VoidResult::failure(AgentError::ConfigMissingField,
                "event_processor.source array required");
        }
        
        const auto& sources = ep["source"];
        if (sources.empty()) {
            LOG_ERROR("event_processor.source cannot be empty");
            return VoidResult::failure(AgentError::ConfigMissingField,
                "At least one event source required");
        }
        
        // Validate each source entry
        int index = 0;
        for (const auto& src : sources) {
            if (!src.contains("path") || !src["path"].is_string()) {
                LOG_ERROR("source[" + std::to_string(index) + "].path missing or not string");
                LOG_ERROR("DUMP: " + src.dump());
                return VoidResult::failure(AgentError::ConfigMissingField,
                    "source[" + std::to_string(index) + "].path required");
            }
            
            std::string path = src["path"];
            if (path.empty()) {
                LOG_ERROR("source[" + std::to_string(index) + "].path cannot be empty");
                return VoidResult::failure(AgentError::ConfigInvalidValue,
                    "source[" + std::to_string(index) + "].path cannot be empty");
            }
            
            if (!src.contains("query") || !src["query"].is_string()) {
                LOG_ERROR("source[" + std::to_string(index) + "].query missing or not string");
                return VoidResult::failure(AgentError::ConfigMissingField,
                    "source[" + std::to_string(index) + "].query required");
            }
            
            index++;
        }
        
        LOG_DEBUG("Validated " + std::to_string(index) + " event sources");
        return VoidResult::success();
    }
};
