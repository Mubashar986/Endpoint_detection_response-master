// ============================================
// EnrollmentClient.cpp - Agent Registration
// ============================================
//
// TEACHING: The Enrollment Flow
// =============================
// 
//   Agent                          Server
//     |                               |
//     |  POST /api/v1/enroll/         |
//     |  {enrollment_token,           |
//     |   agent_id, hostname}         |
//     |------------------------------>|
//     |                               |
//     |                     Validate token
//     |                     Create Agent record
//     |                     Generate identity_token
//     |                               |
//     |  {success: true,              |
//     |   identity_token: "abc..."}   |
//     |<------------------------------|
//     |                               |
//   Save to auth.secret               |
//     |                               |
//
// ============================================

#include "EnrollmentClient.hpp"
#include "HttpClient.hpp"
#include "Logger.hpp"
#include "nlohmann/json.hpp"

#include <fstream>
#include <random>

/*
 * TEACHING: Windows-specific includes
 * ===================================
 * We need these for:
 * - gethostname() - Get computer name
 * - Windows version info
 */
#ifdef _WIN32
#include <winsock2.h>  // For gethostname
#pragma comment(lib, "ws2_32.lib")  // Link Winsock library
#endif

/*
 * REFACTOR NOTE: Use Canonical Metadata Sources
 * ==============================================
 * Instead of hardcoding version or generating our own UUID,
 * we use the globals from EdrAgent.cpp which are the SINGLE
 * SOURCE OF TRUTH.
 * 
 * g_agentId      → Set at startup via AgentId::getOrCreate()
 * g_agentVersion → Set from AGENT_VERSION macro (CMake)
 * 
 * This ensures enrollment uses the SAME identifiers as
 * telemetry, heartbeat, and all other communications.
 * 
 * TEACHING: extern keyword
 * ========================
 * 'extern' declares that a variable EXISTS in another file.
 * - Without extern: Compiler creates a NEW variable
 * - With extern: Compiler looks for EXISTING variable
 * 
 * EdrAgent.cpp DEFINES: std::string g_agentId;
 * Here we DECLARE:      extern std::string g_agentId;
 */
extern std::string g_agentId;       // From EdrAgent.cpp (AgentId::getOrCreate)
extern std::string g_agentVersion;  // From EdrAgent.cpp (AGENT_VERSION macro)


bool EnrollmentClient::enrollAgent(ConfigReader& config)
{
    /*
     * TEACHING: Function Flow
     * =======================
     * 1. Check if already enrolled → return early
     * 2. Get enrollment token from config
     * 3. Build JSON payload
     * 4. Send HTTP POST request
     * 5. Parse response
     * 6. Save identity token to file
     * 7. Return success/failure
     */
    
    // Step 1: Already enrolled?
    if (!config.needsEnrollment()) {
        LOG_INFO("Agent already enrolled - using existing identity token");
        return true;
    }
    
    // Step 2: Get enrollment token
    std::string enrollToken = config.getEnrollmentToken();
    if (enrollToken.empty()) {
        LOG_ERROR("No enrollment_token found in config.json. Cannot enroll.");
        return false;
    }
    
    // Step 2.5: FAIL-FAST check for Agent ID
    // This catches programming errors - g_agentId must be initialized before enrollment
    if (g_agentId.empty()) {
        LOG_ERROR("FATAL: g_agentId is empty. AgentId::getOrCreate() was not called.");
        LOG_ERROR("This is a programming error - check main.cpp initialization order.");
        return false;
    }
    
    LOG_INFO("Starting agent enrollment process...");
    
    // Step 3: Build payload
    /*
     * TEACHING: nlohmann::json
     * ========================
     * This is a popular C++ JSON library.
     * 
     * Usage:
     *   nlohmann::json j;
     *   j["key"] = "value";     // Add string
     *   j["number"] = 42;       // Add int
     *   j.dump();               // Convert to string
     * 
     * It's like Python's dict:
     *   j = {"key": "value"}
     */
    nlohmann::json payload;
    payload["enrollment_token"] = enrollToken;
    
    // REFACTORED: Use global agent ID (set at startup, persistent)
    // This ensures enrollment uses the SAME ID as telemetry/heartbeat
    payload["agent_id"] = g_agentId;
    
    payload["hostname"] = getHostname();
    payload["os_version"] = getOsVersion();
    
    // REFACTORED: Use global version from CMake (via EdrAgent.cpp)
    // Never hardcode versions - they become stale!
    payload["agent_version"] = g_agentVersion;
    
    LOG_INFO("Sending enrollment request for host: " + payload["hostname"].get<std::string>());
    
    // Step 4: Send HTTP request
    /*
     * TEACHING: Why try-catch?
     * ========================
     * Network operations can fail for many reasons:
     * - Server down
     * - No internet
     * - Timeout
     * 
     * Rather than crashing, we CATCH the error and handle it gracefully.
     */
    try {
        /*
         * TEACHING: HttpClient API
         * ========================
         * HttpClient has two usage patterns:
         * 
         * 1. Full constructor (for telemetry - uses persistent connection):
         *    HttpClient(server, port, apiPath, token, useHttps)
         * 
         * 2. Default constructor + POST (for one-off requests):
         *    HttpClient http;
         *    http.POST(fullUrl, data);
         * 
         * For enrollment, we use pattern 2 because:
         * - It's a one-time request
         * - We don't have a token yet (that's what we're getting!)
         */
        HttpClient http;  // Default constructor
        
        // Build full URL
        std::string protocol = config.useHttps() ? "https://" : "http://";
        std::string url = protocol + config.getHttpServer() + ":" + 
                          std::to_string(config.getHttpPort()) + "/api/v1/enroll/";
        
        LOG_INFO("Enrollment URL: " + url);
        
        /*
         * TEACHING: HTTP POST
         * ===================
         * POST = "Send data to server"
         * GET  = "Request data from server"
         * 
         * We POST because we're sending agent info TO the server.
         */
        http.addHeader("Content-Type", "application/json");
        std::string response = http.POST(url, payload.dump());
        
        // Step 5: Parse response
        auto jsonResponse = nlohmann::json::parse(response);
        
        /*
         * TEACHING: .contains() vs .find()
         * =================================
         * .contains("key")          → Returns true/false (C++20)
         * .find("key") != .end()    → Same but works in C++17
         * 
         * We check if "success" exists before accessing it.
         */
        if (jsonResponse.contains("success") && jsonResponse["success"] == true) {
            std::string identityToken = jsonResponse["identity_token"];
            
            // Step 6: Save token
            /*
             * TEACHING: std::filesystem::current_path()
             * =========================================
             * Returns the current working directory.
             * For a Windows service, this is usually set explicitly in main.cpp.
             */
            std::filesystem::path basePath = std::filesystem::current_path();
            
            if (saveIdentityToken(identityToken, basePath)) {
                LOG_INFO("Enrollment successful! Identity token saved to auth.secret");
                return true;
            } else {
                LOG_ERROR("Enrollment succeeded but failed to save identity token");
                return false;
            }
        }
        
        // Server returned error
        std::string error = jsonResponse.value("error", "Unknown server error");
        LOG_ERROR("Enrollment failed: " + error);
        return false;
        
    } catch (const nlohmann::json::exception& e) {
        /*
         * TEACHING: Multiple catch blocks
         * ================================
         * We can catch different exception types:
         * - json::exception = JSON parsing failed
         * - std::exception  = Any other error
         * 
         * Most specific catch first, most general last.
         */
        LOG_ERROR("JSON parse error during enrollment: " + std::string(e.what()));
        return false;
    } catch (const std::exception& e) {
        LOG_ERROR("Enrollment failed with exception: " + std::string(e.what()));
        return false;
    }
}


