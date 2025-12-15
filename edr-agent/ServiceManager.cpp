// ============================================================================
// ServiceManager.cpp - Windows Service Control Manager Integration
// ============================================================================
// Implementation of Windows Service lifecycle management.
// 
// ARCHITECTURE:
// 1. Install() - Registers service with SCM using CreateService()
// 2. Uninstall() - Removes service using DeleteService()
// 3. Run() - Called by SCM, sets up ServiceMain
// 4. ServiceMain() - Initializes agent and runs main loop
// 5. ServiceCtrlHandler() - Handles STOP/PAUSE/CONTINUE signals
// ============================================================================

#include "ServiceManager.hpp"
#include "Logger.hpp"
#include "AgentId.hpp"            // For g_agentId initialization in service mode
#include "ConfigReader.hpp"       // For enrollment check
#include "EnrollmentClient.hpp"   // For enrollment in service mode
#include <iostream>
#include <sstream>
#include <filesystem>             // For std::filesystem::current_path()

// Forward declaration of agent main function (defined in EdrAgent.cpp)
extern int runAgent(bool serviceMode);
extern void requestAgentShutdown();

// ============================================================================
// Static Member Initialization
// ============================================================================
SERVICE_STATUS_HANDLE ServiceManager::s_StatusHandle = NULL;
SERVICE_STATUS ServiceManager::s_Status = {};
HANDLE ServiceManager::s_StopEvent = NULL;
DWORD ServiceManager::s_CheckPoint = 0;

// ============================================================================
// Install Service
// ============================================================================
// Registers the EDR Agent as a Windows Service with:
// - Auto-start on boot
// - LocalSystem account (highest privileges)
// - Failure recovery options
// ============================================================================
bool ServiceManager::Install() {
    LOG_SVC_INFO("Installing service...");
    
    // Step 1: Get path to current executable
    // We append --service so SCM knows to run in service mode
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0) {
        LOG_SVC_ERROR("Failed to get executable path. Error: " + std::to_string(GetLastError()));
        return false;
    }
    
    // Build command line: "C:\path\to\edr-agent.exe" --service
    std::wstring servicePath = L"\"";
    servicePath += exePath;
    servicePath += L"\" --service";
    
    LOG_SVC_INFO("Service binary: " + std::string(servicePath.begin(), servicePath.end()));
    
    // Step 2: Open Service Control Manager
    // SC_MANAGER_CREATE_SERVICE allows us to create new services
    SC_HANDLE hSCManager = OpenSCManagerW(
        NULL,                      // Local computer
        NULL,                      // Default database (SERVICES_ACTIVE_DATABASE)
        SC_MANAGER_CREATE_SERVICE  // Access right to create services
    );
    
    if (hSCManager == NULL) {
        DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) {
            LOG_SVC_ERROR("Access denied. Run as Administrator.");
        } else {
            LOG_SVC_ERROR("OpenSCManager failed. Error: " + std::to_string(error));
        }
        return false;
    }
    
    // Step 3: Create the service
    SC_HANDLE hService = CreateServiceW(
        hSCManager,                     // SCM handle
        SERVICE_NAME,                   // Service name (internal)
        SERVICE_DISPLAY_NAME,           // Display name (UI)
        SERVICE_ALL_ACCESS,             // Full access
        SERVICE_WIN32_OWN_PROCESS,      // Runs in its own process
        SERVICE_AUTO_START,             // Start automatically on boot
        SERVICE_ERROR_NORMAL,           // Log errors to event log
        servicePath.c_str(),            // Path to executable with args
        NULL,                           // No load ordering group
        NULL,                           // No tag identifier
        NULL,                           // No dependencies
        NULL,                           // LocalSystem account
        NULL                            // No password needed for LocalSystem
    );
    
    if (hService == NULL) {
        DWORD error = GetLastError();
        CloseServiceHandle(hSCManager);
        
        if (error == ERROR_SERVICE_EXISTS) {
            LOG_SVC_WARN("Service already exists. Use --uninstall first if you want to reinstall.");
            return false;
        }
        
        LOG_SVC_ERROR("CreateService failed. Error: " + std::to_string(error));
        return false;
    }
    
    // Step 4: Set service description
    SERVICE_DESCRIPTIONW desc = {};
    desc.lpDescription = const_cast<LPWSTR>(SERVICE_DESCRIPTION);
    ChangeServiceConfig2W(hService, SERVICE_CONFIG_DESCRIPTION, &desc);
    
    // Step 5: Configure failure recovery (restart on crash)
    SC_ACTION actions[3] = {
        { SC_ACTION_RESTART, 60000 },   // First failure: restart after 60s
        { SC_ACTION_RESTART, 60000 },   // Second failure: restart after 60s
        { SC_ACTION_RESTART, 60000 }    // Subsequent failures: restart after 60s
    };
    
    SERVICE_FAILURE_ACTIONSW failureActions = {};
    failureActions.dwResetPeriod = 86400;  // Reset failure count after 24 hours
    failureActions.lpRebootMsg = NULL;
    failureActions.lpCommand = NULL;
    failureActions.cActions = 3;
    failureActions.lpsaActions = actions;
    
    ChangeServiceConfig2W(hService, SERVICE_CONFIG_FAILURE_ACTIONS, &failureActions);
    
    // Cleanup
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    
    LOG_SVC_INFO("Service installed successfully!");
    LOG_SVC_INFO("Start with: net start EDRAgent");
    
    return true;
}

