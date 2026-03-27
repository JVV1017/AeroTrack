// =============================================================================
// Server.cpp — AeroTrack Ground Control server implementation
// =============================================================================
// Requirements: REQ-SVR-010 to 070, REQ-STM-010 to 040
// Standard:     MISRA C++ compliant (see Server.h header comment)
// =============================================================================

#include "Server.h"

#include <cstring>   // std::memcpy
#include <chrono>

namespace AeroTrack {

    // ---------------------------------------------------------------------------
    // Constructor / Destructor
    // ---------------------------------------------------------------------------
    Server::Server()
        : m_rudp()
        , m_logger()
        , m_registry()
        , m_handoffManager()
        , m_ui()
        , m_pendingConnects()
        , m_running(false)
        , m_defaultSectorId(1U)   // New connections start in sector 1
    {
    }

    Server::~Server()
    {
        Shutdown();
    }

    // ---------------------------------------------------------------------------
    // Init — set up all subsystems
    // ---------------------------------------------------------------------------
    bool Server::Init()
    {
        // 1. Initialise RUDP (Winsock + UDP socket)
        if (!m_rudp.Init()) {
            return false;
        }

        // 2. Bind to server port
        if (!m_rudp.Bind(SERVER_PORT)) {
            m_rudp.Shutdown();
            return false;
        }

        // 3. Open server log file (REQ-LOG-030)
        if (!m_logger.Init("aerotrack_server.log")) {
            m_rudp.Shutdown();
            return false;
        }

        // 4. Attach logger to RUDP for retransmission logging (REQ-COM-050)
        m_rudp.SetLogger(&m_logger);

        // 5. Configure sectors for HandoffManager (REQ-SVR-040)
        //    Two latitude-band sectors for simulation demo
        SectorDefinition south;
        south.sectorId = 1U;
        south.sectorName = "SECTOR-SOUTH";
        south.latMin = 40.0;
        south.latMax = 45.0;
        south.radarImagePath = "test_data/sector_south.jpg";
        m_handoffManager.AddSector(south);

        SectorDefinition north;
        north.sectorId = 2U;
        north.sectorName = "SECTOR-NORTH";
        north.latMin = 45.0;
        north.latMax = 50.0;
        north.radarImagePath = "test_data/sector_north.jpg";
        m_handoffManager.AddSector(north);

        m_logger.LogInfo("AeroTrack Ground Control Server initialized");
        m_logger.LogInfo("Listening on port " + std::to_string(SERVER_PORT));
        m_logger.LogInfo("Sectors configured: SECTOR-SOUTH (40-45 lat), SECTOR-NORTH (45-50 lat)");

        // 6. Attach UI to data sources (REQ-SVR-060)
        m_ui.Attach(&m_registry, &m_handoffManager);
        m_ui.AddEvent("Server initialized on port " + std::to_string(SERVER_PORT));
        m_ui.AddEvent("Sectors: SOUTH (40-45 lat), NORTH (45-50 lat)");

        return true;
    }

    // ---------------------------------------------------------------------------
    // Run — single-threaded main event loop
    // ---------------------------------------------------------------------------
    void Server::Run()
    {
        m_running = true;
        m_logger.LogInfo("Server main loop started");

        while (m_running) {
            // 1. Try to receive a packet (short timeout for responsiveness)
            Packet inPacket;
            Endpoint sender;

            if (m_rudp.Receive(inPacket, sender, SOCKET_RECV_TIMEOUT_MS)) {
                // Log the received packet (REQ-SVR-070)
                m_logger.LogPacket("RX", inPacket, "OK");

                // 2. Dispatch based on packet type
                switch (inPacket.GetType()) {
                case PacketType::CONNECT:
                    HandleConnect(inPacket, sender);
                    break;

                case PacketType::CONNECT_CONFIRM:
                    HandleConnectConfirm(inPacket, sender);
                    break;

                case PacketType::POSITION_REPORT:
                    HandlePositionReport(inPacket, sender);
                    break;

                case PacketType::HANDOFF_ACK:
                    HandleHandoffAck(inPacket, sender);
                    break;

                case PacketType::HEARTBEAT:
                    HandleHeartbeat(inPacket, sender);
                    break;

                case PacketType::DISCONNECT:
                    HandleDisconnect(inPacket, sender);
                    break;

                default:
                    m_logger.LogError("Unknown packet type received: " +
                        inPacket.TypeString());
                    break;
                }
            }
            // Receive returned false = timeout or error, continue loop

            // 3. Periodic checks (run every iteration regardless of packet)
            CheckContactTimeouts();
            CheckHandoffTimeouts();

            // 4. Refresh the dashboard (REQ-SVR-060)
            m_ui.Render();
        }

        m_logger.LogInfo("Server main loop exited");
        m_rudp.Shutdown();
        m_logger.Close();
    }

