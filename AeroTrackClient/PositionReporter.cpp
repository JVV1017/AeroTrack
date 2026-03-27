// REQ-CLT-020: Periodic POSITION_REPORT transmission
// REQ-PKT-040: PositionPayload serialization

#include "PositionReporter.h"
#include "../AeroTrackShared/PacketTypes.h"
#include <cstring>

namespace AeroTrack {

    PositionReporter::PositionReporter(RUDP& rudp,
        Logger& logger,
        uint32_t        flightId,
        const Endpoint& serverEndpoint) noexcept
        : m_rudp(rudp)
        , m_logger(logger)
        , m_flightId(flightId)
        , m_serverEndpoint(serverEndpoint)
        , m_latitude(43.5)
        , m_longitude(-79.5)
        , m_altitudeFt(35000U)
        , m_speedKts(450U)
        , m_headingDeg(0U)
    {
    }

    // ---------------------------------------------------------------------------
    // REQ-CLT-020: Build and send POSITION_REPORT
    // REQ-PKT-040: Serialize 24-byte PositionPayload into packet payload
    // ---------------------------------------------------------------------------
    void PositionReporter::SendReport() noexcept {
        SimulateMovement();

        PositionPayload pos;
        pos.latitude = m_latitude;
        pos.longitude = m_longitude;
        pos.altitude_ft = m_altitudeFt;
        pos.speed_kts = m_speedKts;
        pos.heading_deg = m_headingDeg;

        // MISRA Deviation 1: vector RAII per REQ-SYS-030
        std::vector<uint8_t> payload(sizeof(PositionPayload));
        std::memcpy(payload.data(), &pos, sizeof(PositionPayload));

        Packet pkt(PacketType::POSITION_REPORT, m_flightId);
        pkt.SetPayload(payload);

        if (m_rudp.SendReliable(pkt, m_flightId, m_serverEndpoint)) {
            // REQ-CLT-060: Log TX via shared logger
            m_logger.LogPacket("TX", pkt);
        }
        else {
            m_logger.LogError("PositionReporter: SendReliable failed");
        }
    }

    void PositionReporter::SetPosition(double   latitude,
        double   longitude,
        uint32_t altitudeFt,
        uint16_t speedKts,
        uint16_t headingDeg) noexcept {
        m_latitude = latitude;
        m_longitude = longitude;
        m_altitudeFt = altitudeFt;
        m_speedKts = speedKts;
        m_headingDeg = headingDeg;
    }

    double   PositionReporter::GetLatitude()  const noexcept { return m_latitude; }
    double   PositionReporter::GetLongitude() const noexcept { return m_longitude; }
    uint32_t PositionReporter::GetAltitude()  const noexcept { return m_altitudeFt; }
    uint16_t PositionReporter::GetSpeed()     const noexcept { return m_speedKts; }
    uint16_t PositionReporter::GetHeading()   const noexcept { return m_headingDeg; }

    void PositionReporter::SimulateMovement() noexcept {
        m_latitude += 0.05;
        if (m_latitude > 49.0) {
            m_latitude = 43.5;
            m_longitude = -79.5;
        }
    }

} // namespace AeroTrack