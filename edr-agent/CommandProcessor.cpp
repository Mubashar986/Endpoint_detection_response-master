
#include "CommandProcessor.hpp"
#include "ConfigReader.hpp"
#include "HttpClient.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <windows.h>
#include <iphlpapi.h>
#include <winnt.h>
#include <stdexcept>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <TlHelp32.h>

#pragma comment(lib, "iphlpapi.lib")

namespace CommandProcessor 
{
    using json = nlohmann::json;

    // Polling Thread State
    static std::atomic<bool> pollingActive{false};
    static std::condition_variable pollCV;
    static std::mutex pollMutex;
    static std::thread pollThread;

    // Forward declaration of internal helper
    json executeResponseCommand(const std::string& type, const json& params);

    std::string executeCommand(const std::string& command) {
        json commandJson, responseJson;
        std::string commandType;

        try 
        {
            commandJson = json::parse(command);
            commandType = commandJson.at("type").get<std::string>();
        } 
        catch (const std::exception&) 
        {
            return R"({"type": "error", "status": "invalid JSON or missing 'type' field"})";
        }

        // Check for Response Actions first
        if (commandType == "kill_process" || commandType == "isolate_host" || commandType == "deisolate_host") {
            json params = commandJson.value("parameters", json::object());
            return executeResponseCommand(commandType, params).dump(4);
        }

        auto executeCommandByType = [&]() -> json 
        {
            if (commandType == "ping") 
            {
                return {{"type", "ping"}, {"status", "pong"}};
            }

            if (commandType == "auth") 
            {
                if (commandJson.value("message", "") == "Authentication required") 
                {
                    std::cout << "Performing authentication" << std::endl;
                    return {
                        {"type", "auth"},
                        {"info", {
                            {"hostname", getHostName()},
                            {"os", "Windows"},
                            {"version", getWindowsVersion()},
                            {"version_number", getWindowsVersionNumber()},
                            {"mac_address", getMacAddress()}
                        }}
                    };
                }
                else if (commandJson.value("message", "") == "Authentication successful")
                {
                    std::cout << "Authentication successful" << std::endl;
                    return "";
                }

                return {{"type", "error"}, {"status", "invalid authentication message"}};
            }

            if (commandType == "system_info") 
            {
                try 
                {
                    std::cout << "Getting system info" << std::endl;
                    return {
                        {"type", "system_info"},
                        {"info", json::parse(getSystemInfoSummary())}
                    };
                } 
                catch (const std::exception&) 
                {
                    std::cout << "Failed to get system info" << std::endl;
                    return {{"type", "error"}, {"status", "failed to get system info"}};
                }
            }

            if (commandType == "reverse_shell") 
            {
                std::cout << "Starting reverse shell" << std::endl;
                ConfigReader configReader("config.json");
                std::string ip = configReader.getServerReverseShellIp();
                int port = configReader.getServerReverseShellPort();
                std::cout << "IP: " << ip << ", Port: " << port << std::endl;
                if (ip.empty() || port == -1)
                {
                    return {{"type", "error"}, {"status", "missing or invalid 'ip' or 'port'"}};
                }
                if (startReverseShell(ip, port)) 
                {
                    return {{"type", "reverse_shell"}, {"status", "reverse shell started"}};
                }
                return {{"type", "error"}, {"status", "failed to start reverse shell"}};
            }

            if (commandType == "echo") 
            {
                if (commandJson.contains("message") && commandJson["message"].is_string()) 
                {
                    return "";
                }
                return {{"type", "error"}, {"status", "missing or invalid 'message'"}};
            }

            if (commandType == "event") 
            {
                if (commandJson.contains("message") && commandJson["message"].is_string()) 
                {
                    return "";
                }
                return {{"type", "error"}, {"status", "missing or invalid 'message'"}};
            }
            return {{"type", "error"}, {"status", "unknown command"}};
        };

        responseJson = executeCommandByType();

        return responseJson.dump(4);
    }