    // ---------------------------------------------------------------------------
    // Shutdown — signal main loop to stop
    // ---------------------------------------------------------------------------
    void Server::Shutdown()
    {
        m_running = false;
    }

    // ---------------------------------------------------------------------------
    // REQ-SVR-010: HandleConnect — Step 1 of 3-step handshake
    // ---------------------------------------------------------------------------
    // Client sends CONNECT with payload: flight_id (uint32) + callsign (string)
    // Server sends CONNECT_ACK with payload: sector_id (uint32) + session_token (uint32)
    // ---------------------------------------------------------------------------
    void Server::HandleConnect(const Packet& packet, const Endpoint& sender)
    {
        const uint32_t flightId = packet.GetFlightId();
        const auto& payload = packet.GetPayload();

        // Extract callsign from payload (after any leading fields)
        // For simplicity: entire payload is the callsign string
        std::string callsign;
        if (payload.size() > 0U) {
            callsign.assign(payload.begin(), payload.end());
        }
        else {
            callsign = "UNKNOWN-" + std::to_string(flightId);
        }

        m_logger.LogInfo("CONNECT received from flight " + std::to_string(flightId) +
            " (" + callsign + ") at " + sender.ip + ":" +
            std::to_string(sender.port));

        // Check if already connected or pending
        if (m_registry.HasFlight(flightId)) {
            m_logger.LogError("Flight " + std::to_string(flightId) +
                " already registered — rejecting duplicate CONNECT");
            return;
        }

        // Generate a session token via a simple counter
        // (In production this would be cryptographically random)
        static uint32_t nextToken = 1000U;
        const uint32_t sessionToken = nextToken;
        nextToken += 1U;

        // Store pending connection (will be finalised on CONNECT_CONFIRM)
        PendingConnect pending;
        pending.flightId = flightId;
        pending.callsign = callsign;
        pending.clientEndpoint = sender;
        pending.assignedSector = m_defaultSectorId;
        pending.sessionToken = sessionToken;
        m_pendingConnects[flightId] = pending;

        // Send CONNECT_ACK (Step 2)
        SendConnectAck(flightId, m_defaultSectorId, sessionToken, sender);

        m_logger.LogInfo("CONNECT_ACK sent to flight " + std::to_string(flightId) +
            " — sector=" + std::to_string(m_defaultSectorId) +
            ", token=" + std::to_string(sessionToken));
        m_ui.AddEvent("CONNECT from " + callsign + " (FLT:" + std::to_string(flightId) + ")");
    }

    // ---------------------------------------------------------------------------
    // REQ-SVR-010: HandleConnectConfirm — Step 3 of 3-step handshake
    // ---------------------------------------------------------------------------
    void Server::HandleConnectConfirm(const Packet& packet, const Endpoint& sender)
    {
        (void)sender;  // Endpoint already stored in PendingConnect from step 1
        const uint32_t flightId = packet.GetFlightId();

        auto it = m_pendingConnects.find(flightId);
        if (it == m_pendingConnects.end()) {
            m_logger.LogError("CONNECT_CONFIRM for unknown pending flight " +
                std::to_string(flightId));
            return;
        }

        const PendingConnect& pending = it->second;

        // Register the flight in the FlightRegistry (REQ-SVR-020)
        if (!m_registry.RegisterFlight(flightId, pending.callsign,
            pending.clientEndpoint)) {
            m_logger.LogError("Failed to register flight " + std::to_string(flightId));
            m_pendingConnects.erase(it);
            return;
        }

        // Set the sector assignment
        m_registry.UpdateSector(flightId, pending.assignedSector);

        // Transition SM: IDLE → CONNECTED (transition #1)
        FlightRecord* record = m_registry.GetFlight(flightId);
        if (record != nullptr) {
            TransitionResult tr = record->stateMachine.Transition(
                FlightState::CONNECTED,
                "3-step handshake complete (CONNECT_CONFIRM received)"
            );
            LogTransition(tr, flightId);
            record->stateMachine.RecordPacketReceived();
        }

        // Clean up pending entry
        m_pendingConnects.erase(it);

        m_logger.LogInfo("Flight " + std::to_string(flightId) +
            " connected and registered — state=CONNECTED, sector=" +
            std::to_string(pending.assignedSector));
        m_ui.AddEvent("Flight " + std::to_string(flightId) +
            " registered in sector " + std::to_string(pending.assignedSector));
    }

