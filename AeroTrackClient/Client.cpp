// REQ-CLT-010: 3-step handshake
// REQ-CLT-060: TX/RX packet logging via Logger instance
// REQ-CLT-070: DISCONNECT on shutdown (should)

#include "Client.h"
#include "PositionReporter.h"
#include "HandoffHandler.h"
#include "FileReceiver.h"
#include "ClientUI.h"
#include "../AeroTrackShared/PacketTypes.h"
#include "../AeroTrackShared/Config.h"
#include <cstdio>
#include <chrono>

namespace AeroTrack {

    Client::Client() noexcept
        : m_flightId(0U)
        , m_sectorId(0U)
        , m_sessionToken(0U)
        , m_state(ClientState::DISCONNECTED)
        , m_running(false)
    {
        m_serverEndpoint.ip = SERVER_IP;
        m_serverEndpoint.port = SERVER_PORT;
    }

    Client::~Client() noexcept {
        Stop();
    }

    // ---------------------------------------------------------------------------
    // Init
    // ---------------------------------------------------------------------------
    bool Client::Init(uint32_t flightId, const std::string& callsign) noexcept {
        m_flightId = flightId;
        m_callsign = (callsign.size() > 8U) ? callsign.substr(0U, 8U) : callsign;

        // REQ-CLT-060: Open log file — filename includes flight ID for uniqueness
        std::string logFile = "aerotrack_client_" + std::to_string(flightId) + ".log";
        if (!m_logger.Init(logFile)) {
            return false;
        }
        m_logger.LogInfo("Client: Logger initialized");

        m_rudp.SetLogger(&m_logger);

        if (!m_rudp.Init()) {
            m_logger.LogError("Client: RUDP Init failed");
            return false;
        }

        char msg[64];
        std::snprintf(msg, sizeof(msg),
            "Client: Initialized — flight %u callsign %s",
            m_flightId, m_callsign.c_str());
        m_logger.LogInfo(msg);
        return true;
    }

    // ---------------------------------------------------------------------------
    // REQ-CLT-010: 3-step handshake
    // ---------------------------------------------------------------------------
    bool Client::Connect() noexcept {
        m_state = ClientState::CONNECTING;

        // ------------------------------------------------------------------
        // Step 1: Send CONNECT  (payload: callsign as null-terminated bytes)
        // ------------------------------------------------------------------
        Packet connectPkt(PacketType::CONNECT, m_flightId);
        {
            std::vector<uint8_t> payload(m_callsign.size() + 1U, 0U);
            for (size_t i = 0U; i < m_callsign.size(); ++i) {
                payload[i] = static_cast<uint8_t>(m_callsign[i]);
            }
            connectPkt.SetPayload(payload);
        }

        if (!m_rudp.SendReliable(connectPkt, m_flightId, m_serverEndpoint)) {
            m_logger.LogError("Client: Failed to send CONNECT");
            m_state = ClientState::DISCONNECTED;
            return false;
        }
        m_logger.LogPacket("TX", connectPkt);
        m_logger.LogInfo("Client: CONNECT sent — waiting for CONNECT_ACK");

        // ------------------------------------------------------------------
        // Step 2: Receive CONNECT_ACK
        // ------------------------------------------------------------------
        Packet rxPkt;
        Endpoint from;
        bool ackReceived = false;

        for (uint32_t attempt = 0U; attempt < RUDP_MAX_RETRIES; ++attempt) {
            if (m_rudp.Receive(rxPkt, from)) {
                if (rxPkt.GetType() == PacketType::CONNECT_ACK &&
                    rxPkt.GetFlightId() == m_flightId) {
                    m_logger.LogPacket("RX", rxPkt);
                    HandleConnectAck(rxPkt);
                    ackReceived = true;
                    break;
                }
            }
        }

        if (!ackReceived) {
            m_logger.LogError("Client: CONNECT_ACK not received — handshake failed");
            m_state = ClientState::DISCONNECTED;
            return false;
        }

        // ------------------------------------------------------------------
        // Step 3: Send CONNECT_CONFIRM  (payload: echo sectorId, 4 bytes BE)
        // ------------------------------------------------------------------
        Packet confirmPkt(PacketType::CONNECT_CONFIRM, m_flightId);
        {
            std::vector<uint8_t> payload(4U);
            payload[0U] = static_cast<uint8_t>((m_sectorId >> 24U) & 0xFFU);
            payload[1U] = static_cast<uint8_t>((m_sectorId >> 16U) & 0xFFU);
            payload[2U] = static_cast<uint8_t>((m_sectorId >> 8U) & 0xFFU);
            payload[3U] = static_cast<uint8_t>(m_sectorId & 0xFFU);
            confirmPkt.SetPayload(payload);
        }

        if (!m_rudp.SendReliable(confirmPkt, m_flightId, m_serverEndpoint)) {
            m_logger.LogError("Client: Failed to send CONNECT_CONFIRM");
            m_state = ClientState::DISCONNECTED;
            return false;
        }
        m_logger.LogPacket("TX", confirmPkt);

        m_state = ClientState::CONNECTED;

        char msg[64];
        std::snprintf(msg, sizeof(msg),
            "Client: Handshake complete — CONNECTED to sector %u", m_sectorId);
        m_logger.LogInfo(msg);

        // Create sub-modules now that handshake succeeded
        // MISRA Deviation 1: make_unique RAII per REQ-SYS-030
        m_positionReporter = std::make_unique<PositionReporter>(
            m_rudp, m_logger, m_flightId, m_serverEndpoint);
        m_handoffHandler = std::make_unique<HandoffHandler>(
            m_rudp, m_logger, m_flightId, m_serverEndpoint);
        // REQ-CLT-040: flightId passed so output file is named received_sector_<flightId>.jpg
        m_fileReceiver = std::make_unique<FileReceiver>(m_logger, m_flightId);
        m_ui = std::make_unique<ClientUI>(*this);

        return true;
    }

