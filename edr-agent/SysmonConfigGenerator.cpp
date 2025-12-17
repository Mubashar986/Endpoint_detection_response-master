#include "SysmonConfigGenerator.hpp"
#include "Logger.hpp"
#include <fstream>
#include <iostream>

// ==========================================
// SYSMON XML RULE TEMPLATES
// ==========================================

// 1. Process Monitor (Event ID 1 & 5)
const std::string RULE_PROCESS = R"(
    <!-- Event ID 1: Process Creation -->
    <RuleGroup name="ProcessCreate" groupRelation="or">
      <ProcessCreate onmatch="include">
        <Image condition="begin with">C:\</Image>
      </ProcessCreate>
    </RuleGroup>

    <!-- Event ID 5: Process Termination -->
    <RuleGroup name="ProcessTerminate" groupRelation="or">
      <ProcessTerminate onmatch="include">
        <Image condition="begin with">C:\</Image>
      </ProcessTerminate>
    </RuleGroup>
)";

// 2. Network Monitor (Event ID 3)
const std::string RULE_NETWORK = R"(
    <!-- Event ID 3: Network Connection -->
    <RuleGroup name="NetworkConnect" groupRelation="or">
      <NetworkConnect onmatch="include">
        <Initiated condition="is">true</Initiated>
      </NetworkConnect>
    </RuleGroup>
)";

// 3. File Monitor (Event ID 11)
const std::string RULE_FILE = R"(
    <!-- Event ID 11: File Creation -->
    <RuleGroup name="FileCreate" groupRelation="or">
      <FileCreate onmatch="include">
        <TargetFilename condition="contains">Temp</TargetFilename>
        <TargetFilename condition="contains">AppData</TargetFilename>
        <TargetFilename condition="end with">.exe</TargetFilename>
        <TargetFilename condition="end with">.dll</TargetFilename>
        <TargetFilename condition="end with">.ps1</TargetFilename>
      </FileCreate>
    </RuleGroup>
)";

// 4. Registry Monitor (Event ID 12, 13, 14)
const std::string RULE_REGISTRY = R"(
    <!-- Event ID 12-14: Registry Events -->
    <RuleGroup name="RegistryEvent" groupRelation="or">
      <RegistryEvent onmatch="include">
        <TargetObject condition="contains">CurrentVersion\Run</TargetObject>
      </RegistryEvent>
    </RuleGroup>
)";

// 5. DNS Monitor (Event ID 22)
const std::string RULE_DNS = R"(
    <!-- Event ID 22: DNS Query -->
    <RuleGroup name="DnsQuery" groupRelation="or">
      <DnsQuery onmatch="exclude">
        <QueryName condition="end with">microsoft.com</QueryName>
        <QueryName condition="end with">windows.com</QueryName>
      </DnsQuery>
    </RuleGroup>
)";

bool SysmonConfigGenerator::generateConfig(const nlohmann::json& moduleConfig, const std::string& outputPath) {
    try {
        std::string xmlContent = buildXmlForModules(moduleConfig);
        
        std::ofstream outFile(outputPath);
        if (!outFile.is_open()) {
            LOG_ERROR("Failed to open output path for Sysmon config: " + outputPath);
            return false;
        }

        outFile << xmlContent;
        outFile.close();

        LOG_INFO("Successfully generated Sysmon config at: " + outputPath);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception generating Sysmon config: " + std::string(e.what()));
        return false;
    }
}

std::string SysmonConfigGenerator::buildXmlForModules(const nlohmann::json& modules) {
    std::stringstream ss;
    
    // Header
    ss << "<Sysmon schemaversion=\"4.90\">\n";
    ss << "  <HashAlgorithms>SHA256</HashAlgorithms>\n";
    ss << "  <DnsLookup>False</DnsLookup>\n";
    ss << "  <CheckRevocation/>\n\n";
    ss << "  <EventFiltering>\n";

    // Inject rules based on config
    // We assume default is enabled if missing, or handle safe check
    
    // Helper lambda to check enablement
    auto isEnabled = [&](const std::string& name) -> bool {
        if (modules.contains(name)) {
            return modules[name].value("enabled", true);
        }
        return true; // Default enabled if missing? Or aligned with ConfigManager defaults
    };

    if (isEnabled("process_monitor")) {
        ss << RULE_PROCESS << "\n";
    }
    if (isEnabled("network_monitor")) {
        ss << RULE_NETWORK << "\n";
    }
    if (isEnabled("file_monitor")) {
        ss << RULE_FILE << "\n";
    }
    if (isEnabled("registry_monitor")) {
        ss << RULE_REGISTRY << "\n";
    }
    if (isEnabled("dns_monitor")) {
        ss << RULE_DNS << "\n";
    }

    // Footer
    ss << "  </EventFiltering>\n";
    ss << "</Sysmon>";

    return ss.str();
}

std::string SysmonConfigGenerator::getRulesForModule(const std::string& moduleName) {
    // Not strictly needed if buildXmlForModules does it directly, 
    // but kept for interface completeness or future granular use.
    if (moduleName == "process_monitor") return RULE_PROCESS;
    if (moduleName == "network_monitor") return RULE_NETWORK;
    if (moduleName == "file_monitor") return RULE_FILE;
    if (moduleName == "registry_monitor") return RULE_REGISTRY;
    if (moduleName == "dns_monitor") return RULE_DNS;
    return "";
}