// ============================================================================
// Uninstall Service
// ============================================================================
// Stops the service if running, then removes it from SCM
// ============================================================================
bool ServiceManager::Uninstall() {
    LOG_SVC_INFO("Uninstalling service...");
    
    // Step 1: Open SCM
    SC_HANDLE hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (hSCManager == NULL) {
        DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED) {
            LOG_SVC_ERROR("Access denied. Run as Administrator.");
        } else {
            LOG_SVC_ERROR("OpenSCManager failed. Error: " + std::to_string(error));
        }
        return false;
    }
    
    // Step 2: Open the service
    SC_HANDLE hService = OpenServiceW(hSCManager, SERVICE_NAME, SERVICE_ALL_ACCESS);
    if (hService == NULL) {
        DWORD error = GetLastError();
        CloseServiceHandle(hSCManager);
        
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) {
            LOG_SVC_WARN("Service is not installed.");
            return true;  // Not installed = success for uninstall
        }
        
        LOG_SVC_ERROR("OpenService failed. Error: " + std::to_string(error));
        return false;
    }
    
    // Step 3: Stop service if running
    SERVICE_STATUS status;
    if (ControlService(hService, SERVICE_CONTROL_STOP, &status)) {
        LOG_SVC_INFO("Stopping service...");
        
        // Wait for service to stop (max 30 seconds)
        int attempts = 0;
        while (status.dwCurrentState != SERVICE_STOPPED && attempts < 30) {
            Sleep(1000);
            if (!QueryServiceStatus(hService, &status)) break;
            attempts++;
        }
        
        if (status.dwCurrentState == SERVICE_STOPPED) {
            LOG_SVC_INFO("Service stopped.");
        } else {
            LOG_SVC_WARN("Service did not stop cleanly.");
        }
    }
    
    // Step 4: Delete the service
    if (!DeleteService(hService)) {
        DWORD error = GetLastError();
        CloseServiceHandle(hService);
        CloseServiceHandle(hSCManager);
        
        if (error == ERROR_SERVICE_MARKED_FOR_DELETE) {
            LOG_SVC_WARN("Service already marked for deletion. It will be removed after reboot.");
            return true;
        }
        
        LOG_SVC_ERROR("DeleteService failed. Error: " + std::to_string(error));
        return false;
    }
    
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    
    LOG_SVC_INFO("Service uninstalled successfully!");
    return true;
}

// ============================================================================
// Run (Service Mode Entry Point)
// ============================================================================
// Called when the process is started with --service flag
// This function connects to SCM and registers our ServiceMain
// ============================================================================
void ServiceManager::Run() {
    LOG_SVC_INFO("Starting in service mode...");
    
    // Service table maps service names to their entry points
    // A process can host multiple services, but we only have one
    SERVICE_TABLE_ENTRYW ServiceTable[] = {
        { const_cast<LPWSTR>(SERVICE_NAME), ServiceMain },
        { NULL, NULL }  // Table must be NULL-terminated
    };
    
    // StartServiceCtrlDispatcher connects to SCM and waits for service start
    // It blocks until all services in the table have stopped
    if (!StartServiceCtrlDispatcherW(ServiceTable)) {
        DWORD error = GetLastError();
        
        if (error == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            // This error means we're not running as a service
            LOG_SVC_ERROR("Not running as a service. Use --console for interactive mode.");
        } else {
            LOG_SVC_ERROR("StartServiceCtrlDispatcher failed. Error: " + std::to_string(error));
        }
    }
}

