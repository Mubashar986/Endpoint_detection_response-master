#include "EventConverter.hpp"
#include <iostream>      // For std::cout, std::cerr
#include <Windows.h>     // For GetComputerNameA, DWORD
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>

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
        
        std::string systemTime = system["TimeCreated"]["SystemTime"];
        time_t timestamp = parseSystemTime(systemTime);
        
        djangoEvent["agent_id"] = getHostname();
        djangoEvent["event_id"] = generateEventId();
        djangoEvent["event_type"] = eventType;
        djangoEvent["timestamp"] = timestamp;
        djangoEvent["severity"] = determineSeverity(eventId);
        djangoEvent["version"] = "1.0";
        
        djangoEvent["host"] = {
            {"hostname", system.value("Computer", "")},
            {"os", "Windows"},
            {"os_version", "10"}
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
