#pragma once
// REQ-CLT-010: 3-step connection handshake before any data transmission
// REQ-CLT-060: All TX/RX packets logged via Logger instance
// REQ-CLT-070: Graceful DISCONNECT on shutdown (should)

#include "../AeroTrackShared/RUDP.h"
#include "../AeroTrackShared/Packet.h"
#include "../AeroTrackShared/Logger.h"
#include <cstdint>
#include <string>
#include <atomic>
#include <memory>

namespace AeroTrack {

    // Forward declarations — full includes in Client.cpp only
    class PositionReporter;
    class HandoffHandler;
    class FileReceiver;
    class ClientUI;

    // Client-side connection state
    enum class ClientState : uint8_t {
        DISCONNECTED = 0U,
        CONNECTING = 1U,
        CONNECTED = 2U,
        TRACKING = 3U,
        DISCONNECTING = 4U
    };

    class Client {
    public:
        Client() noexcept;
        ~Client() noexcept;

        // Initialize logger and RUDP socket.
        bool Init(uint32_t flightId, const std::string& callsign) noexcept;

        // REQ-CLT-010: 3-step handshake
        bool Connect() noexcept;

        // Main loop — blocks until Stop()
        void Run() noexcept;

        // REQ-CLT-070: Signal shutdown, sends DISCONNECT
        void Stop() noexcept;

        // Accessors for ClientUI
        ClientState             GetState()             const noexcept;
        uint32_t                GetFlightId()          const noexcept;
        const std::string& GetCallsign()          const noexcept;
        uint32_t                GetSectorId()          const noexcept;
        const PositionReporter& GetPositionReporter()  const noexcept;
        const FileReceiver& GetFileReceiver()      const noexcept;

    private:
        // REQ-CLT-060: Logger owned here, pointer passed to sub-modules
        Logger            m_logger;
        RUDP              m_rudp;
        uint32_t          m_flightId;
        std::string       m_callsign;
        uint32_t          m_sectorId;
        uint32_t          m_sessionToken;
        ClientState       m_state;
        std::atomic<bool> m_running;
        Endpoint          m_serverEndpoint;

        // MISRA Deviation 1: unique_ptr RAII per REQ-SYS-030
        std::unique_ptr<PositionReporter> m_positionReporter;
        std::unique_ptr<HandoffHandler>   m_handoffHandler;
        std::unique_ptr<FileReceiver>     m_fileReceiver;
        std::unique_ptr<ClientUI>         m_ui;

        void DispatchPacket(const Packet& pkt) noexcept;
        void HandleConnectAck(const Packet& pkt) noexcept;
        void HandleTrackingAck(const Packet& pkt) noexcept;
        void HandleHandoffInstruct(const Packet& pkt) noexcept;
        void HandleFileTransfer(const Packet& pkt) noexcept;
        void SendDisconnect()                  noexcept;
        void SendHeartbeat()                  noexcept;
    };

} // namespace AeroTrack