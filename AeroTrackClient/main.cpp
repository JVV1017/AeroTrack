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
namespace {
    AeroTrack::Client* g_client = nullptr;
}

static void SignalHandler(int sig) noexcept {
    (void)sig;
    if (g_client != nullptr) {
        g_client->Stop();  // REQ-CLT-070: sends DISCONNECT
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
        std::fprintf(stderr,
            "AeroTrackClient: WSAStartup failed (%d)\n", wsaResult);
        return EXIT_FAILURE;
    }

    // Register Ctrl+C handler (REQ-CLT-070)
    std::signal(SIGINT, SignalHandler);

    {
        AeroTrack::Client client;
        g_client = &client;

        // Default identity — overridden by command-line args
        uint32_t    flightId = 1001U;
        std::string callsign = "AC123";

        if (argc >= 3) {
            flightId = static_cast<uint32_t>(std::atoi(argv[1]));
            callsign = std::string(argv[2]);
        }
        else if (argc >= 2) {
            flightId = static_cast<uint32_t>(std::atoi(argv[1]));
        }

        std::fprintf(stdout,
            "AeroTrackClient: Flight %u (%s) connecting to 127.0.0.1:27015...\n",
            flightId, callsign.c_str());

        if (!client.Init(flightId, callsign)) {
            std::fprintf(stderr, "AeroTrackClient: Init failed\n");
            WSACleanup();
            return EXIT_FAILURE;
        }

        // REQ-CLT-010: 3-step handshake
        if (!client.Connect()) {
            std::fprintf(stderr,
                "AeroTrackClient: Handshake failed — is the server running?\n");
            WSACleanup();
            return EXIT_FAILURE;
        }

        std::fprintf(stdout,
            "AeroTrackClient: Connected. Starting flight terminal.\n");

        // REQ-CLT-020 to 050: Main loop
        client.Run();

        g_client = nullptr;
    }

    WSACleanup();
    std::fprintf(stdout, "AeroTrackClient: Shutdown complete.\n");
    return EXIT_SUCCESS;
}