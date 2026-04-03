// =============================================================================
// main.cpp — AeroTrack Ground Control Server entry point
// =============================================================================
// Requirements: REQ-SYS-001 (server application)
// Standard:     MISRA C++ — MISRA Deviation 2: cout used for startup messages
//               MISRA Deviation DEV-006: std::signal() used for Ctrl+C handling
// =============================================================================

#include "Server.h"

// MISRA Deviation 2: Stream I/O for startup/shutdown console messages
#include <iostream>
#include <csignal>

// ---------------------------------------------------------------------------
// Anonymous namespace — satisfies MISRA 7-3-1 (V2575):
// "The global namespace shall only contain main, namespace declarations,
//  and extern C declarations."
// g_serverPtr and SignalHandler are file-local; anonymous namespace replaces
// the prior 'static' linkage specifier and keeps them out of the global
// namespace.
// ---------------------------------------------------------------------------
namespace {

    AeroTrack::Server* g_serverPtr = nullptr;

    // -----------------------------------------------------------------------
    // Signal handler — Ctrl+C graceful shutdown
    // MISRA DEV-006: std::signal() use documented in deviation log.
    // Handler calls only Server::Shutdown() which sets a single bool flag —
    // no I/O, no allocation, no non-reentrant functions.
    // -----------------------------------------------------------------------
    void SignalHandler(int sig)
    {
        if ((sig == SIGINT) && (g_serverPtr != nullptr)) {
            std::cout << "\n[AeroTrack] Shutdown signal received. Stopping server...\n";
            g_serverPtr->Shutdown();
        }
    }

} // namespace

// ---------------------------------------------------------------------------
// Entry point
// MISRA 6-6-5 (V2506): Single return at function end via exitCode variable.
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
    // MISRA 0-1-7 (V2547): Return value (previous handler) explicitly discarded.
    // MISRA DEV-006: std::signal() deviation — see deviation log.
    (void)std::signal(SIGINT, SignalHandler);

    int exitCode = 0;

    std::cout << "[AeroTrack] Initializing server...\n";
    if (!server.Init()) {
        std::cerr << "[AeroTrack] ERROR: Server initialization failed.\n";
        exitCode = 1;
    }
    else {
        std::cout << "[AeroTrack] Server initialized successfully.\n";
        std::cout << "[AeroTrack] Listening on " << AeroTrack::SERVER_IP
            << ":" << AeroTrack::SERVER_PORT << "\n";
        std::cout << "[AeroTrack] Press Ctrl+C to stop.\n\n";

        // Run the main event loop (blocks until Shutdown() is called)
        server.Run();

        std::cout << "\n[AeroTrack] Server stopped.\n";
        g_serverPtr = nullptr;
    }

    return exitCode;
}