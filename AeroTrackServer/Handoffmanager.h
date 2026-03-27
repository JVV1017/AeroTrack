// =============================================================================
// HandoffManager.h — Handoff coordination for AeroTrack Ground Control
// =============================================================================
// Requirements: REQ-SVR-040 (sector boundary detection + HANDOFF_INSTRUCT)
// Also drives: State transitions #3–#8 via FlightRegistry's StateMachine
//
// Standard:     MISRA C++ — no stream I/O, no Winsock, no raw new/delete,
//               U suffix, fixed-width types, explicit bool conditions
//
// Design:
//   HandoffManager is a pure coordination class — NO network I/O.
//   It checks positions against sector boundaries, manages pending handoff
//   timers, and returns HandoffAction structs that tell the Server main
//   loop what packets to send and what state transitions to execute.
//
//   Sector model: Simple latitude-band sectors for simulation.
//   Real ATC uses complex polygonal sectors — this is sufficient for
//   demonstrating the handoff state machine and boundary detection.
// =============================================================================
#pragma once

#include "FlightRegistry.h"
#include "StateMachine.h"
#include "Packet.h"

#include <cstdint>
#include <chrono>
#include <map>
#include <vector>
#include <string>

namespace AeroTrack {

    // ---------------------------------------------------------------------------
    // SectorDefinition — defines a sector's geographic boundary
    // ---------------------------------------------------------------------------
    // Uses latitude bands for simplicity. Each sector spans:
    //   latMin (inclusive) to latMax (exclusive), all longitudes.
    // ---------------------------------------------------------------------------
    struct SectorDefinition {
        uint32_t    sectorId{ 0U };
        std::string sectorName;      // e.g., "SECTOR-NORTH", "SECTOR-SOUTH"
        double      latMin{ 0.0 };   // Southern boundary (inclusive)
        double      latMax{ 0.0 };   // Northern boundary (exclusive)
        std::string radarImagePath;  // Path to 1MB+ JPEG for this sector
    };

    // ---------------------------------------------------------------------------
    // HandoffActionType — what the Server main loop should do
    // ---------------------------------------------------------------------------
    enum class HandoffActionType : uint8_t {
        NONE = 0U,   // No action needed
        SEND_HANDOFF_INSTRUCT = 1U, // Send HANDOFF_INSTRUCT to aircraft
        START_FILE_TRANSFER = 2U, // Begin radar image transfer (REQ-SVR-050)
        HANDOFF_TIMED_OUT = 3U, // Handoff timeout — revert to original sector
        HANDOFF_COMPLETED = 4U  // Handoff success — update sector assignment
    };

    // ---------------------------------------------------------------------------
    // HandoffAction — instruction returned to Server main loop
    // ---------------------------------------------------------------------------
    // Server reads these and performs the actual I/O (send packets, log, etc.)
    // ---------------------------------------------------------------------------
    struct HandoffAction {
        HandoffActionType type{ HandoffActionType::NONE };
        uint32_t          flightId{ 0U };
        uint32_t          fromSectorId{ 0U };     // Original sector
        uint32_t          toSectorId{ 0U };       // Target sector
        std::string       radarImagePath;          // For START_FILE_TRANSFER
        TransitionResult  transitionResult;        // Result of any SM transition attempted
    };

    // ---------------------------------------------------------------------------
    // PendingHandoff — tracks an in-progress handoff with timeout
    // ---------------------------------------------------------------------------
    struct PendingHandoff {
        uint32_t flightId{ 0U };
        uint32_t fromSectorId{ 0U };
        uint32_t toSectorId{ 0U };
        std::string radarImagePath;
        std::chrono::steady_clock::time_point initiatedTime;
        bool instructSent{ false };  // True once HANDOFF_INSTRUCT was sent (PENDING state)
    };

    // ---------------------------------------------------------------------------
    // HandoffManager — coordinates sector boundary detection and handoff flow
    // ---------------------------------------------------------------------------
    class HandoffManager {
    public:
        HandoffManager();

        // -----------------------------------------------------------------------
        // Sector configuration — called once during server initialisation
        // -----------------------------------------------------------------------
        // Adds a sector definition. Sectors should cover the full latitude range
        // expected during simulation with no gaps or overlaps.
        // -----------------------------------------------------------------------
        void AddSector(const SectorDefinition& sector);

