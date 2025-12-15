// ============================================
// EnrollmentClient.hpp - Agent Registration
// ============================================
// 
// TEACHING: What is this file for?
// ================================
// When a new agent starts for the first time, it needs to "register"
// itself with the server. This is called ENROLLMENT.
//
// Think of it like:
// - New employee's first day → HR gives them an employee badge
// - New agent's first run → Server gives it an identity token
//
// TEACHING: Header Files (.hpp) vs Source Files (.cpp)
// ====================================================
// - .hpp = DECLARATIONS = "What functions exist?"
// - .cpp = DEFINITIONS  = "How do functions work?"
//
// We put declarations in .hpp so OTHER files can use our functions.
// We put implementations in .cpp to keep compilation fast.
//
// ============================================
#pragma once

/*
 * TEACHING: #pragma once
 * =======================
 * This is an "include guard". It prevents this file from being
 * included multiple times in the same compilation.
 * 
 * Without it:
 *   #include "EnrollmentClient.hpp"
 *   #include "EnrollmentClient.hpp"  // ERROR: class defined twice!
 * 
 * With it:
 *   #include "EnrollmentClient.hpp"
 *   #include "EnrollmentClient.hpp"  // OK: second include is ignored
 */

#include <string>
#include <filesystem>  // C++17 for path operations
#include "ConfigReader.hpp"

/*
 * TEACHING: Class Design
 * ======================
 * This class uses STATIC methods. Why?
 * 
 * Static = "Belongs to the class, not an instance"
 * 
 * Regular method:
 *   EnrollmentClient client;
 *   client.enrollAgent(config);  // Need to create object first
 * 
 * Static method:
 *   EnrollmentClient::enrollAgent(config);  // Just call directly!
 * 
 * We use static because EnrollmentClient doesn't need to store any data.
 * It just performs actions and returns results.
 */

class EnrollmentClient {
public:
    // Main function - handles the entire enrollment process
    static bool enrollAgent(ConfigReader& config);
    
    /*
     * REMOVED: generateAgentId()
     * ==========================
     * Previously this class generated its own UUIDs, which was a DRY violation.
     * Now we use the global g_agentId from EdrAgent.cpp, which is initialized
     * via AgentId::getOrCreate() - ensuring persistence and consistency.
     */
    
private:
    /*
     * TEACHING: public vs private
     * ===========================
     * public  = Anyone can call these
     * private = Only THIS class can call these
     * 
     * We hide helper functions as private because:
     * 1. They're implementation details
     * 2. We might change them later
     * 3. External code shouldn't depend on them
     */
    
    static std::string getHostname();
    static std::string getOsVersion();
    static bool saveIdentityToken(const std::string& token, 
                                  const std::filesystem::path& basePath);
};