    // ---------------------------------------------------------------------------
    // HandlePositionReport — REQ-SVR-020 + REQ-SVR-040
    // ---------------------------------------------------------------------------
    void Server::HandlePositionReport(const Packet& packet, const Endpoint& sender)
    {
        (void)sender;  // Endpoint already in FlightRecord from registration
        const uint32_t flightId = packet.GetFlightId();
        const auto& payload = packet.GetPayload();

        FlightRecord* record = m_registry.GetFlight(flightId);
        if (record == nullptr) {
            m_logger.LogError("POSITION_REPORT for unregistered flight " +
                std::to_string(flightId));
            return;
        }

        // Deserialize PositionPayload from packet payload
        if (payload.size() >= sizeof(PositionPayload)) {
            PositionPayload pos;
            std::memcpy(&pos, payload.data(), sizeof(PositionPayload));
            m_registry.UpdatePosition(flightId, pos);
        }

        // If in CONNECTED state, transition to TRACKING on first position report (#2)
        if (record->stateMachine.GetCurrentState() == FlightState::CONNECTED) {
            TransitionResult tr = record->stateMachine.Transition(
                FlightState::TRACKING,
                "First POSITION_REPORT received"
            );
            LogTransition(tr, flightId);
        }
        else {
            // Reset contact timer for existing TRACKING flights
            record->stateMachine.RecordPacketReceived();
        }

        // Check for handoff trigger (REQ-SVR-040)
        HandoffAction action = m_handoffManager.CheckForHandoff(m_registry, flightId);
        if (action.type == HandoffActionType::SEND_HANDOFF_INSTRUCT) {
            LogTransition(action.transitionResult, flightId);
            m_ui.AddEvent("HANDOFF initiated: FLT:" + std::to_string(flightId) +
                " sector " + std::to_string(action.fromSectorId) +
                " -> " + std::to_string(action.toSectorId));

            // Send HANDOFF_INSTRUCT to aircraft
            SendHandoffInstruct(flightId, action.toSectorId, record->clientEndpoint);

            // Confirm instruction sent → HANDOFF_INITIATED → HANDOFF_PENDING (#4)
            HandoffAction confirmAction = m_handoffManager.ConfirmInstructSent(
                m_registry, flightId);
            LogTransition(confirmAction.transitionResult, flightId);

            // Start file transfer if indicated (REQ-SVR-050)
            if (confirmAction.type == HandoffActionType::START_FILE_TRANSFER) {
                m_ui.AddEvent("File transfer started to FLT:" + std::to_string(flightId));
                ExecuteFileTransfer(flightId, confirmAction.radarImagePath);
            }
        }
    }

    // ---------------------------------------------------------------------------
    // HandleHandoffAck — REQ-SVR-040 (handoff completion)
    // ---------------------------------------------------------------------------
    void Server::HandleHandoffAck(const Packet& packet, const Endpoint& sender)
    {
        (void)sender;  // Endpoint already in FlightRecord
        const uint32_t flightId = packet.GetFlightId();

        FlightRecord* record = m_registry.GetFlight(flightId);
        if (record == nullptr) {
            m_logger.LogError("HANDOFF_ACK for unregistered flight " +
                std::to_string(flightId));
            return;
        }

        // Process the ACK through HandoffManager
        HandoffAction action = m_handoffManager.ProcessHandoffAck(m_registry, flightId);
        LogTransition(action.transitionResult, flightId);

        if (action.type == HandoffActionType::HANDOFF_COMPLETED) {
            m_logger.LogInfo("Handoff completed for flight " + std::to_string(flightId) +
                " — now in sector " + std::to_string(action.toSectorId));
            m_ui.AddEvent("HANDOFF complete: FLT:" + std::to_string(flightId) +
                " now in sector " + std::to_string(action.toSectorId));
        }
    }

