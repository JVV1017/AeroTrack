// =============================================================================
// HandoffManager.cpp — Handoff coordination implementation
// =============================================================================
// Requirements: REQ-SVR-040
// Standard:     MISRA C++ compliant (see HandoffManager.h header comment)
// =============================================================================

#include "HandoffManager.h"
#include "Config.h"   // HANDOFF_TIMEOUT_MS

namespace AeroTrack {

    // ---------------------------------------------------------------------------
    // Constructor
    // ---------------------------------------------------------------------------
    HandoffManager::HandoffManager()
        : m_sectors()
        , m_pendingHandoffs()
    {
    }

    // ---------------------------------------------------------------------------
    // Sector configuration
    // ---------------------------------------------------------------------------
    void HandoffManager::AddSector(const SectorDefinition& sector)
    {
        m_sectors.push_back(sector);
    }

    uint32_t HandoffManager::GetSectorCount() const
    {
        return static_cast<uint32_t>(m_sectors.size());
    }

    // ---------------------------------------------------------------------------
    // REQ-SVR-040: Determine sector for a given latitude
    // ---------------------------------------------------------------------------
    // Sectors use latitude bands: latMin (inclusive) to latMax (exclusive).
    // Returns 0 if position is out of bounds for all defined sectors.
    // ---------------------------------------------------------------------------
    uint32_t HandoffManager::GetSectorForPosition(double latitude) const
    {
        for (const auto& sector : m_sectors) {
            if ((latitude >= sector.latMin) && (latitude < sector.latMax)) {
                return sector.sectorId;
            }
        }
        return 0U;  // No matching sector
    }

    // ---------------------------------------------------------------------------
    // REQ-SVR-040: Check if position update triggers a handoff
    // ---------------------------------------------------------------------------
    // Flow: detect boundary → TRACKING → HANDOFF_INITIATED (transition #3)
    // ---------------------------------------------------------------------------
    HandoffAction HandoffManager::CheckForHandoff(FlightRegistry& registry,
        uint32_t flightId)
    {
        FlightRecord* record = registry.GetFlight(flightId);
        if (record == nullptr) {
            return MakeNoAction(flightId);
        }

        // Only initiate handoff from TRACKING state
        if (record->stateMachine.GetCurrentState() != FlightState::TRACKING) {
            return MakeNoAction(flightId);
        }

        // Don't initiate if a handoff is already in progress for this flight
        if (m_pendingHandoffs.find(flightId) != m_pendingHandoffs.end()) {
            return MakeNoAction(flightId);
        }

        // Must have a position report to evaluate
        if (!record->hasPosition) {
            return MakeNoAction(flightId);
        }

        // Determine which sector the aircraft is now in
        const uint32_t currentSector = record->sectorId;
        const uint32_t newSector = GetSectorForPosition(record->lastPosition.latitude);

        // No boundary crossing if sectors match, or position is out of bounds
        if ((newSector == 0U) || (newSector == currentSector)) {
            return MakeNoAction(flightId);
        }

        // --- Boundary crossing detected ---

        // Find the target sector's radar image path
        const SectorDefinition* targetSectorDef = FindSector(newSector);
        std::string radarPath;
        if (targetSectorDef != nullptr) {
            radarPath = targetSectorDef->radarImagePath;
        }

        // Transition #3: TRACKING → HANDOFF_INITIATED
        TransitionResult tr = record->stateMachine.Transition(
            FlightState::HANDOFF_INITIATED,
            "Sector boundary crossed (REQ-SVR-040)"
        );

        if (!tr.success) {
            // Transition rejected — should not happen from TRACKING, but handle defensively
            HandoffAction action{};
            action.type = HandoffActionType::NONE;
            action.flightId = flightId;
            action.fromSectorId = currentSector;
            action.toSectorId = newSector;
            action.transitionResult = tr;
            return action;
        }

        // Create a pending handoff record (timer starts when INSTRUCT is confirmed sent)
        PendingHandoff pending;
        pending.flightId = flightId;
        pending.fromSectorId = currentSector;
        pending.toSectorId = newSector;
        pending.radarImagePath = radarPath;
        pending.initiatedTime = std::chrono::steady_clock::now();
        pending.instructSent = false;
        m_pendingHandoffs.emplace(flightId, pending);

        // Return action: Server should build and send HANDOFF_INSTRUCT
        HandoffAction action{};
        action.type = HandoffActionType::SEND_HANDOFF_INSTRUCT;
        action.flightId = flightId;
        action.fromSectorId = currentSector;
        action.toSectorId = newSector;
        action.radarImagePath = radarPath;
        action.transitionResult = tr;

        return action;
    }