    // Helper to execute response actions
    json executeResponseCommand(const std::string& type, const json& params) {
        if (type == "kill_process") {
            if (params.contains("pid") && params["pid"].is_number()) {
                unsigned long pid = params["pid"];
                
                // Try to kill
                if (killProcessTree(pid)) {
                    return {{"status", "success"}, {"message", "Process tree terminated"}};
                } else {
                    // Get the last error code from the OS
                    DWORD error = GetLastError();
                    std::string errorMsg = "Failed to terminate process. Error Code: " + std::to_string(error);
                    
                    if (error == 5) errorMsg += " (Access Denied)";
                    else if (error == 87) errorMsg += " (Invalid Parameter/PID)";
                    
                    return {{"status", "failed"}, {"message", errorMsg}, {"error_code", error}};
                }
            }
            return {{"status", "failed"}, {"message", "Missing PID"}};
        }

        if (type == "isolate_host") {
            // Default to localhost if not provided (safe failover)
            ConfigReader config("config.json");
            std::string serverIp = config.getHttpServer(); 
            if (serverIp.find("localhost") != std::string::npos) serverIp = "127.0.0.1";
            
            if (isolateHost(serverIp, 8000)) { // Hardcoded port for MVP
                return {{"status", "success"}, {"message", "Host isolated"}};
            }
            return {{"status", "failed"}, {"message", "Failed to isolate host. Check Admin privileges."}};
        }

        if (type == "deisolate_host") {
            if (deisolateHost()) {
                return {{"status", "success"}, {"message", "Host de-isolated"}};
            }
            return {{"status", "failed"}, {"message", "Failed to de-isolate host. Check Admin privileges."}};
        }

        return {{"status", "error"}, {"message", "Unknown command type"}};
    }

    // ==========================================
    // RESPONSE ACTIONS IMPLEMENTATION
    // ==========================================

    bool killProcess(unsigned long pid) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (hProcess == NULL) {
            return false;
        }

        if (!TerminateProcess(hProcess, 1)) {
            CloseHandle(hProcess);
            return false;
        }

        // Verify death (wait up to 2 seconds)
        DWORD waitResult = WaitForSingleObject(hProcess, 2000);
        CloseHandle(hProcess);

