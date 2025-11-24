
#ifndef COMMANDPROCESSOR_HPP
#define COMMANDPROCESSOR_HPP

#include "nlohmann/json.hpp"
#include <string>

namespace CommandProcessor {
	// Function declarations
	// System Info & Basic Commands
	std::string executeCommand(const std::string& command);
	std::string getSystemInfoSummary();
	std::string getHostName();
	std::string getWindowsVersion();
	std::string getWindowsVersionNumber();
	std::string getMacAddress();
	bool startReverseShell(const std::string& ip, const int port);

	// Response Actions (New)
	bool killProcess(unsigned long pid);
	bool killProcessTree(unsigned long pid);
	bool isolateHost(const std::string& serverIp, int serverPort);
	bool deisolateHost();

	// Command Polling (New)
	void startCommandPolling();
	void stopCommandPolling();
}

#endif // COMMANDPROCESSOR_HPP