    // ---------------------------------------------------------------------------
    // Confirm HANDOFF_INSTRUCT sent → HANDOFF_INITIATED → HANDOFF_PENDING (#4)
    // ---------------------------------------------------------------------------
    HandoffAction HandoffManager::ConfirmInstructSent(FlightRegistry& registry,
        uint32_t flightId)
    {
        FlightRecord* record = registry.GetFlight(flightId);
        if (record == nullptr) {
            return MakeNoAction(flightId);
        }

        auto it = m_pendingHandoffs.find(flightId);
        if (it == m_pendingHandoffs.end()) {
            return MakeNoAction(flightId);
        }

        // Transition #4: HANDOFF_INITIATED → HANDOFF_PENDING
        TransitionResult tr = record->stateMachine.Transition(
            FlightState::HANDOFF_PENDING,
            "HANDOFF_INSTRUCT sent, timeout timer started"
        );

        if (!tr.success) {
            HandoffAction action{};
            action.type = HandoffActionType::NONE;
            action.flightId = flightId;
            action.fromSectorId = it->second.fromSectorId;
            action.toSectorId = it->second.toSectorId;
            action.transitionResult = tr;
            return action;
        }

        // Mark as sent and reset the timer to start the timeout window now
        it->second.instructSent = true;
        it->second.initiatedTime = std::chrono::steady_clock::now();

        // Return action: Server should start file transfer to this aircraft
        HandoffAction action{};
        action.type = HandoffActionType::START_FILE_TRANSFER;
        action.flightId = flightId;
        action.fromSectorId = it->second.fromSectorId;
        action.toSectorId = it->second.toSectorId;
        action.radarImagePath = it->second.radarImagePath;
        action.transitionResult = tr;

        return action;
    }

    // ---------------------------------------------------------------------------
    // Process HANDOFF_ACK → HANDOFF_COMPLETE (#5) → TRACKING (#7, auto)
    // ---------------------------------------------------------------------------
    HandoffAction HandoffManager::ProcessHandoffAck(FlightRegistry& registry,
        uint32_t flightId)
    {
        FlightRecord* record = registry.GetFlight(flightId);
        if (record == nullptr) {
            return MakeNoAction(flightId);
        }

        auto it = m_pendingHandoffs.find(flightId);
        if (it == m_pendingHandoffs.end()) {
            return MakeNoAction(flightId);
        }

        // Capture values BEFORE any erase — avoids use-after-erase (MISRA lifetime)
        const uint32_t fromSectorId = it->second.fromSectorId;
        const uint32_t newSectorId = it->second.toSectorId;

        // Transition #5: HANDOFF_PENDING → HANDOFF_COMPLETE
        TransitionResult tr5 = record->stateMachine.Transition(
            FlightState::HANDOFF_COMPLETE,
            "Aircraft HANDOFF_ACK received"
        );

        if (!tr5.success) {
            HandoffAction action{};
            action.type = HandoffActionType::NONE;
            action.flightId = flightId;
            action.fromSectorId = fromSectorId;
            action.toSectorId = newSectorId;
            action.transitionResult = tr5;
            return action;
        }

        // Transition #7: HANDOFF_COMPLETE → TRACKING (auto-transition, new sector)
        TransitionResult tr7 = record->stateMachine.Transition(
            FlightState::TRACKING,
            "Handoff complete, now tracking in new sector"
        );
        // tr7 should always succeed from HANDOFF_COMPLETE

        // Update the flight's sector assignment to the new sector
        registry.UpdateSector(flightId, newSectorId);

        // Remove from pending map — iterator invalidated after this line
        m_pendingHandoffs.erase(it);

        // Reset contact timer — we just heard from the aircraft
        record->stateMachine.RecordPacketReceived();

        HandoffAction action{};
        action.type = HandoffActionType::HANDOFF_COMPLETED;
        action.flightId = flightId;
        action.fromSectorId = fromSectorId;
        action.toSectorId = newSectorId;
        action.transitionResult = tr7;  // Return the final TRACKING transition

        return action;
    }

