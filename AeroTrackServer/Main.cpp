// =============================================================================
// main.cpp — AeroTrack Ground Control Server entry point
// =============================================================================
// Requirements: REQ-SYS-001 (server application)
// Standard:     MISRA C++ — MISRA Deviation 2: cout used for startup messages
// =============================================================================

#include "Server.h"

// MISRA Deviation 2: Stream I/O for startup/shutdown console messages
#include <iostream>
#include <csignal>

// ---------------------------------------------------------------------------
// Global server pointer for signal handler (single-instance application)
// ---------------------------------------------------------------------------
static AeroTrack::Server* g_serverPtr = nullptr;

// ---------------------------------------------------------------------------
// Signal handler — Ctrl+C graceful shutdown
// ---------------------------------------------------------------------------
void SignalHandler(int signal)
{
    if ((signal == SIGINT) && (g_serverPtr != nullptr)) {
        std::cout << "\n[AeroTrack] Shutdown signal received. Stopping server...\n";
        g_serverPtr->Shutdown();
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main()
{
    std::cout << "========================================\n";
    std::cout << "  AeroTrack Ground Control Server\n";
    std::cout << "  CSCN74000 — Software Safety & Reliability\n";
    std::cout << "  DAL-C | MISRA C++ | DO-178C\n";
    std::cout << "========================================\n\n";

    AeroTrack::Server server;
    g_serverPtr = &server;

    // Register Ctrl+C handler for graceful shutdown
    std::signal(SIGINT, SignalHandler);

    // Initialise all subsystems
    std::cout << "[AeroTrack] Initializing server...\n";
    if (!server.Init()) {
        std::cerr << "[AeroTrack] ERROR: Server initialization failed.\n";
        return 1;
    }

    std::cout << "[AeroTrack] Server initialized successfully.\n";
    std::cout << "[AeroTrack] Listening on " << AeroTrack::SERVER_IP
        << ":" << AeroTrack::SERVER_PORT << "\n";
    std::cout << "[AeroTrack] Press Ctrl+C to stop.\n\n";

    // Run the main event loop (blocks until Shutdown() is called)
    server.Run();

    std::cout << "\n[AeroTrack] Server stopped.\n";
    g_serverPtr = nullptr;

    return 0;
}