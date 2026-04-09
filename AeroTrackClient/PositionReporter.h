#pragma once
// REQ-CLT-020: Periodic POSITION_REPORT transmission
// REQ-PKT-040: PositionPayload — 24 bytes

#include "../AeroTrackShared/RUDP.h"
#include "../AeroTrackShared/Packet.h"
#include "../AeroTrackShared/Logger.h"
#include <cstdint>

namespace AeroTrack {

    class PositionReporter {
    public:
        PositionReporter(RUDP& rudp,
            Logger& logger,
            uint32_t        flightId,
            const Endpoint& serverEndpoint) noexcept;

        // REQ-CLT-020: Build and send one POSITION_REPORT packet
        void SendReport() noexcept;

        // Inject real position data (or used by unit tests)
        void SetPosition(double   latitude,
            double   longitude,
            uint32_t altitudeFt,
            uint16_t speedKts,
            uint16_t headingDeg) noexcept;

        // Accessors for ClientUI
        double   GetLatitude()  const noexcept;
        double   GetLongitude() const noexcept;
        uint32_t GetAltitude()  const noexcept;
        uint16_t GetSpeed()     const noexcept;
        uint16_t GetHeading()   const noexcept;

    private:
        RUDP& m_rudp;
        Logger& m_logger;
        uint32_t m_flightId;
        Endpoint m_serverEndpoint;

        double   m_latitude;
        double   m_longitude;
        uint32_t m_altitudeFt;
        uint16_t m_speedKts;
        uint16_t m_headingDeg;

        // +1.0 = moving north, -1.0 = moving south.
        // Bounce prevents hard wrap from 49.0 -> 43.5 which triggered
        // an immediate reverse handoff and reset FileReceiver mid-transfer.
        double   m_latDirection;

        void SimulateMovement() noexcept;
    };

} // namespace AeroTrack