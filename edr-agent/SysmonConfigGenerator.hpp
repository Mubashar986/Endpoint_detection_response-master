#ifndef SYSMONCONFIGGENERATOR_HPP
#define SYSMONCONFIGGENERATOR_HPP

#include <string>
#include "nlohmann/json.hpp"

class SysmonConfigGenerator {
public:
    /**
     * @brief Generates a Sysmon XML configuration file based on enabled modules.
     * 
     * @param moduleConfig The "modules" section of the agent configuration JSON.
     * @param outputPath The path where the generated XML file should be saved.
     * @return true if generation and writing were successful, false otherwise.
     */
    static bool generateConfig(const nlohmann::json& moduleConfig, const std::string& outputPath);

private:
    /**
     * @brief Helper to build the complete XML string.
     */
    static std::string buildXmlForModules(const nlohmann::json& modules);

    /**
     * @brief Returns the XML rule block for a specific module if enabled.
     * 
     * @param moduleName The name of the module (e.g., "process_monitor").
     * @return std::string The XML block or empty string if disabled/unknown.
     */
    static std::string getRulesForModule(const std::string& moduleName);
};

#endif // SYSMONCONFIGGENERATOR_HPP
