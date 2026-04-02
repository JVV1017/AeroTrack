// =============================================================================
// FlightRegistry.cpp — Active flight tracking registry implementation
// =============================================================================
// Requirements: REQ-SVR-020, REQ-SVR-030
// Standard:     MISRA C++ compliant (see FlightRegistry.h header comment)
// =============================================================================

#include "FlightRegistry.h"

#include <cstring>  // std::memset

namespace AeroTrack {

    // ---------------------------------------------------------------------------
    // FlightRecord constructor
    // ---------------------------------------------------------------------------
    FlightRecord::FlightRecord(uint32_t id,
        const std::string& sign,
        const Endpoint& ep)
        : flightId(id)
        , callsign(sign)
        , stateMachine(id)         // StateMachine starts in IDLE
        , sectorId(0U)             // No sector until CONNECT_ACK assigns one
        , lastPosition()           // Zero-initialised below
        , hasPosition(false)
        , clientEndpoint(ep)
        , sessionToken(0U)         // Set by FlightRegistry during registration
        , connectedTime(std::chrono::steady_clock::now())
    {
        // Zero the position struct defensively
        std::memset(&lastPosition, 0, sizeof(PositionPayload));
    }

    // ---------------------------------------------------------------------------
    // FlightRegistry constructor
    // ---------------------------------------------------------------------------
    FlightRegistry::FlightRegistry()
        : m_flights()
        , m_nextSessionToken(1000U)   // Session tokens start at 1000
    {
    }

    // ---------------------------------------------------------------------------
    // RegisterFlight — called after 3-step handshake completion
    // ---------------------------------------------------------------------------
    bool FlightRegistry::RegisterFlight(uint32_t flightId,
        const std::string& callsign,
        const Endpoint& clientEndpoint)
    {
        // Check for duplicate registration
        if (m_flights.find(flightId) != m_flights.end()) {
            return false;  // Already registered — caller should log this
        }

        // Create the flight record (StateMachine starts in IDLE)
        FlightRecord record(flightId, callsign, clientEndpoint);
        record.sessionToken = m_nextSessionToken;
        m_nextSessionToken += 1U;

        // Insert into the registry
        // MISRA: using emplace to construct in-place, avoids unnecessary copy
        m_flights.emplace(flightId, record);

        return true;
    }

    // ---------------------------------------------------------------------------
    // RemoveFlight — called on DISCONNECT or stale cleanup
    // ---------------------------------------------------------------------------
    bool FlightRegistry::RemoveFlight(uint32_t flightId)
    {
        auto it = m_flights.find(flightId);
        if (it == m_flights.end()) {
            return false;  // Flight not found
        }

        m_flights.erase(it);
        return true;
    }

    // ---------------------------------------------------------------------------
    // GetFlight — non-owning lookup
    // ---------------------------------------------------------------------------
    FlightRecord* FlightRegistry::GetFlight(uint32_t flightId)
    {
        auto it = m_flights.find(flightId);
        if (it == m_flights.end()) {
            return nullptr;
        }
        return &(it->second);
    }

    const FlightRecord* FlightRegistry::GetFlight(uint32_t flightId) const
    {
        auto it = m_flights.find(flightId);
        if (it == m_flights.end()) {
            return nullptr;
        }
        return &(it->second);
    }

    // ---------------------------------------------------------------------------
    // REQ-SVR-020: Update last known position
    // ---------------------------------------------------------------------------
    bool FlightRegistry::UpdatePosition(uint32_t flightId,
        const PositionPayload& position)
    {
        FlightRecord* record = GetFlight(flightId);
        if (record == nullptr) {
            return false;
        }

        record->lastPosition = position;
        record->hasPosition = true;

        // Reset the contact-loss timer on the state machine (REQ-STM-040)
        record->stateMachine.RecordPacketReceived();

        return true;
    }

    // ---------------------------------------------------------------------------
    // REQ-SVR-020: Update sector assignment
    // ---------------------------------------------------------------------------
    bool FlightRegistry::UpdateSector(uint32_t flightId, uint32_t newSectorId)
    {
        FlightRecord* record = GetFlight(flightId);
        if (record == nullptr) {
            return false;
        }

        record->sectorId = newSectorId;
        return true;
    }

    // ---------------------------------------------------------------------------
    // REQ-STM-040: Bulk timeout check across all flights
    // ---------------------------------------------------------------------------
    std::vector<TransitionResult> FlightRegistry::CheckAllContactTimeouts()
    {
        std::vector<TransitionResult> timeouts;

        for (auto& pair : m_flights) {
            TransitionResult result = pair.second.stateMachine.CheckContactTimeout();
            if (result.success) {
                timeouts.push_back(result);
            }
        }

        return timeouts;
    }

    // ---------------------------------------------------------------------------
    // Accessors
    // ---------------------------------------------------------------------------
    uint32_t FlightRegistry::GetFlightCount() const
    {
        return static_cast<uint32_t>(m_flights.size());
    }

    bool FlightRegistry::HasFlight(uint32_t flightId) const
    {
        return (m_flights.find(flightId) != m_flights.end());
    }

    const std::map<uint32_t, FlightRecord>& FlightRegistry::GetAllFlights() const
    {
        return m_flights;
    }

} // namespace AeroTrack