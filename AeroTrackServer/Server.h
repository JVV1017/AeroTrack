// =============================================================================
// Server.h — AeroTrack Ground Control server application
// =============================================================================
// Requirements: REQ-SVR-010 (handshake), REQ-SVR-030 (SM per flight),
//               REQ-SVR-040 (handoff via HandoffManager),
//               REQ-SVR-050 (file transfer via FileTransfer),
//               REQ-SVR-070 (packet logging via Logger)
//
// Standard:     MISRA C++ — no raw new/delete, U suffix, fixed-width types
//               Winsock calls confined to RUDP.cpp (MISRA Deviation 3)
//               Stream I/O confined to Logger and ServerUI (MISRA Deviation 2)
//
// Design:
//   Server owns all subsystems: RUDP, Logger, FlightRegistry, HandoffManager.
//   The main loop is single-threaded using non-blocking RUDP::Receive with
//   a short timeout, interleaved with periodic checks (contact timeout,
//   handoff timeout). No threads, no mutexes.
//
//   Packet dispatch:
//     CONNECT         → HandleConnect()        → 3-step handshake
//     CONNECT_CONFIRM → HandleConnectConfirm() → register flight, SM→CONNECTED
//     POSITION_REPORT → HandlePositionReport()  → update registry, check handoff
//     HANDOFF_ACK     → HandleHandoffAck()      → complete handoff + file transfer
//     HEARTBEAT       → HandleHeartbeat()       → reset contact timer
//     DISCONNECT      → HandleDisconnect()      → remove from registry
// =============================================================================
#pragma once

#include "FlightRegistry.h"
#include "HandoffManager.h"
#include "FileTransfer.h"
#include "ServerUI.h"
#include "StateMachine.h"
#include "RUDP.h"
#include "Logger.h"
#include "Packet.h"
#include "PacketTypes.h"
#include "Config.h"

#include <cstdint>
#include <string>
#include <map>

namespace AeroTrack {

    // ---------------------------------------------------------------------------
    // PendingConnect — tracks a client mid-handshake (between CONNECT and CONFIRM)
    // ---------------------------------------------------------------------------
    struct PendingConnect {
        uint32_t    flightId{ 0U };
        std::string callsign;
        Endpoint    clientEndpoint;
        uint32_t    assignedSector{ 0U };
        uint32_t    sessionToken{ 0U };
    };

    // ---------------------------------------------------------------------------
    // Server — Ground Control station application
    // ---------------------------------------------------------------------------
    class Server {
    public:
        Server();
        ~Server();

        // Non-copyable: owns RUDP socket and Logger file handle
        Server(const Server&) = delete;
        Server& operator=(const Server&) = delete;

        // -----------------------------------------------------------------------
        // Initialisation — call before Run()
        // -----------------------------------------------------------------------
        // 1. Initialises Winsock via RUDP
        // 2. Binds to SERVER_PORT
        // 3. Opens server log file
        // 4. Configures sector definitions for HandoffManager
        // 5. Pre-loads radar image for file transfer
        //
        // Returns false if any step fails (caller should abort).
        // -----------------------------------------------------------------------
        bool Init();

        // -----------------------------------------------------------------------
        // Main loop — blocks until Shutdown() is called or fatal error
        // -----------------------------------------------------------------------
        // Single-threaded event loop:
        //   1. RUDP::Receive with short timeout
        //   2. Dispatch packet to handler
        //   3. Check contact timeouts (REQ-STM-040)
        //   4. Check handoff timeouts (REQ-SVR-040)
        //   5. Update UI (if attached)
        // -----------------------------------------------------------------------
        void Run();

        // -----------------------------------------------------------------------
        // Signal the main loop to exit gracefully
        // -----------------------------------------------------------------------
        void Shutdown();

        // -----------------------------------------------------------------------
        // Subsystem accessors — for ServerUI to read state
        // -----------------------------------------------------------------------
        const FlightRegistry& GetFlightRegistry() const;
        const HandoffManager& GetHandoffManager() const;
        bool                   IsRunning() const;

    private:
        // -----------------------------------------------------------------------
        // Packet handlers (dispatched from main loop)
        // -----------------------------------------------------------------------

        // REQ-SVR-010: Step 1 — receive CONNECT, send CONNECT_ACK
        void HandleConnect(const Packet& packet, const Endpoint& sender);

        // REQ-SVR-010: Step 3 — receive CONNECT_CONFIRM, register flight
        void HandleConnectConfirm(const Packet& packet, const Endpoint& sender);

        // REQ-SVR-020/040: Update position, check for handoff trigger
        void HandlePositionReport(const Packet& packet, const Endpoint& sender);

        // REQ-SVR-040: Process handoff acknowledgement from aircraft
        void HandleHandoffAck(const Packet& packet, const Endpoint& sender);

        // REQ-STM-040: Reset contact timer for this flight
        void HandleHeartbeat(const Packet& packet, const Endpoint& sender);

        // Remove flight from registry
        void HandleDisconnect(const Packet& packet, const Endpoint& sender);

        // -----------------------------------------------------------------------
        // Periodic checks (called each iteration of main loop)
        // -----------------------------------------------------------------------
        void CheckContactTimeouts();
        void CheckHandoffTimeouts();

        // -----------------------------------------------------------------------
        // File transfer execution — sends all chunks for a flight
        // -----------------------------------------------------------------------
        void ExecuteFileTransfer(uint32_t flightId, const std::string& imagePath);

        // -----------------------------------------------------------------------
        // Helpers
        // -----------------------------------------------------------------------

        // Build and send a CONNECT_ACK packet
        void SendConnectAck(uint32_t flightId, uint32_t sectorId,
            uint32_t sessionToken, const Endpoint& dest);

        // Build and send a HANDOFF_INSTRUCT packet
        void SendHandoffInstruct(uint32_t flightId, uint32_t newSectorId,
            const Endpoint& dest);

        // Log a TransitionResult via the Logger
        void LogTransition(const TransitionResult& tr, uint32_t flightId);

        // -----------------------------------------------------------------------
        // Subsystems (owned)
        // -----------------------------------------------------------------------
        RUDP              m_rudp;
        Logger            m_logger;
        FlightRegistry    m_registry;
        HandoffManager    m_handoffManager;
        ServerUI          m_ui;

        // -----------------------------------------------------------------------
        // Pending handshakes (between CONNECT and CONNECT_CONFIRM)
        // -----------------------------------------------------------------------
        // Keyed by flight_id. Entry created on CONNECT, consumed on CONFIRM.
        std::map<uint32_t, PendingConnect> m_pendingConnects;

        // -----------------------------------------------------------------------
        // Server state
        // -----------------------------------------------------------------------
        bool m_running;
        uint32_t m_defaultSectorId;  // Initial sector assigned to new connections
    };

} // namespace AeroTrack