    // ---------------------------------------------------------------------------
    // HandleHeartbeat — REQ-STM-040 (reset contact timer)
    // ---------------------------------------------------------------------------
    void Server::HandleHeartbeat(const Packet& packet, const Endpoint& sender)
    {
        (void)sender;
        const uint32_t flightId = packet.GetFlightId();

        FlightRecord* record = m_registry.GetFlight(flightId);
        if (record != nullptr) {
            record->stateMachine.RecordPacketReceived();
        }
    }

    // ---------------------------------------------------------------------------
    // HandleDisconnect — graceful teardown
    // ---------------------------------------------------------------------------
    void Server::HandleDisconnect(const Packet& packet, const Endpoint& sender)
    {
        (void)sender;
        const uint32_t flightId = packet.GetFlightId();

        m_logger.LogInfo("DISCONNECT received from flight " + std::to_string(flightId));
        m_ui.AddEvent("DISCONNECT: FLT:" + std::to_string(flightId));

        m_registry.RemoveFlight(flightId);
    }

    // ---------------------------------------------------------------------------
    // Periodic: Check all flights for contact timeout (REQ-STM-040)
    // ---------------------------------------------------------------------------
    void Server::CheckContactTimeouts()
    {
        auto timeouts = m_registry.CheckAllContactTimeouts();
        for (const auto& tr : timeouts) {
            m_logger.LogError("LOST_CONTACT detected — " + std::string(tr.trigger));
            m_ui.AddEvent("LOST_CONTACT: " + std::string(tr.trigger));

            // Log the state transition
            m_logger.LogStateChange(
                0U,  // flight_id not in TransitionResult — logged via trigger
                FlightStateToString(tr.previousState),
                FlightStateToString(tr.currentState),
                tr.trigger
            );
        }
    }

    // ---------------------------------------------------------------------------
    // Periodic: Check pending handoffs for timeout (REQ-SVR-040)
    // ---------------------------------------------------------------------------
    void Server::CheckHandoffTimeouts()
    {
        auto timedOut = m_handoffManager.CheckHandoffTimeouts(m_registry);
        for (const auto& action : timedOut) {
            LogTransition(action.transitionResult, action.flightId);
            m_logger.LogError("Handoff timeout for flight " +
                std::to_string(action.flightId) +
                " — reverted to sector " +
                std::to_string(action.fromSectorId));
            m_ui.AddEvent("HANDOFF TIMEOUT: FLT:" + std::to_string(action.flightId) +
                " reverted to sector " + std::to_string(action.fromSectorId));
        }
    }

    // ---------------------------------------------------------------------------
    // ExecuteFileTransfer — send radar image chunks (REQ-SVR-050)
    // ---------------------------------------------------------------------------
    void Server::ExecuteFileTransfer(uint32_t flightId, const std::string& imagePath)
    {
        FlightRecord* record = m_registry.GetFlight(flightId);
        if (record == nullptr) {
            m_logger.LogError("File transfer: flight " + std::to_string(flightId) +
                " not found");
            return;
        }

        FileTransfer ft;
        if (!ft.LoadFile(imagePath)) {
            m_logger.LogError("File transfer: failed to load " + imagePath);
            return;
        }

        if (!ft.PrepareTransfer(flightId)) {
            m_logger.LogError("File transfer: failed to prepare for flight " +
                std::to_string(flightId));
            return;
        }

        m_logger.LogInfo("Starting file transfer: " + imagePath +
            " (" + std::to_string(ft.GetFileSize()) + " bytes, " +
            std::to_string(ft.GetTotalChunks()) + " chunks) to flight " +
            std::to_string(flightId));

        const Endpoint& dest = record->clientEndpoint;

        // 1. Send FILE_TRANSFER_START
        Packet startPkt = ft.BuildStartPacket();
        if (!m_rudp.SendReliable(startPkt, flightId, dest)) {
            m_logger.LogError("File transfer START failed for flight " +
                std::to_string(flightId));
            return;
        }
        m_logger.LogPacket("TX", startPkt, "OK");

        // 2. Send each chunk (each ACKed individually via RUDP)
        for (uint32_t i = 0U; i < ft.GetTotalChunks(); ++i) {
            Packet chunkPkt = ft.BuildChunkPacket(i);
            if (!m_rudp.SendReliable(chunkPkt, flightId, dest)) {
                m_logger.LogError("File transfer CHUNK " + std::to_string(i) +
                    " failed for flight " + std::to_string(flightId));
                return;  // Abort transfer on chunk failure
            }
            // Log every 100th chunk to avoid flooding the log
            if ((i % 100U == 0U) || (i == ft.GetTotalChunks() - 1U)) {
                m_logger.LogPacket("TX", chunkPkt, "OK");
            }
        }

        // 3. Send FILE_TRANSFER_END
        Packet endPkt = ft.BuildEndPacket();
        if (!m_rudp.SendReliable(endPkt, flightId, dest)) {
            m_logger.LogError("File transfer END failed for flight " +
                std::to_string(flightId));
            return;
        }
        m_logger.LogPacket("TX", endPkt, "OK");

        m_logger.LogInfo("File transfer completed: " + std::to_string(ft.GetTotalChunks()) +
            " chunks sent to flight " + std::to_string(flightId));
    }