        return waitResult == WAIT_OBJECT_0;
    }

    bool killProcessTree(unsigned long pid) {
        // Find children
        std::vector<unsigned long> children;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe32;
            pe32.dwSize = sizeof(PROCESSENTRY32);
            if (Process32First(snapshot, &pe32)) {
                do {
                    if (pe32.th32ParentProcessID == pid) {
                        children.push_back(pe32.th32ProcessID);
                    }
                } while (Process32Next(snapshot, &pe32));
            }
            CloseHandle(snapshot);
        }

        // Kill children first (recursive)
        for (unsigned long childPid : children) {
            killProcessTree(childPid);
        }

        // Kill parent
        return killProcess(pid);
    }

    bool runNetshCommand(const std::string& args) {
        std::string fullCmd = "netsh.exe " + args;
        
        STARTUPINFOA si = {sizeof(si)};
        PROCESS_INFORMATION pi;
        
        BOOL success = CreateProcessA(
            NULL, (LPSTR)fullCmd.c_str(), NULL, NULL, FALSE, 
            CREATE_NO_WINDOW, NULL, NULL, &si, &pi
        );
        
        if (!success) return false;
        
        WaitForSingleObject(pi.hProcess, 5000);
        DWORD exitCode;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        
        return exitCode == 0;
    }

    bool isolateHost(const std::string& serverIp, int serverPort) {
        // 1. Block all outbound
        if (!runNetshCommand("advfirewall firewall add rule name=\"EDR_BLOCK_ALL\" dir=out action=block")) return false;
        
        // 2. Allow Antigravity IDE (Web ports)
        if (!runNetshCommand("advfirewall firewall add rule name=\"EDR_ALLOW_ANTIGRAVITY\" dir=out action=allow protocol=TCP remoteport=80,443")) return false;

        // 3. Allow EDR Server
        std::string allowServer = "advfirewall firewall add rule name=\"EDR_ALLOW_SERVER\" dir=out action=allow remoteip=" + serverIp + " protocol=TCP remoteport=" + std::to_string(serverPort);
        if (!runNetshCommand(allowServer)) return false;

        // 4. Allow DNS (UDP 53)
        if (!runNetshCommand("advfirewall firewall add rule name=\"EDR_ALLOW_DNS\" dir=out action=allow protocol=UDP remoteport=53")) return false;

        return true;
    }

    bool deisolateHost() {
        bool success = true;
        // We attempt all removals, but track if any failed
        if (!runNetshCommand("advfirewall firewall delete rule name=\"EDR_BLOCK_ALL\"")) success = false;
        if (!runNetshCommand("advfirewall firewall delete rule name=\"EDR_ALLOW_ANTIGRAVITY\"")) success = false;
        if (!runNetshCommand("advfirewall firewall delete rule name=\"EDR_ALLOW_SERVER\"")) success = false;
        if (!runNetshCommand("advfirewall firewall delete rule name=\"EDR_ALLOW_DNS\"")) success = false;
        
        return success;
    }

    // ==========================================
    // POLLING THREAD IMPLEMENTATION
    // ==========================================

    void pollCommandsLoop() {
        HttpClient client;
        ConfigReader config("config.json");
        std::string serverHost = config.getHttpServer();
        int serverPort = config.getHttpPort();
        std::string serverUrl = "http://" + serverHost + ":" + std::to_string(serverPort);
        std::string authToken = config.getAuthToken();
        
        // Ensure URL doesn't end with slash for consistency
        if (serverUrl.back() == '/') serverUrl.pop_back();

        client.addHeader("Authorization", "Token " + authToken);
        client.addHeader("X-Agent-ID", getHostName()); // Use hostname as Agent ID for MVP

        std::cout << "[CommandPoll] Thread started. Polling " << serverUrl << std::endl;

        while (pollingActive) {
            try {
                std::string response = client.GET(serverUrl + "/api/v1/commands/poll/");
                
                if (!response.empty() && response != "{}" && response.length() > 2) {
                    json commandJson = json::parse(response);
                    
                    if (commandJson.contains("command_id")) {
                        std::string commandId = commandJson["command_id"];
                        std::string commandType = commandJson["type"];
                        json params = commandJson.value("parameters", json::object());
                        
                        std::cout << "[CommandPoll] Received command: " << commandType << std::endl;

                        // Execute command
                        json result = executeResponseCommand(commandType, params);
                        
                        // Report result back
                        client.POST(serverUrl + "/api/v1/commands/result/" + commandId + "/", 
                                    result.dump());
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[CommandPoll] Error: " << e.what() << std::endl;
            }
            
            // Interruptible sleep for 5 seconds
            std::unique_lock<std::mutex> lock(pollMutex);
            pollCV.wait_for(lock, std::chrono::seconds(5), []{ return !pollingActive; });
        }
    }

    void startCommandPolling() {
        if (!pollingActive) {
            pollingActive = true;
            pollThread = std::thread(pollCommandsLoop);
            pollThread.detach();
            std::cout << "[CommandPoll] Service Started" << std::endl;
        }
    }

    void stopCommandPolling() {
        if (pollingActive) {
            pollingActive = false;
            pollCV.notify_all();
            if (pollThread.joinable()) pollThread.join();
            std::cout << "[CommandPoll] Service Stopped" << std::endl;
        }
    }

    // ==========================================
    // EXISTING SYSTEM INFO FUNCTIONS
    // ==========================================

    std::string getHostName() 
    {
        char hostname[256];
        DWORD size = sizeof(hostname);
        GetComputerNameA(hostname, &size);
        return std::string(hostname);
    }


    std::string getWindowsVersion() 
    {
        NTSTATUS(WINAPI * RtlGetVersion)(LPOSVERSIONINFOEXW) = nullptr;
        OSVERSIONINFOEXW osInfo = {0}; // Zero-initialize the structure

        // Explicitly set the size of the structure
        osInfo.dwOSVersionInfoSize = sizeof(osInfo);

        // Attempt to load the function from ntdll
        *(FARPROC*)&RtlGetVersion = GetProcAddress(GetModuleHandleA("ntdll"), "RtlGetVersion");

        if (RtlGetVersion != nullptr && RtlGetVersion(&osInfo) == 0) { // Check if function call was successful
            if (osInfo.dwMajorVersion == 10 && osInfo.dwMinorVersion == 0) {
                if (osInfo.dwBuildNumber >= 22000) {
                    return "11";
                }
                return "10";
            } else if (osInfo.dwMajorVersion == 6) {
                if (osInfo.dwMinorVersion == 3) return "8.1";
                if (osInfo.dwMinorVersion == 2) return "8";
                if (osInfo.dwMinorVersion == 1) return "7";
                if (osInfo.dwMinorVersion == 0) return "Vista";
            }
        }

        return "Unknown";
    }


    std::string getWindowsVersionNumber() 
    {
        NTSTATUS(WINAPI * RtlGetVersion)(LPOSVERSIONINFOEXW) = nullptr;
        OSVERSIONINFOEXW osInfo = {0}; // Zero-initialize the structure

        // Explicitly set the size of the structure
        osInfo.dwOSVersionInfoSize = sizeof(osInfo);

        // Attempt to load the function from ntdll
        *(FARPROC*)&RtlGetVersion = GetProcAddress(GetModuleHandleA("ntdll"), "RtlGetVersion");

        if (RtlGetVersion != nullptr && RtlGetVersion(&osInfo) == 0) { // Check if function call was successful
            // Create a string with major, minor, and build number
            return std::to_string(osInfo.dwMajorVersion) + "." +
                std::to_string(osInfo.dwMinorVersion) + "." +
                std::to_string(osInfo.dwBuildNumber);
        }

        return "Unknown";
    }


    std::string getMacAddress() 
    {
        IP_ADAPTER_INFO* adapterInfo = nullptr;
        ULONG bufferSize = 0;

        if (GetAdaptersInfo(adapterInfo, &bufferSize) == ERROR_BUFFER_OVERFLOW) {
            adapterInfo = (IP_ADAPTER_INFO*)malloc(bufferSize);
        }

        if (GetAdaptersInfo(adapterInfo, &bufferSize) == NO_ERROR) {
            char mac[18];
            snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                adapterInfo->Address[0], adapterInfo->Address[1],
                adapterInfo->Address[2], adapterInfo->Address[3],
                adapterInfo->Address[4], adapterInfo->Address[5]);
            free(adapterInfo);
            return std::string(mac);
        }

        free(adapterInfo);
        return "Unknown";
    }


    std::string getSystemInfoSummary() 
    {
        nlohmann::json sysinfo;
        nlohmann::json system_info;

        // Get hostname
        sysinfo["hostname"] = getHostName();

        // Get operating system information
        sysinfo["os"] = "Windows";
        sysinfo["version"] = getWindowsVersion();
        sysinfo["version_number"] = getWindowsVersionNumber();

        // Get current timestamp
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        std::tm local_tm;
        localtime_s(&local_tm, &now_c);
        std::stringstream ss;
        ss << std::put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
        sysinfo["timestamp"] = ss.str();

        // Get CPU information
        SYSTEM_INFO sysInfo;
        GetNativeSystemInfo(&sysInfo);
        sysinfo["cpu_cores"] = sysInfo.dwNumberOfProcessors;

        // Get memory information
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        GlobalMemoryStatusEx(&memInfo);
        sysinfo["total_memory"] = static_cast<float>(memInfo.ullTotalPhys / (1024 * 1024 * 1024));
        sysinfo["available_memory"] = static_cast<float>(memInfo.ullAvailPhys / (1024 * 1024 * 1024));

        // Get system architecture
        sysinfo["architecture"] = "x64";

        // Get MAC address
        sysinfo["mac_address"] = getMacAddress();

        // Get username
        char username[UNLEN + 1];
        DWORD username_len = UNLEN + 1;
        GetUserNameA(username, &username_len);
        sysinfo["username"] = username;

        system_info["system_info"] = sysinfo;

        // Return JSON dump
        return system_info.dump(4);
    }


    bool startReverseShell(const std::string& ip, const int port)
    {
        // Create a reverse shell connection to the specified IP and port
        // For demonstration purposes, we will just print the IP and port
        std::cout << "Starting reverse shell to " << ip << ":" << port << std::endl;
        std::string command = "revshell.exe " + ip + " " + std::to_string(port);
        int result = system(command.c_str());
        std::cout << "Reverse shell started" << std::endl;
        return true;
    }

}
