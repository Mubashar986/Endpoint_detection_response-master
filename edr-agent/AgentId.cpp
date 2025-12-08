/**
 * @file AgentId.cpp
 * @brief Implementation of Unique Agent Identifier Management
 * 
 * Uses C++ standard library for UUID generation (no Boost dependency)
 */

#include "AgentId.hpp"    // Our header
#include <random>         // std::random_device, std::mt19937_64
#include <sstream>        // std::stringstream
#include <iomanip>        // std::hex, std::setfill, std::setw
#include <fstream>        // File I/O
#include <iostream>       // Logging
#include <regex>          // UUID validation

namespace AgentId {

// ============================================
// UUID Generation using C++ Standard Library
// ============================================

std::string generate() {
    // Use random_device for cryptographic randomness
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    
    // Generate two 64-bit random numbers (128 bits total)
    uint64_t part1 = dis(gen);
    uint64_t part2 = dis(gen);
    
    // Format as UUID v4: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    // - Version 4 at position 12 (4xxx)
    // - Variant at position 16 (8, 9, A, or B)
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    // First segment: 8 hex chars
    ss << std::setw(8) << ((part1 >> 32) & 0xFFFFFFFF) << "-";
    
    // Second segment: 4 hex chars
    ss << std::setw(4) << ((part1 >> 16) & 0xFFFF) << "-";
    
    // Third segment: 4xxx (version 4)
    ss << std::setw(4) << (((part1 & 0xFFFF) & 0x0FFF) | 0x4000) << "-";
    
    // Fourth segment: yxxx (variant 8-B)
    ss << std::setw(4) << (((part2 >> 48) & 0x3FFF) | 0x8000) << "-";
    
    // Fifth segment: 12 hex chars
    ss << std::setw(12) << (part2 & 0xFFFFFFFFFFFF);
    
    return ss.str();
}

// ============================================
// UUID Validation
// ============================================

bool isValid(const std::string& uuid) {
    // UUID format: 8-4-4-4-12 hex characters with dashes
    // Example: 550e8400-e29b-41d4-a716-446655440000
    
    if (uuid.length() != 36) {
        return false;
    }
    
    // Regex pattern for UUID
    static const std::regex uuidPattern(
        "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$"
    );
    
    return std::regex_match(uuid, uuidPattern);
}

// ============================================
// Get or Create Persistent Agent ID
// ============================================

std::string getOrCreate(const std::filesystem::path& configDir) {
    // Use a FIXED system-wide location for agent ID
    // This ensures console mode and service mode use the SAME agent ID
    // C:\ProgramData\EDRAgent\agent_id.txt (accessible by all users/services)
    
    std::filesystem::path systemDir = "C:\\ProgramData\\EDRAgent";
    std::filesystem::path idPath = systemDir / "agent_id.txt";
    
    // Also check the legacy location (configDir) for backward compatibility
    std::filesystem::path legacyPath = configDir / "agent_id.txt";
    
    // ------------------------------------------
    // Step 1: Check if ID file exists in system location
    // ------------------------------------------
    if (std::filesystem::exists(idPath)) {
        std::ifstream file(idPath);
        
        if (file.is_open()) {
            std::string existingId;
            
            if (std::getline(file, existingId)) {
                // Trim whitespace (newlines, spaces, tabs)
                size_t endPos = existingId.find_last_not_of(" \n\r\t");
                if (endPos != std::string::npos) {
                    existingId = existingId.substr(0, endPos + 1);
                }
                
                // Validate the stored UUID
                if (!existingId.empty() && isValid(existingId)) {
                    std::cout << "[AgentId] Loaded existing ID: " << existingId << std::endl;
                    return existingId;
                } else {
                    std::cerr << "[AgentId] WARNING: Invalid ID in file, regenerating" << std::endl;
                }
            }
        }
    }
    
    // ------------------------------------------
    // Step 1b: Check legacy location and migrate if found
    // ------------------------------------------
    if (std::filesystem::exists(legacyPath)) {
        std::ifstream file(legacyPath);
        
        if (file.is_open()) {
            std::string existingId;
            
            if (std::getline(file, existingId)) {
                size_t endPos = existingId.find_last_not_of(" \n\r\t");
                if (endPos != std::string::npos) {
                    existingId = existingId.substr(0, endPos + 1);
                }
                
                if (!existingId.empty() && isValid(existingId)) {
                    std::cout << "[AgentId] Migrating ID from legacy location: " << existingId << std::endl;
                    
                    // Migrate to system location
                    try {
                        std::filesystem::create_directories(systemDir);
                        std::ofstream sysFile(idPath);
                        if (sysFile.is_open()) {
                            sysFile << existingId;
                            sysFile.close();
                            std::cout << "[AgentId] Migrated to: " << idPath.string() << std::endl;
                        }
                    } catch (...) {
                        // Ignore migration errors, we still have the ID
                    }
                    
                    return existingId;
                }
            }
        }
    }
    
    // ------------------------------------------
    // Step 2: Generate new UUID
    // ------------------------------------------
    std::string newId = generate();
    
    std::cout << "[AgentId] Generated new ID: " << newId << std::endl;
    
    // ------------------------------------------
    // Step 3: Save to SYSTEM location for persistence
    // ------------------------------------------
    try {
        // Create directory if it doesn't exist
        if (!std::filesystem::exists(systemDir)) {
            std::filesystem::create_directories(systemDir);
        }
        
        std::ofstream file(idPath);
        
        if (file.is_open()) {
            file << newId;
            file.close();
            std::cout << "[AgentId] Saved ID to: " << idPath.string() << std::endl;
        } else {
            std::cerr << "[AgentId] WARNING: Could not save ID to file. "
                      << "ID will be regenerated on next run." << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[AgentId] ERROR saving ID: " << e.what() << std::endl;
    }
    
    return newId;
}

}  // namespace AgentId