    // ---------------------------------------------------------------------------
    // SendConnectAck — build and send CONNECT_ACK packet
    // ---------------------------------------------------------------------------
    // Payload: sector_id (uint32) + session_token (uint32) = 8 bytes
    // ---------------------------------------------------------------------------
    void Server::SendConnectAck(uint32_t flightId, uint32_t sectorId,
        uint32_t sessionToken, const Endpoint& dest)
    {
        Packet pkt(PacketType::CONNECT_ACK, flightId);

        std::vector<uint8_t> payload(8U);
        std::memcpy(payload.data(), &sectorId, sizeof(uint32_t));
        std::memcpy(&payload[4U], &sessionToken, sizeof(uint32_t));
        pkt.SetPayload(payload);

        if (m_rudp.SendReliable(pkt, flightId, dest)) {
            m_logger.LogPacket("TX", pkt, "OK");
        }
        else {
            m_logger.LogPacket("TX", pkt, "ERROR");
            m_logger.LogError("Failed to send CONNECT_ACK to flight " +
                std::to_string(flightId));
        }
    }

    // ---------------------------------------------------------------------------
    // SendHandoffInstruct — build and send HANDOFF_INSTRUCT packet
    // ---------------------------------------------------------------------------
    // Payload: new_sector_id (uint32) = 4 bytes
    // ---------------------------------------------------------------------------
    void Server::SendHandoffInstruct(uint32_t flightId, uint32_t newSectorId,
        const Endpoint& dest)
    {
        Packet pkt(PacketType::HANDOFF_INSTRUCT, flightId);

        std::vector<uint8_t> payload(4U);
        std::memcpy(payload.data(), &newSectorId, sizeof(uint32_t));
        pkt.SetPayload(payload);

        if (m_rudp.SendReliable(pkt, flightId, dest)) {
            m_logger.LogPacket("TX", pkt, "OK");
        }
        else {
            m_logger.LogPacket("TX", pkt, "ERROR");
            m_logger.LogError("Failed to send HANDOFF_INSTRUCT to flight " +
                std::to_string(flightId));
        }
    }

    // ---------------------------------------------------------------------------
    // LogTransition — log a StateMachine transition result
    // ---------------------------------------------------------------------------
    void Server::LogTransition(const TransitionResult& tr, uint32_t flightId)
    {
        if (tr.success) {
            m_logger.LogStateChange(
                flightId,
                FlightStateToString(tr.previousState),
                FlightStateToString(tr.currentState),
                tr.trigger
            );
        }
        else if (tr.rejectionReason != nullptr) {
            m_logger.LogError("Transition rejected for flight " +
                std::to_string(flightId) + ": " +
                std::string(tr.rejectionReason) +
                " (from=" + FlightStateToString(tr.previousState) +
                ", trigger=" + tr.trigger + ")");
        }
    }

    // ---------------------------------------------------------------------------
    // Accessors
    // ---------------------------------------------------------------------------
    const FlightRegistry& Server::GetFlightRegistry() const
    {
        return m_registry;
    }

    const HandoffManager& Server::GetHandoffManager() const
    {
        return m_handoffManager;
    }

    bool Server::IsRunning() const
    {
        return m_running;
    }

} // namespace AeroTrack