    // ---------------------------------------------------------------------------
    // Run — main receive/send/UI loop
    // ---------------------------------------------------------------------------
    void Client::Run() noexcept {
        if (m_state != ClientState::CONNECTED) {
            m_logger.LogError("Client: Run() called before handshake — aborting");
            return;
        }

        m_running.store(true);
        m_state = ClientState::TRACKING;
        m_logger.LogInfo("Client: Run loop started");

        auto lastHeartbeat = std::chrono::steady_clock::now();
        auto lastPositionReport = std::chrono::steady_clock::now();

        while (m_running.load()) {
            auto now = std::chrono::steady_clock::now();

            // REQ-CLT-020: Send POSITION_REPORT on interval
            auto posElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastPositionReport).count();
            if (posElapsed >= static_cast<long long>(POSITION_REPORT_INTERVAL_MS)) {
                if (m_positionReporter != nullptr) {
                    m_positionReporter->SendReport();
                }
                lastPositionReport = now;
            }

            // Send HEARTBEAT on interval
            auto hbElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - lastHeartbeat).count();
            if (hbElapsed >= static_cast<long long>(HEARTBEAT_INTERVAL_MS)) {
                SendHeartbeat();
                lastHeartbeat = now;
            }

            // Receive and dispatch any incoming packet
            Packet pkt;
            Endpoint from;
            if (m_rudp.Receive(pkt, from, 100U)) {
                // REQ-CLT-060: Log RX
                m_logger.LogPacket("RX", pkt);
                DispatchPacket(pkt);
            }

