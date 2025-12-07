/**
 * @file AgentId.hpp
 * @brief Unique Agent Identifier Management
 * 
 * Provides functions to generate and manage persistent UUIDs
 * for uniquely identifying this agent across sessions and reboots.
 * 
 * UUID Format: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (36 chars)
 * Example: 550e8400-e29b-41d4-a716-446655440000
 * 
 * Priority:
 * 1. Read from agent_id.txt if exists
 * 2. Generate new UUID and save to file
 * 
 * @author EDR Team
 * @version 1.0
 */

#ifndef AGENT_ID_HPP
#define AGENT_ID_HPP

#include <string>
#include <filesystem>

namespace AgentId {

/**
 * @brief Gets existing agent ID or creates new one if first run
 * 
 * On first run:
 * 1. Generates a UUID v4 (random-based)
 * 2. Saves to agent_id.txt in the config directory
 * 3. Returns the UUID
 * 
 * On subsequent runs:
 * 1. Reads UUID from agent_id.txt
 * 2. Returns the existing UUID
 * 
 * @param configDir Directory where agent_id.txt will be stored
 * @return UUID string like "550e8400-e29b-41d4-a716-446655440000"
 */
std::string getOrCreate(const std::filesystem::path& configDir);

/**
 * @brief Generates a new random UUID v4
 * 
 * Uses Boost.UUID random generator for cryptographic randomness.
 * 
 * @return UUID string in format "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"
 */
std::string generate();

/**
 * @brief Validates UUID format
 * 
 * Checks if string matches UUID format:
 * - 36 characters total
 * - Dashes at positions 8, 13, 18, 23
 * - Only hex characters and dashes
 * 
 * @param uuid String to validate
 * @return true if valid UUID format, false otherwise
 */
bool isValid(const std::string& uuid);

}  // namespace AgentId

#endif  // AGENT_ID_HPP