/*
 * DELETED: generateAgentId()
 * ==========================
 * This function was a DRY violation - it duplicated AgentId::generate()
 * from AgentId.cpp without the persistence logic.
 * 
 * The agent ID should come from g_agentId which is set at startup
 * using AgentId::getOrCreate() - this ensures:
 * 1. ID is persistent across restarts (saved to file)
 * 2. ID is consistent across all agent communications
 * 3. Service mode and console mode use the same ID
 * 
 * LESSON: Before writing new code, SEARCH for existing implementations!
 * Command: grep -r "generate.*id" edr-agent/
 */


std::string EnrollmentClient::getHostname()
{
    /*
     * TEACHING: gethostname()
     * =======================
     * This is a POSIX/Windows function that returns the computer's network name.
     * 
     * On Windows: Returns COMPUTERNAME (e.g., "DESKTOP-ABC123")
     * On Linux:   Returns hostname (e.g., "myserver")
     */
    
#ifdef _WIN32
    char buffer[256];
    if (gethostname(buffer, sizeof(buffer)) == 0) {
        return std::string(buffer);
    }
#endif
    return "Unknown";
}


std::string EnrollmentClient::getOsVersion()
{
    /*
     * TEACHING: OS Version Detection
     * ==============================
     * Getting exact Windows version is surprisingly complex.
     * For simplicity, we just return "Windows".
     * 
     * In production, you might use GetVersionEx() or 
     * RtlGetVersion() for detailed info like "Windows 10 22H2".
     */
    
#ifdef _WIN32
    return "Windows";
#else
    return "Unknown OS";
#endif
}


bool EnrollmentClient::saveIdentityToken(const std::string& token, 
                                         const std::filesystem::path& basePath)
{
    /*
     * TEACHING: File I/O in C++
     * =========================
     * std::ofstream = Output File Stream = Write to file
     * std::ifstream = Input File Stream  = Read from file
     * std::fstream  = Both read and write
     * 
     * Pattern:
     *   std::ofstream file("path");
     *   if (file.is_open()) {
     *       file << "content";
     *       file.close();
     *   }
     */
    
    std::filesystem::path secretPath = basePath / "auth.secret";
    
    std::ofstream out(secretPath);
    if (!out.is_open()) {
        LOG_ERROR("Failed to create auth.secret at: " + secretPath.string());
        return false;
    }
    
    out << token;
    out.close();
    
    LOG_INFO("Identity token saved to: " + secretPath.string());
    return true;
}
