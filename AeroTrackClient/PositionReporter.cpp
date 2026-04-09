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
        , m_latDirection(1.0)   // start moving north toward sector boundary at 45.0
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
        // MISRA Fix [V2547]: memcpy returns void* to the destination — always
        // the same pointer passed in as the first argument. There is no
        // meaningful result to check; discard is intentional.
        (void)std::memcpy(payload.data(), &pos, sizeof(PositionPayload));

        Packet pkt(PacketType::POSITION_REPORT, m_flightId);
        pkt.SetPayload(payload);

        // Fire-and-forget: POSITION_REPORT is sent every 2s.
        // SendReliable blocks the receive loop waiting for ACK, which prevents
        // the client from draining FILE_TRANSFER_CHUNK packets during a burst
        // transfer (server sends 1024 chunks at 1 chunk/ms). Loss of one position
        // report is acceptable -- the next arrives 2 seconds later.
        // MISRA 0-1-7: (void) cast -- return value intentionally discarded.
        (void)m_rudp.SendPacket(pkt, m_flightId, m_serverEndpoint);
        m_logger.LogPacket("TX", pkt);  // REQ-CLT-060
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
        // Move in current direction — MISRA DEV-004: guard-clauses below
        m_latitude += m_latDirection * 0.10;  // 0.10/report at 2s interval = 30s to first crossing

        // Bounce at sector limits instead of hard-wrapping.
        // Bounce between 42.0 and 48.0 (sector boundary at 45.0).
        // Step 0.10 at 2s interval: first crossing in ~30s, round trip ~2 min.
        if (m_latitude >= 48.0) {
            m_latitude = 48.0;
            m_latDirection = -1.0;  // reverse: head south
        }
        else if (m_latitude <= 42.0) {
            m_latitude = 42.0;
            m_latDirection = 1.0;   // reverse: head north
        }
        else {
            // MISRA 6-4-2: terminal else — no action needed mid-range
        }
    }

} // namespace AeroTrack