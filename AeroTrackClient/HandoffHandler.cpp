// REQ-CLT-030: Process HANDOFF_INSTRUCT, respond with HANDOFF_ACK

#include "HandoffHandler.h"
#include "../AeroTrackShared/PacketTypes.h"
#include <cstdio>

namespace AeroTrack {

    HandoffHandler::HandoffHandler(RUDP& rudp,
        Logger& logger,
        uint32_t        flightId,
        const Endpoint& serverEndpoint) noexcept
        : m_rudp(rudp)
        , m_logger(logger)
        , m_flightId(flightId)
        , m_serverEndpoint(serverEndpoint)
        , m_pendingSectorId(0U)
        , m_hasPendingHandoff(false)
    {
    }

    // ---------------------------------------------------------------------------
    // REQ-CLT-030: Parse new sector ID from payload and send HANDOFF_ACK
    // Payload: newSectorId (4 bytes big-endian)
    // ---------------------------------------------------------------------------
    void HandoffHandler::HandleInstruct(const Packet& pkt) noexcept {
        const std::vector<uint8_t>& payload = pkt.GetPayload();
        if (payload.size() < 4U) {
            m_logger.LogError("HandoffHandler: HANDOFF_INSTRUCT payload too short");
            return;
        }

        m_pendingSectorId =
            (static_cast<uint32_t>(payload[0U]) << 24U) |
            (static_cast<uint32_t>(payload[1U]) << 16U) |
            (static_cast<uint32_t>(payload[2U]) << 8U) |
            static_cast<uint32_t>(payload[3U]);

        char msg[64];
        std::snprintf(msg, sizeof(msg),
            "HandoffHandler: HANDOFF_INSTRUCT — new sector %u", m_pendingSectorId);
        m_logger.LogInfo(msg);

        SendHandoffAck();
        m_hasPendingHandoff = true;
    }

    uint32_t HandoffHandler::GetPendingSectorId() const noexcept {
        return m_pendingSectorId;
    }

    bool HandoffHandler::HasPendingHandoff() const noexcept {
        return m_hasPendingHandoff;
    }

    void HandoffHandler::ClearPendingHandoff() noexcept {
        m_hasPendingHandoff = false;
        m_pendingSectorId = 0U;
    }

    void HandoffHandler::SendHandoffAck() noexcept {
        Packet ack(PacketType::HANDOFF_ACK, m_flightId);
        if (m_rudp.SendPacket(ack, m_flightId, m_serverEndpoint)) {
            m_logger.LogPacket("TX", ack);
            m_logger.LogInfo("HandoffHandler: HANDOFF_ACK sent");
        }
        else {
            m_logger.LogError("HandoffHandler: Failed to send HANDOFF_ACK");
        }
    }

} // namespace AeroTrack