            // Refresh UI
            if (m_ui != nullptr) {
                m_ui->Render();
            }
        }

        m_logger.LogInfo("Client: Run loop exited");
    }

    // ---------------------------------------------------------------------------
    // Stop
    // ---------------------------------------------------------------------------
    void Client::Stop() noexcept {
        if (m_running.load()) {
            if ((m_state == ClientState::TRACKING) ||
                (m_state == ClientState::CONNECTED)) {
                SendDisconnect();
            }
            m_running.store(false);
        }
    }

    // ---------------------------------------------------------------------------
    // Accessors
    // ---------------------------------------------------------------------------
    ClientState Client::GetState() const noexcept {
        return m_state;
    }

    uint32_t Client::GetFlightId() const noexcept {
        return m_flightId;
    }

    const std::string& Client::GetCallsign() const noexcept {
        return m_callsign;
    }

    uint32_t Client::GetSectorId() const noexcept {
        return m_sectorId;
    }

    const PositionReporter& Client::GetPositionReporter() const noexcept {
        return *m_positionReporter;
    }

    const FileReceiver& Client::GetFileReceiver() const noexcept {
        return *m_fileReceiver;
    }

    // ---------------------------------------------------------------------------
    // Packet dispatch
    // ---------------------------------------------------------------------------
    void Client::DispatchPacket(const Packet& pkt) noexcept {
        switch (pkt.GetType()) {
        case PacketType::CONNECT_ACK:
            break;  // Duplicate during run — ignore
        case PacketType::TRACKING_ACK:
            HandleTrackingAck(pkt);
            break;
        case PacketType::HANDOFF_INSTRUCT:
            HandleHandoffInstruct(pkt);
            break;
        case PacketType::FILE_TRANSFER_START:
        case PacketType::FILE_TRANSFER_CHUNK:
        case PacketType::FILE_TRANSFER_END:
            HandleFileTransfer(pkt);
            break;
        case PacketType::ACK:
            break;  // Handled inside RUDP layer
        default: {
            char msg[64];
            std::snprintf(msg, sizeof(msg),
                "Client: Unhandled packet type 0x%02X",
                static_cast<uint8_t>(pkt.GetType()));
            m_logger.LogError(msg);
            break;
        }
        }
    }

    // ---------------------------------------------------------------------------
    // Handshake helper — parse CONNECT_ACK payload
    // Payload: sectorId (4 bytes BE) + sessionToken (4 bytes BE)
    // ---------------------------------------------------------------------------
    void Client::HandleConnectAck(const Packet& pkt) noexcept {
        const std::vector<uint8_t>& payload = pkt.GetPayload();
        if (payload.size() < 8U) {
            m_logger.LogError("Client: CONNECT_ACK payload too short");
            return;
        }

        m_sectorId =
            (static_cast<uint32_t>(payload[0U]) << 24U) |
            (static_cast<uint32_t>(payload[1U]) << 16U) |
            (static_cast<uint32_t>(payload[2U]) << 8U) |
            static_cast<uint32_t>(payload[3U]);

        m_sessionToken =
            (static_cast<uint32_t>(payload[4U]) << 24U) |
            (static_cast<uint32_t>(payload[5U]) << 16U) |
            (static_cast<uint32_t>(payload[6U]) << 8U) |
            static_cast<uint32_t>(payload[7U]);

        char msg[64];
        std::snprintf(msg, sizeof(msg),
            "Client: CONNECT_ACK — sector %u token 0x%08X",
            m_sectorId, m_sessionToken);
        m_logger.LogInfo(msg);
    }

    void Client::HandleTrackingAck(const Packet& pkt) noexcept {
        (void)pkt;
        if (m_ui != nullptr) {
            m_ui->AppendEvent("TRACKING_ACK received");
        }
    }

    // REQ-CLT-030: Forward to HandoffHandler
    void Client::HandleHandoffInstruct(const Packet& pkt) noexcept {
        if (m_handoffHandler != nullptr) {
            m_handoffHandler->HandleInstruct(pkt);
            uint32_t newSector = m_handoffHandler->GetPendingSectorId();
            if (newSector != 0U) {
                m_sectorId = newSector;
                m_handoffHandler->ClearPendingHandoff();

                // REQ-CLT-060: Log handoff event to flight terminal comm log
                if (m_ui != nullptr) {
                    char msg[48];
                    std::snprintf(msg, sizeof(msg),
                        "Handoff complete -> sector %u", m_sectorId);
                    m_ui->AppendEvent(msg);
                }
            }
        }
    }

    // REQ-CLT-040: Forward to FileReceiver
    // Comm log events are posted on state transitions so the flight terminal
    // always reflects the current transfer status (REQ-CLT-050).
    void Client::HandleFileTransfer(const Packet& pkt) noexcept {
        if (m_fileReceiver != nullptr) {
            const TransferState stateBefore = m_fileReceiver->GetState();
            m_fileReceiver->HandlePacket(pkt);
            const TransferState stateAfter = m_fileReceiver->GetState();

            if (m_ui != nullptr) {
                // IDLE -> RECEIVING: new transfer starting
                if ((stateBefore != TransferState::RECEIVING) &&
                    (stateAfter == TransferState::RECEIVING)) {
                    m_ui->AppendEvent("Receiving radar image...");
                }
                // -> COMPLETE: write success
                else if ((stateBefore != TransferState::COMPLETE) &&
                    (stateAfter == TransferState::COMPLETE)) {
                    char msg[64];
                    std::snprintf(msg, sizeof(msg),
                        "Radar saved: %s",
                        m_fileReceiver->GetOutputPath().c_str());
                    m_ui->AppendEvent(msg);
                }
                // -> FAILED: write failed or incomplete
                else if ((stateBefore != TransferState::FAILED) &&
                    (stateAfter == TransferState::FAILED)) {
                    m_ui->AppendEvent("Radar transfer failed");
                }
                else {
                    // MISRA 6-4-2: terminal else — no event for intermediate chunk states
                }
            }
        }
    }

    // REQ-CLT-070: Send DISCONNECT (should — best effort)
    void Client::SendDisconnect() noexcept {
        m_state = ClientState::DISCONNECTING;
        Packet pkt(PacketType::DISCONNECT, m_flightId);
        if (m_rudp.SendReliable(pkt, m_flightId, m_serverEndpoint)) {
            m_logger.LogPacket("TX", pkt);
            m_logger.LogInfo("Client: DISCONNECT sent");
        }
        else {
            m_logger.LogError("Client: DISCONNECT send failed (best-effort)");
        }
    }

    void Client::SendHeartbeat() noexcept {
        Packet pkt(PacketType::HEARTBEAT, m_flightId);
        // Fire-and-forget: SendReliable blocks the receive loop waiting for ACK,
        // which prevents the client from draining FILE_TRANSFER_CHUNK packets
        // during a burst transfer. Loss of one heartbeat is acceptable -- the
        // server contact timeout is 10s and heartbeats fire every 3s.
        // MISRA 0-1-7: (void) cast -- return value intentionally discarded.
        (void)m_rudp.SendPacket(pkt, m_flightId, m_serverEndpoint);
        m_logger.LogPacket("TX", pkt);
    }

} // namespace AeroTrack