    // ---------------------------------------------------------------------------
    // Check pending handoffs for timeout → HANDOFF_FAILED (#6) → TRACKING (#8)
    // ---------------------------------------------------------------------------
    std::vector<HandoffAction> HandoffManager::CheckHandoffTimeouts(
        FlightRegistry& registry)
    {
        std::vector<HandoffAction> timedOut;
        std::vector<uint32_t> toRemove;

        const auto now = std::chrono::steady_clock::now();

        for (auto& pair : m_pendingHandoffs) {
            PendingHandoff& pending = pair.second;

            // Only check timeout if INSTRUCT has actually been sent (PENDING state)
            if (!pending.instructSent) {
                continue;
            }

            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - pending.initiatedTime).count();

            if (static_cast<uint64_t>(elapsed) < static_cast<uint64_t>(HANDOFF_TIMEOUT_MS)) {
                continue;  // Not timed out yet
            }

            // --- Timeout detected ---
            FlightRecord* record = registry.GetFlight(pending.flightId);
            if (record == nullptr) {
                toRemove.push_back(pending.flightId);
                continue;
            }

            // Transition #6: HANDOFF_PENDING → HANDOFF_FAILED
            TransitionResult tr6 = record->stateMachine.Transition(
                FlightState::HANDOFF_FAILED,
                "Handoff timeout — no HANDOFF_ACK within HANDOFF_TIMEOUT_MS"
            );

            // Transition #8: HANDOFF_FAILED → TRACKING (auto-revert, original sector)
            TransitionResult tr8 = record->stateMachine.Transition(
                FlightState::TRACKING,
                "Handoff failed, reverting to original sector"
            );

            // Sector stays at fromSectorId (no change — revert)

            HandoffAction action{};
            action.type = HandoffActionType::HANDOFF_TIMED_OUT;
            action.flightId = pending.flightId;
            action.fromSectorId = pending.fromSectorId;
            action.toSectorId = pending.toSectorId;
            action.transitionResult = tr8;  // Final state: back to TRACKING

            timedOut.push_back(action);
            toRemove.push_back(pending.flightId);
        }

        // Clean up expired entries
        for (const uint32_t id : toRemove) {
            m_pendingHandoffs.erase(id);
        }

        return timedOut;
    }

    // ---------------------------------------------------------------------------
    // Query helpers
    // ---------------------------------------------------------------------------
    bool HandoffManager::IsHandoffPending(uint32_t flightId) const
    {
        return (m_pendingHandoffs.find(flightId) != m_pendingHandoffs.end());
    }

    const PendingHandoff* HandoffManager::GetPendingHandoff(uint32_t flightId) const
    {
        auto it = m_pendingHandoffs.find(flightId);
        if (it == m_pendingHandoffs.end()) {
            return nullptr;
        }
        return &(it->second);
    }

    uint32_t HandoffManager::GetPendingHandoffCount() const
    {
        return static_cast<uint32_t>(m_pendingHandoffs.size());
    }

    // ---------------------------------------------------------------------------
    // Internal helpers
    // ---------------------------------------------------------------------------
    const SectorDefinition* HandoffManager::FindSector(uint32_t sectorId) const
    {
        for (const auto& sector : m_sectors) {
            if (sector.sectorId == sectorId) {
                return &sector;
            }
        }
        return nullptr;
    }

    HandoffAction HandoffManager::MakeNoAction(uint32_t flightId) const
    {
        HandoffAction action{};
        action.type = HandoffActionType::NONE;
        action.flightId = flightId;
        action.fromSectorId = 0U;
        action.toSectorId = 0U;

        // Zero-init the TransitionResult
        action.transitionResult.success = false;
        action.transitionResult.previousState = FlightState::IDLE;
        action.transitionResult.currentState = FlightState::IDLE;
        action.transitionResult.trigger = "No action";
        action.transitionResult.rejectionReason = nullptr;

        return action;
    }

} // namespace AeroTrack