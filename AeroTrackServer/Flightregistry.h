// =============================================================================
// FlightRegistry.h — Active flight tracking registry for Ground Control
// =============================================================================
// Requirements: REQ-SVR-020, REQ-SVR-030
// Standard:     MISRA C++ — no raw new/delete, no stream I/O, U suffix,
//               fixed-width types, explicit bool conditions
//
// Design:
//   FlightRecord is a composite struct that OWNS its StateMachine instance.
//   FlightRegistry stores records in std::map<uint32_t, FlightRecord> keyed
//   by flight_id. Every incoming packet carries flight_id in the header,
//   making keyed lookup the primary access pattern.
//
//   std::map chosen over std::unordered_map for MISRA compliance:
//   deterministic iteration order, no hash function complexity.
//
//   FlightRegistry performs NO I/O — callers use TransitionResult and
//   Logger for all logging. This keeps the module testable in isolation.
// =============================================================================
#pragma once

#include "StateMachine.h"
#include "Packet.h"       // PositionPayload (shared library)
#include "RUDP.h"         // Endpoint (shared library)

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace AeroTrack {

    // ---------------------------------------------------------------------------
    // FlightRecord — all server-side state for one tracked aircraft
    // ---------------------------------------------------------------------------
    // REQ-SVR-020: stores current state, sector assignment, last known position
    // REQ-SVR-030: contains an independent StateMachine instance
    // ---------------------------------------------------------------------------
    struct FlightRecord {
        // --- Identity ---
        uint32_t    flightId;           // Unique aircraft identifier
        std::string callsign;           // e.g., "AC-801" (from CONNECT payload)

        // --- State machine (REQ-SVR-030) ---
        StateMachine stateMachine;      // Owns per-flight state machine

        // --- Tracking data (REQ-SVR-020) ---
        uint32_t    sectorId;           // Current assigned sector
        PositionPayload lastPosition;      // Last received position report
        bool        hasPosition;        // True once first POSITION_REPORT received

        // --- Connection info ---
        Endpoint    clientEndpoint;     // Where to send packets back to this aircraft
        uint32_t    sessionToken;       // Assigned during CONNECT_ACK

        // --- Timestamps ---
        std::chrono::steady_clock::time_point connectedTime;  // When handshake completed

        // Constructor: initialises with flight_id and callsign
        // StateMachine starts in IDLE, position zeroed, hasPosition = false
        FlightRecord(uint32_t id, const std::string& sign, const Endpoint& ep);
    };

    // ---------------------------------------------------------------------------
    // FlightRegistry — manages all active flights
    // ---------------------------------------------------------------------------
    // Thread safety: NOT thread-safe. The Server main loop is single-threaded
    // (select/poll pattern on one UDP socket), so no mutex is needed.
    // ---------------------------------------------------------------------------
    class FlightRegistry {
    public:
        FlightRegistry();

        // -----------------------------------------------------------------------
        // Registration — called after 3-step handshake completes
        // -----------------------------------------------------------------------
        // Returns true if flight was registered, false if flight_id already exists.
        // Assigns an auto-incremented session token.
        // -----------------------------------------------------------------------
        bool RegisterFlight(uint32_t flightId,
            const std::string& callsign,
            const Endpoint& clientEndpoint);

        // -----------------------------------------------------------------------
        // Removal — called on DISCONNECT or when removing a stale entry
        // -----------------------------------------------------------------------
        // Returns true if flight existed and was removed.
        // -----------------------------------------------------------------------
        bool RemoveFlight(uint32_t flightId);

        // -----------------------------------------------------------------------
        // Lookup — returns pointer to FlightRecord, or nullptr if not found
        // -----------------------------------------------------------------------
        // Non-owning pointer valid until RemoveFlight is called for this ID.
        // -----------------------------------------------------------------------
        FlightRecord* GetFlight(uint32_t flightId);
        const FlightRecord* GetFlight(uint32_t flightId) const;

        // -----------------------------------------------------------------------
        // REQ-SVR-020: Update last known position for a flight
        // -----------------------------------------------------------------------
        // Returns true if flight was found and position updated.
        // Also calls RecordPacketReceived() on the flight's StateMachine.
        // -----------------------------------------------------------------------
        bool UpdatePosition(uint32_t flightId, const PositionPayload& position);

        // -----------------------------------------------------------------------
        // REQ-SVR-020: Update sector assignment for a flight
        // -----------------------------------------------------------------------
        bool UpdateSector(uint32_t flightId, uint32_t newSectorId);

        // -----------------------------------------------------------------------
        // REQ-STM-040: Check all flights for contact timeout
        // -----------------------------------------------------------------------
        // Iterates every flight and calls StateMachine::CheckContactTimeout().
        // Returns a vector of TransitionResults for flights that transitioned
        // to LOST_CONTACT (caller logs each via Logger::LogStateChange).
        // -----------------------------------------------------------------------
        std::vector<TransitionResult> CheckAllContactTimeouts();

        // -----------------------------------------------------------------------
        // Accessors — for UI display and iteration
        // -----------------------------------------------------------------------
        uint32_t GetFlightCount() const;
        bool     HasFlight(uint32_t flightId) const;

        // Provides read-only access to all flights for dashboard rendering
        const std::map<uint32_t, FlightRecord>& GetAllFlights() const;

    private:
        std::map<uint32_t, FlightRecord> m_flights;
        uint32_t m_nextSessionToken;  // Auto-increment, assigned at registration
    };

} // namespace AeroTrack