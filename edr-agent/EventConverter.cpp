#include "EventConverter.hpp"
#include "AgentId.hpp"    // UUID-based agent identification
#include <iostream>      // For std::cout, std::cerr
#include <Windows.h>     // For GetComputerNameA, DWORD
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

// External reference to global agent ID (set once at startup)
extern std::string g_agentId;
// External reference to global agent version  
extern std::string g_agentVersion;

std::string EventConverter::getHostname() {
    char hostname[256];
    DWORD size = sizeof(hostname);
    if (GetComputerNameA(hostname, &size)) {
        return std::string(hostname);
    }
    return "Unknown";
}





std::string EventConverter::generateEventId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    
    for (int i = 0; i < 8; i++) ss << std::setw(1) << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << std::setw(1) << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << std::setw(1) << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << std::setw(1) << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << std::setw(1) << dis(gen);
    
    return ss.str();
}
// more sysmon process will be add here later
std::string EventConverter::mapSysmonToEventType(int eventId) {
    switch (eventId) {
        case 1:  return "process";
        case 3:  return "network";
        case 5:  return "process";
        case 11: return "file";
        case 23: return "file";
        default: return "unknown";
    }
}
// here we will add the different severity level and we have to different severity level 
// we will addd here different condition 
// to do for the later 
std::string EventConverter::determineSeverity(int eventId) {
    return "info";
}

time_t EventConverter::parseSystemTime(const std::string& systemTime) {
    std::tm tm = {};
    std::istringstream ss(systemTime);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    
    if (ss.fail()) {
        return std::time(nullptr);
    }
    
    return _mkgmtime(&tm);
}

nlohmann::json EventConverter::sysmonEventToDjangoFormat(const nlohmann::json& sysmonEvent) {
    nlohmann::json djangoEvent;
    
    try {
        if (!sysmonEvent.contains("info")) {
            std::cerr << "[EventConverter] Missing 'info' field" << std::endl;
            return djangoEvent;
        }
        
        auto system = sysmonEvent["info"]["System"];
        auto eventData = sysmonEvent["info"]["EventData"];
        
        int eventId = system.value("EventID", 0);
        
        std::cout << "[EventConverter] Processing Event ID: " << eventId << std::endl;
        
        // Skip Event ID 5 (termination)
        if (eventId == 5) {
            std::cout << "[EventConverter] Skipping Event ID 5 (termination)" << std::endl;
            return djangoEvent;
        }
        
        std::string eventType = mapSysmonToEventType(eventId);
        
        if (eventType == "unknown") {
            std::cout << "[EventConverter] Unknown Event ID: " << eventId << std::endl;
            return djangoEvent;
        }
        
        std::cout << "[EventConverter] Event Type: " << eventType << std::endl;
        // ===== FIX: Use proper timestamp format =====

         std::string systemTime = system["TimeCreated"]["SystemTime"];
        time_t timestamp = parseSystemTime(systemTime);
        
        
     
        // Use UUID-based agent ID (not hostname)
        // hostname is still included in the 'host' object for display purposes
        djangoEvent["agent_id"] = g_agentId;  // UUID from AgentId module
        djangoEvent["event_id"] = generateEventId();
        djangoEvent["event_type"] = eventType;
        
        djangoEvent["timestamp"] = timestamp;


        djangoEvent["severity"] = determineSeverity(eventId);
        djangoEvent["version"] = "1.0";  // Schema/format version
        djangoEvent["agent_version"] = g_agentVersion;  // Agent software version for updates
        std::cout << "[EventConverter] agent_version set to: " << g_agentVersion << std::endl;
        

        // here we may need to add the different os_version here 
        djangoEvent["host"] = {
            {"hostname", system.value("Computer", "")},
            {"os", "Windows"},
            {"os_version", "11"}
        };
        
        if (eventType == "process" && eventId == 1) {
            djangoEvent["process"] = {
                {"name", eventData.value("Image", "")},
                {"pid", eventData.value("ProcessId", 0)},
                {"command_line", eventData.value("CommandLine", "")},
                {"user", eventData.value("User", "")},
                {"parent_image", eventData.value("ParentImage", "")},
                {"action", "created"}
            };
            std::cout << "[EventConverter] Process: " << eventData.value("Image", "Unknown") << std::endl;
        }
        else if (eventType == "network" && eventId == 3) {
            djangoEvent["network"] = {
                {"source_ip", eventData.value("SourceIp", "")},
                {"source_port", eventData.value("SourcePort", 0)},
                {"dest_ip", eventData.value("DestinationIp", "")},
                {"dest_port", eventData.value("DestinationPort", 0)},
                {"protocol", eventData.value("Protocol", "")},
                {"image", eventData.value("Image", "")}
            };
            std::cout << "[EventConverter] Network: " 
                      << eventData.value("DestinationIp", "Unknown") << ":" 
                      << eventData.value("DestinationPort", 0) << std::endl;
        }
        else if (eventType == "file" && (eventId == 11 || eventId == 23)) {
            std::string operation = (eventId == 11) ? "created" : "deleted";
            
            djangoEvent["file"] = {
                {"path", eventData.value("TargetFilename", "")},
                {"operation", operation},
                {"process_image", eventData.value("Image", "")}
            };
            std::cout << "[EventConverter] File: " << operation << " " 
                      << eventData.value("TargetFilename", "Unknown") << std::endl;
        }
        else {
            std::cerr << "[EventConverter] Unhandled event: " << eventType << "/" << eventId << std::endl;
            return djangoEvent;
        }
        
        std::cout << "[EventConverter] ✓ Conversion successful" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[EventConverter] ERROR: " << e.what() << std::endl;
        return nlohmann::json();
    }
    
    return djangoEvent;
}
