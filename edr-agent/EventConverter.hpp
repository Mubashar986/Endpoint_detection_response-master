#ifndef EVENTCONVERTER_HPP
#define EVENTCONVERTER_HPP

#include "nlohmann/json.hpp"
#include <string>

class EventConverter {
public:
    static nlohmann::json sysmonEventToDjangoFormat(const nlohmann::json& sysmonEvent);
    
private:
    static std::string generateEventId();
    static std::string getHostname();
    static long long parseSystemTime(const std::string& systemTime);
    static std::string mapSysmonToEventType(int eventId);
    static std::string determineSeverity(int eventId);
};

#endif
