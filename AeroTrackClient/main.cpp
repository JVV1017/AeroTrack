// AeroTrackClient — main.cpp
// REQ-CLT-070: Ctrl+C triggers Stop() which sends DISCONNECT

// MISRA Rule 16-2-1: #pragma comment documented — links Winsock library
#pragma comment(lib, "Ws2_32.lib")

// Winsock must come first to avoid conflicts with Windows.h
#include <winsock2.h>
#include <ws2tcpip.h>

// winsock2.h / windows.h defines ERROR as a macro which conflicts with
// AeroTrack::PacketType::ERROR (PacketTypes.h line ~42).
// Undefine it here before including any AeroTrack headers.
#ifdef ERROR
#undef ERROR
#endif

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <string>

#include "Client.h"

// ---------------------------------------------------------------------------
// Graceful shutdown on Ctrl+C
// ---------------------------------------------------------------------------
// MISRA Fix [V2575]: SignalHandler moved into the anonymous namespace.
// Anonymous namespace gives internal linkage (same effect as 'static') and
// satisfies MISRA Rule 7-3-1: the global namespace shall only contain main,
// namespace declarations, and extern "C" declarations.
namespace {
    AeroTrack::Client* g_client = nullptr;

    static void SignalHandler(int sig) noexcept {
        (void)sig;
        if (g_client != nullptr) {
            g_client->Stop();  // REQ-CLT-070: sends DISCONNECT
        }
    }
}

// ---------------------------------------------------------------------------
// main
// Usage: AeroTrackClient.exe <flightId> <callsign>
// Example: AeroTrackClient.exe 1001 AC123
//          AeroTrackClient.exe 2002 UA442
// Defaults to 1001 / AC123 if no args provided.
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // Initialize Winsock 2.2
    WSADATA wsaData;
    int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaResult != 0) {
        // MISRA Fix [V2547]: fprintf return value explicitly discarded.
        // Return value is the character count written; failure here is
        // non-recoverable (stderr unavailable), so discard is intentional.
        (void)std::fprintf(stderr,
            "AeroTrackClient: WSAStartup failed (%d)\n", wsaResult);
        return EXIT_FAILURE;
    }

    // Register Ctrl+C handler (REQ-CLT-070)
    // DEV-6: std::signal use is a documented deviation (MISRA 18-7-1).
    // MISRA Fix [V2547]: signal() returns the previous handler; discarding
    // it is intentional — we have no prior handler to restore.
    (void)std::signal(SIGINT, SignalHandler);

    {
        AeroTrack::Client client;
        g_client = &client;

        // Default identity — overridden by command-line args
        uint32_t    flightId = 1001U;
        std::string callsign = "AC123";

        // MISRA Fix [V2508]: std::atoi replaced with std::strtoul.
        // atoi is banned (MISRA Rule 18-0-5): undefined behaviour on overflow
        // and returns 0 for invalid input with no error indication.
        // std::strtoul is in <cstdlib>, handles unsigned conversion, and is
        // not on the MISRA banned-function list.
        // MISRA Fix [V2516]: terminal else added to prove the zero-arg case
        // (use defaults) is intentional, satisfying MISRA Rule 6-4-2.
        if (argc >= 3) {
            flightId = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
            callsign = std::string(argv[2]);
        }
        else if (argc >= 2) {
            flightId = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
        }
        else {
            /* No arguments supplied — default values (1001 / AC123) remain. */
        }

        // MISRA Fix [V2547]: fprintf return value explicitly discarded.
        (void)std::fprintf(stdout,
            "AeroTrackClient: Flight %u (%s) connecting to 127.0.0.1:27015...\n",
            flightId, callsign.c_str());

        if (!client.Init(flightId, callsign)) {
            // MISRA Fix [V2547]: fprintf and WSACleanup return values
            // explicitly discarded throughout — failures here are inside an
            // already-failing shutdown path; no further action is possible.
            (void)std::fprintf(stderr, "AeroTrackClient: Init failed\n");
            (void)WSACleanup();
            return EXIT_FAILURE;
        }

        // REQ-CLT-010: 3-step handshake
        if (!client.Connect()) {
            (void)std::fprintf(stderr,
                "AeroTrackClient: Handshake failed — is the server running?\n");
            (void)WSACleanup();
            return EXIT_FAILURE;
        }

        (void)std::fprintf(stdout,
            "AeroTrackClient: Connected. Starting flight terminal.\n");

        // REQ-CLT-020 to 050: Main loop
        client.Run();

        g_client = nullptr;
    }

    (void)WSACleanup();
    (void)std::fprintf(stdout, "AeroTrackClient: Shutdown complete.\n");
    return EXIT_SUCCESS;
}