#pragma once
// REQ-CLT-030: Receive HANDOFF_INSTRUCT, respond with HANDOFF_ACK

#include "../AeroTrackShared/RUDP.h"
#include "../AeroTrackShared/Packet.h"
#include "../AeroTrackShared/Logger.h"
#include <cstdint>

namespace AeroTrack {

    class HandoffHandler {
    public:
        HandoffHandler(RUDP& rudp,
            Logger& logger,
            uint32_t        flightId,
            const Endpoint& serverEndpoint) noexcept;

        // REQ-CLT-030: Parse HANDOFF_INSTRUCT and send HANDOFF_ACK
        void HandleInstruct(const Packet& pkt) noexcept;

        uint32_t GetPendingSectorId()  const noexcept;
        bool     HasPendingHandoff()   const noexcept;
        void     ClearPendingHandoff()       noexcept;

    private:
        RUDP& m_rudp;
        Logger& m_logger;
        uint32_t m_flightId;
        Endpoint m_serverEndpoint;

        uint32_t m_pendingSectorId;
        bool     m_hasPendingHandoff;

        void SendHandoffAck() noexcept;
    };

} // namespace AeroTrack