// ============================================================================
// ServiceMain - Called by SCM when service starts
// ============================================================================
// This is the actual service entry point. SCM calls this after we call
// StartServiceCtrlDispatcher. Here we:
// 1. Register our control handler
// 2. Report SERVICE_START_PENDING
// 3. Initialize the agent
// 4. Report SERVICE_RUNNING
// 5. Run the main agent loop
// 6. Report SERVICE_STOPPED when done
// ============================================================================
void WINAPI ServiceManager::ServiceMain(DWORD argc, LPWSTR* argv) {
    // Suppress unused parameter warnings
    (void)argc;
    (void)argv;
    
    LOG_SVC_INFO("ServiceMain entered");
    
    // Step 0: Set working directory to executable location
    // Services start from C:\Windows\System32 by default, but our config.json
    // is next to the executable. We need to change to that directory.
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) != 0) {
        // Find last backslash to get directory
        wchar_t* lastSlash = wcsrchr(exePath, L'\\');
        if (lastSlash != NULL) {
            *lastSlash = L'\0';  // Truncate to get directory path
            SetCurrentDirectoryW(exePath);
            LOG_SVC_INFO("Working directory set to: " + std::string(exePath, exePath + wcslen(exePath)));
        }
    }
    
    // Step 1: Register control handler
    // This function will receive STOP/PAUSE/etc signals from SCM
    s_StatusHandle = RegisterServiceCtrlHandlerW(SERVICE_NAME, ServiceCtrlHandler);
    if (s_StatusHandle == NULL) {
        LOG_SVC_ERROR("RegisterServiceCtrlHandler failed. Error: " + std::to_string(GetLastError()));
        return;
    }
    
    // Step 2: Create stop event
    // This event is signaled by ServiceCtrlHandler when STOP is received
    s_StopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (s_StopEvent == NULL) {
        LOG_SVC_ERROR("CreateEvent failed. Error: " + std::to_string(GetLastError()));
        ReportStatus(SERVICE_STOPPED, GetLastError(), 0);
        return;
    }
    
    // Step 3: Report that we're starting
    // 3000ms wait hint tells SCM we expect to finish starting within 3 seconds
    ReportStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
    
    LOG_SVC_INFO("Initializing agent...");
    
    // Step 3.5: Initialize Agent ID (CRITICAL - must happen before enrollment)
    // This is the same initialization that happens in console mode
    extern std::string g_agentId;
    std::filesystem::path configDir = std::filesystem::current_path();
    g_agentId = AgentId::getOrCreate(configDir);
    LOG_SVC_INFO("Agent ID initialized: " + g_agentId);
    
    // Step 3.6: Check if enrollment is needed
    // First-time run requires enrollment to get identity_token
    ConfigReader enrollConfig("config.json");
    if (enrollConfig.needsEnrollment()) {
        LOG_SVC_INFO("Agent not enrolled - starting enrollment process...");
        ReportStatus(SERVICE_START_PENDING, NO_ERROR, 10000);  // Give enrollment 10s
        
        if (!EnrollmentClient::enrollAgent(enrollConfig)) {
            LOG_SVC_ERROR("Agent enrollment failed. Cannot continue.");
            ReportStatus(SERVICE_STOPPED, ERROR_SERVICE_SPECIFIC_ERROR, 0);
            return;
        }
        LOG_SVC_INFO("Enrollment successful - continuing startup");
    }
    
    // Step 4: Report running status
    ReportStatus(SERVICE_RUNNING, NO_ERROR, 0);
    LOG_SVC_INFO("Service is now running");
    
    // Step 5: Run the agent main loop
    // This blocks until shutdown is requested
    // runAgent() should check for shutdown signal and exit cleanly
    int result = runAgent(true);  // true = service mode
    
    // Step 6: Cleanup
    if (s_StopEvent) {
        CloseHandle(s_StopEvent);
        s_StopEvent = NULL;
    }
    
    // Step 7: Report stopped
    ReportStatus(SERVICE_STOPPED, result, 0);
    LOG_SVC_INFO("Service stopped with code: " + std::to_string(result));
}

