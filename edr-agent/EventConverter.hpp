#ifndef EVENTCONVERTER_HPP
#define EVENTCONVERTER_HPP

#include "nlohmann/json.hpp"
#include <string>

class EventConverter {
public:
    static nlohmann::json sysmonEventToDjangoFormat(const nlohmann::json& sysmonEvent);
    static std::string getHostname();
    
private:
    static std::string generateEventId();
    static long long parseSystemTime(const std::string& systemTime);
    static std::string mapSysmonToEventType(int eventId);
    static std::string determineSeverity(int eventId);
    
};

#endif
