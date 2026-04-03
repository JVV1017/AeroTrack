// =============================================================================
// StateMachine.cpp — Per-flight state machine implementation
// =============================================================================
// Requirements: REQ-STM-010, REQ-STM-020, REQ-STM-030, REQ-STM-040
// Standard:     MISRA C++ compliant (see StateMachine.h header comment)
// =============================================================================

#include "StateMachine.h"
#include "Config.h"       // LOST_CONTACT_TIMEOUT_MS

namespace AeroTrack {

    // ---------------------------------------------------------------------------
    // FlightStateToString — used by caller for Logger::LogStateChange
    // ---------------------------------------------------------------------------
    // MISRA 6-4-5 (V2520): Each case now terminates with break; single return
    // at function end satisfies 6-6-5 (V2506).
    // ---------------------------------------------------------------------------
    const char* FlightStateToString(FlightState state)
    {
        const char* result;
        switch (state) {
        case FlightState::IDLE:              result = "IDLE";               break;
        case FlightState::CONNECTED:         result = "CONNECTED";          break;
        case FlightState::TRACKING:          result = "TRACKING";           break;
        case FlightState::HANDOFF_INITIATED: result = "HANDOFF_INITIATED";  break;
        case FlightState::HANDOFF_PENDING:   result = "HANDOFF_PENDING";    break;
        case FlightState::HANDOFF_COMPLETE:  result = "HANDOFF_COMPLETE";   break;
        case FlightState::HANDOFF_FAILED:    result = "HANDOFF_FAILED";     break;
        case FlightState::LOST_CONTACT:      result = "LOST_CONTACT";       break;
        default:                             result = "UNKNOWN";             break;
        }
        return result;
    }

    // ---------------------------------------------------------------------------
    // Constructor — starts in IDLE, clock initialised to now
    // ---------------------------------------------------------------------------
    StateMachine::StateMachine(uint32_t flightId)
        : m_flightId(flightId)
        , m_currentState(FlightState::IDLE)
        , m_lastPacketTime(std::chrono::steady_clock::now())
    {}

    // ---------------------------------------------------------------------------
    // REQ-STM-020: Transition table — 10 valid transitions
    // ---------------------------------------------------------------------------
    //  #  | From              | To                | Trigger
    // ----|-------------------|-------------------|-------------------------------
    //  1  | IDLE              | CONNECTED         | Handshake complete
    //  2  | CONNECTED         | TRACKING          | First POSITION_REPORT
    //  3  | TRACKING          | HANDOFF_INITIATED | Sector boundary detected
    //  4  | HANDOFF_INITIATED | HANDOFF_PENDING   | Instruction sent, timer started
    //  5  | HANDOFF_PENDING   | HANDOFF_COMPLETE  | Aircraft sends HANDOFF_ACK
    //  6  | HANDOFF_PENDING   | HANDOFF_FAILED    | Timeout, no ACK from aircraft
    //  7  | HANDOFF_COMPLETE  | TRACKING          | Auto-transition (new sector)
    //  8  | HANDOFF_FAILED    | TRACKING          | Auto-revert (original sector)
    //  9  | TRACKING          | LOST_CONTACT      | Heartbeat timeout
    // 10  | LOST_CONTACT      | TRACKING          | Aircraft resumes communication
    // ---------------------------------------------------------------------------
    // MISRA 6-4-5 (V2520): Each case now terminates with break; single return
    // at function end satisfies 6-6-5 (V2506).
    // ---------------------------------------------------------------------------
    bool StateMachine::IsValidTransition(FlightState from, FlightState to) const
    {
        bool valid;
        switch (from) {
        case FlightState::IDLE:
            valid = (to == FlightState::CONNECTED);                          // #1
            break;

        case FlightState::CONNECTED:
            valid = (to == FlightState::TRACKING);                           // #2
            break;

        case FlightState::TRACKING:
            valid = (to == FlightState::HANDOFF_INITIATED)                   // #3
                || (to == FlightState::LOST_CONTACT);                       // #9
            break;

        case FlightState::HANDOFF_INITIATED:
            valid = (to == FlightState::HANDOFF_PENDING);                    // #4
            break;

        case FlightState::HANDOFF_PENDING:
            valid = (to == FlightState::HANDOFF_COMPLETE)                    // #5
                || (to == FlightState::HANDOFF_FAILED);                     // #6
            break;

        case FlightState::HANDOFF_COMPLETE:
            valid = (to == FlightState::TRACKING);                           // #7
            break;

        case FlightState::HANDOFF_FAILED:
            valid = (to == FlightState::TRACKING);                           // #8
            break;

        case FlightState::LOST_CONTACT:
            valid = (to == FlightState::TRACKING);                           // #10
            break;

        default:
            valid = false;   // Defensive: unknown state rejects all transitions
            break;
        }
        return valid;
    }

    // ---------------------------------------------------------------------------
    // REQ-STM-020 + REQ-STM-030: Attempt transition, reject if invalid
    // ---------------------------------------------------------------------------
    TransitionResult StateMachine::Transition(FlightState targetState, const char* trigger)
    {
        TransitionResult result{};
        result.previousState = m_currentState;
        result.trigger = trigger;
        result.rejectionReason = nullptr;

        if (IsValidTransition(m_currentState, targetState)) {
            // Valid transition — apply it
            m_currentState = targetState;
            result.success = true;
            result.currentState = m_currentState;
        }
        else {
            // REQ-STM-030: Invalid transition — reject and provide reason for logging
            result.success = false;
            result.currentState = m_currentState;  // State unchanged
            result.rejectionReason = "Invalid state transition rejected (REQ-STM-030)";
        }

        return result;
    }

    // ---------------------------------------------------------------------------
    // REQ-STM-040: Record inbound packet timestamp
    // ---------------------------------------------------------------------------
    void StateMachine::RecordPacketReceived()
    {
        m_lastPacketTime = std::chrono::steady_clock::now();
    }

    // ---------------------------------------------------------------------------
    // REQ-STM-040: Check contact-loss timeout
    // ---------------------------------------------------------------------------
    // Only triggers from TRACKING state. Other states have their own timeout
    // semantics (e.g., HANDOFF_PENDING has handoff timeout).
    // ---------------------------------------------------------------------------
    TransitionResult StateMachine::CheckContactTimeout()
    {
        TransitionResult result{};
        result.previousState = m_currentState;
        result.trigger = "Contact timeout check";
        result.rejectionReason = nullptr;
        result.success = false;
        result.currentState = m_currentState;

        // Only check timeout when in TRACKING state
        if (m_currentState == FlightState::TRACKING) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_lastPacketTime).count();

            // MISRA: explicit comparison, LOST_CONTACT_TIMEOUT_MS from Config.h
            if (static_cast<uint64_t>(elapsed) >= static_cast<uint64_t>(LOST_CONTACT_TIMEOUT_MS)) {
                m_currentState = FlightState::LOST_CONTACT;
                result.success = true;
                result.currentState = FlightState::LOST_CONTACT;
                result.trigger = "Heartbeat timeout (REQ-STM-040)";
            }
        }

        return result;
    }

    // ---------------------------------------------------------------------------
    // Accessors
    // ---------------------------------------------------------------------------
    FlightState StateMachine::GetCurrentState() const
    {
        return m_currentState;
    }

    uint32_t StateMachine::GetFlightId() const
    {
        return m_flightId;
    }

    uint64_t StateMachine::GetMillisSinceLastPacket() const
    {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_lastPacketTime).count();
        return static_cast<uint64_t>(elapsed);
    }

} // namespace AeroTrack