// ============================================================================
// ServiceCtrlHandler - Receives control signals from SCM
// ============================================================================
// Called by SCM when:
// - Admin runs "net stop EDRAgent"
// - System is shutting down
// - Admin pauses/continues the service
// ============================================================================
void WINAPI ServiceManager::ServiceCtrlHandler(DWORD ctrlCode) {
    switch (ctrlCode) {
        case SERVICE_CONTROL_STOP:
            LOG_SVC_INFO("Received STOP signal");
            ReportStatus(SERVICE_STOP_PENDING, NO_ERROR, 30000);  // 30s to stop
            
            // Signal the agent to shutdown
            requestAgentShutdown();
            
            // Signal the stop event (for any code waiting on it)
            if (s_StopEvent) {
                SetEvent(s_StopEvent);
            }
            break;
            
        case SERVICE_CONTROL_SHUTDOWN:
            LOG_SVC_INFO("Received SHUTDOWN signal (system is shutting down)");
            ReportStatus(SERVICE_STOP_PENDING, NO_ERROR, 5000);  // 5s for shutdown
            requestAgentShutdown();
            if (s_StopEvent) {
                SetEvent(s_StopEvent);
            }
            break;
            
        case SERVICE_CONTROL_INTERROGATE:
            // Just report current status (already done below)
            break;
            
        case SERVICE_CONTROL_PAUSE:
            LOG_SVC_INFO("Received PAUSE signal (not implemented)");
            break;
            
        case SERVICE_CONTROL_CONTINUE:
            LOG_SVC_INFO("Received CONTINUE signal (not implemented)");
            break;
            
        default:
            LOG_SVC_WARN("Received unknown control code: " + std::to_string(ctrlCode));
            break;
    }
    
    // Always report current status after handling control
    ReportStatus(s_Status.dwCurrentState, NO_ERROR, 0);
}

// ============================================================================
// ReportStatus - Report current state to SCM
// ============================================================================
// Must be called:
// - During startup (SERVICE_START_PENDING → SERVICE_RUNNING)
// - During shutdown (SERVICE_STOP_PENDING → SERVICE_STOPPED)
// - Periodically during long operations (increment checkpoint)
// ============================================================================
void ServiceManager::ReportStatus(DWORD state, DWORD exitCode, DWORD waitHint) {
    // Initialize status structure
    s_Status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    s_Status.dwCurrentState = state;
    s_Status.dwWin32ExitCode = exitCode;
    s_Status.dwWaitHint = waitHint;
    
    // Set which controls we accept based on current state
    // During startup, we can't accept STOP (we're not fully running yet)
    if (state == SERVICE_START_PENDING) {
        s_Status.dwControlsAccepted = 0;
    } else if (state == SERVICE_STOP_PENDING) {
        s_Status.dwControlsAccepted = 0;
    } else {
        // When running, accept STOP and SHUTDOWN
        s_Status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    }
    
    // Checkpoint management
    // - Reset to 0 when in a stable state (RUNNING, STOPPED)
    // - Increment during pending states to show progress
    if (state == SERVICE_RUNNING || state == SERVICE_STOPPED) {
        s_CheckPoint = 0;
    } else {
        s_CheckPoint++;
    }
    s_Status.dwCheckPoint = s_CheckPoint;
    
    // Report to SCM
    if (s_StatusHandle) {
        SetServiceStatus(s_StatusHandle, &s_Status);
    }
}

// ============================================================================
// IsInstalled - Check if service is registered
// ============================================================================
bool ServiceManager::IsInstalled() {
    SC_HANDLE hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (hSCManager == NULL) return false;
    
    SC_HANDLE hService = OpenServiceW(hSCManager, SERVICE_NAME, SERVICE_QUERY_STATUS);
    bool installed = (hService != NULL);
    
    if (hService) CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    
    return installed;
}

// ============================================================================
// IsRunning - Check if service is currently running
// ============================================================================
bool ServiceManager::IsRunning() {
    SC_HANDLE hSCManager = OpenSCManagerW(NULL, NULL, SC_MANAGER_CONNECT);
    if (hSCManager == NULL) return false;
    
    SC_HANDLE hService = OpenServiceW(hSCManager, SERVICE_NAME, SERVICE_QUERY_STATUS);
    if (hService == NULL) {
        CloseServiceHandle(hSCManager);
        return false;
    }
    
    SERVICE_STATUS status;
    bool running = false;
    if (QueryServiceStatus(hService, &status)) {
        running = (status.dwCurrentState == SERVICE_RUNNING);
    }
    
    CloseServiceHandle(hService);
    CloseServiceHandle(hSCManager);
    
    return running;
}