        // -----------------------------------------------------------------------
        // Get sector count (for testing)
        // -----------------------------------------------------------------------
        uint32_t GetSectorCount() const;

        // -----------------------------------------------------------------------
        // REQ-SVR-040: Determine which sector a position falls in
        // -----------------------------------------------------------------------
        // Returns the sectorId, or 0 if no sector matches (out of bounds).
        // -----------------------------------------------------------------------
        uint32_t GetSectorForPosition(double latitude) const;

        // -----------------------------------------------------------------------
        // REQ-SVR-040: Check if a position update requires a handoff
        // -----------------------------------------------------------------------
        // Called after every POSITION_REPORT. Compares the aircraft's current
        // assigned sector with the sector matching its new position.
        //
        // If a boundary crossing is detected AND the flight is in TRACKING state,
        // returns a HandoffAction with type SEND_HANDOFF_INSTRUCT and drives
        // the state machine through TRACKING → HANDOFF_INITIATED.
        //
        // If no crossing, returns HandoffAction with type NONE.
        // -----------------------------------------------------------------------
        HandoffAction CheckForHandoff(FlightRegistry& registry, uint32_t flightId);

        // -----------------------------------------------------------------------
        // Confirm HANDOFF_INSTRUCT was sent — advance to HANDOFF_PENDING
        // -----------------------------------------------------------------------
        // Called by Server after successfully sending the HANDOFF_INSTRUCT packet.
        // Drives HANDOFF_INITIATED → HANDOFF_PENDING and starts the timeout timer.
        //
        // Returns HandoffAction indicating whether to start file transfer.
        // -----------------------------------------------------------------------
        HandoffAction ConfirmInstructSent(FlightRegistry& registry, uint32_t flightId);

        // -----------------------------------------------------------------------
        // Process HANDOFF_ACK received from aircraft
        // -----------------------------------------------------------------------
        // Drives HANDOFF_PENDING → HANDOFF_COMPLETE → TRACKING (auto).
        // Updates the flight's sector assignment to the new sector.
        // Returns HandoffAction with type HANDOFF_COMPLETED.
        // -----------------------------------------------------------------------
        HandoffAction ProcessHandoffAck(FlightRegistry& registry, uint32_t flightId);

        // -----------------------------------------------------------------------
        // Check all pending handoffs for timeout
        // -----------------------------------------------------------------------
        // Called periodically from the server main loop.
        // For any pending handoff that exceeds HANDOFF_TIMEOUT_MS:
        //   drives HANDOFF_PENDING → HANDOFF_FAILED → TRACKING (auto-revert)
        //   and removes the pending entry.
        //
        // Returns a vector of actions for timed-out handoffs.
        // -----------------------------------------------------------------------
        std::vector<HandoffAction> CheckHandoffTimeouts(FlightRegistry& registry);

        // -----------------------------------------------------------------------
        // Query: is a handoff currently pending for this flight?
        // -----------------------------------------------------------------------
        bool IsHandoffPending(uint32_t flightId) const;

        // -----------------------------------------------------------------------
        // Get the pending handoff info (for UI display), nullptr if none
        // -----------------------------------------------------------------------
        const PendingHandoff* GetPendingHandoff(uint32_t flightId) const;

        // -----------------------------------------------------------------------
        // Get count of active pending handoffs
        // -----------------------------------------------------------------------
        uint32_t GetPendingHandoffCount() const;

    private:
        // -----------------------------------------------------------------------
        // Internal: find sector definition by ID, returns nullptr if not found
        // -----------------------------------------------------------------------
        const SectorDefinition* FindSector(uint32_t sectorId) const;

        // -----------------------------------------------------------------------
        // Internal: make a default "no action" result
        // -----------------------------------------------------------------------
        HandoffAction MakeNoAction(uint32_t flightId) const;

        std::vector<SectorDefinition>       m_sectors;
        std::map<uint32_t, PendingHandoff>  m_pendingHandoffs;  // Keyed by flightId
    };

} // namespace AeroTrack