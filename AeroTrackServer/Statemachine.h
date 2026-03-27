// =============================================================================
// StateMachine.h — Per-flight state machine for AeroTrack Ground Control
// =============================================================================
// Requirements: REQ-STM-010, REQ-STM-020, REQ-STM-030, REQ-STM-040
// Standard:     MISRA C++ — no stream I/O, no raw new/delete, U suffix,
//               fixed-width types, explicit bool conditions
// =============================================================================
#pragma once

#include <cstdint>
#include <chrono>

namespace AeroTrack {

    // ---------------------------------------------------------------------------
    // REQ-STM-010: Flight states for each tracked aircraft
    // ---------------------------------------------------------------------------
    enum class FlightState : uint8_t {
        IDLE = 0U,
        CONNECTED = 1U,
        TRACKING = 2U,
        HANDOFF_INITIATED = 3U,
        HANDOFF_PENDING = 4U,
        HANDOFF_COMPLETE = 5U,
        HANDOFF_FAILED = 6U,
        LOST_CONTACT = 7U
    };

    // Total number of defined states (used for bounds checking)
    constexpr uint8_t FLIGHT_STATE_COUNT = 8U;

    // Convert FlightState to human-readable string (for logging via Logger)
    const char* FlightStateToString(FlightState state);

    // ---------------------------------------------------------------------------
    // TransitionResult — returned by every transition attempt
    // ---------------------------------------------------------------------------
    // The caller (Server/FlightRegistry) uses this to drive Logger::LogStateChange
    // and Logger::LogError. StateMachine itself performs NO I/O (MISRA rule 10).
    // ---------------------------------------------------------------------------
    struct TransitionResult {
        bool        success{ false };                         // true = transition accepted
        FlightState previousState{ FlightState::IDLE };       // state before attempt
        FlightState currentState{ FlightState::IDLE };        // state after attempt (unchanged if rejected)
        const char* trigger{ nullptr };                       // event that caused the attempt
        const char* rejectionReason{ nullptr };               // nullptr if success, else human-readable reason
    };

    // ---------------------------------------------------------------------------
    // REQ-STM-010 to 040: Per-flight state machine
    // ---------------------------------------------------------------------------
    // Design notes:
    //   - One instance per tracked aircraft (created by FlightRegistry)
    //   - Pure logic class — all I/O delegated to caller via TransitionResult
    //   - LOST_CONTACT timer uses std::chrono::steady_clock (monotonic, no drift)
    //   - 10 valid transitions enforced per REQ-STM-020
    //   - All invalid transitions rejected per REQ-STM-030
    // ---------------------------------------------------------------------------
    class StateMachine {
    public:
        // -----------------------------------------------------------------------
        // Construction
        // -----------------------------------------------------------------------
        // Initialises in IDLE state with the packet-received clock set to now.
        explicit StateMachine(uint32_t flightId);

        // -----------------------------------------------------------------------
        // REQ-STM-020 / REQ-STM-030: Attempt a state transition
        // -----------------------------------------------------------------------
        // Parameters:
        //   targetState — the desired next state
        //   trigger     — human-readable event description (for log traceability)
        //
        // Returns:
        //   TransitionResult with success=true  if transition is in the valid set,
        //                    or  success=false if transition is invalid (REQ-STM-030).
        //   In both cases, previousState and currentState are populated so the
        //   caller can log via Logger::LogStateChange or Logger::LogError.
        // -----------------------------------------------------------------------
        TransitionResult Transition(FlightState targetState, const char* trigger);

        // -----------------------------------------------------------------------
        // REQ-STM-040: Record that a packet was received from this aircraft
        // -----------------------------------------------------------------------
        // Must be called on every inbound packet (POSITION_REPORT, HEARTBEAT,
        // HANDOFF_ACK, etc.) to reset the contact-loss timer.
        // -----------------------------------------------------------------------
        void RecordPacketReceived();

        // -----------------------------------------------------------------------
        // REQ-STM-040: Check whether contact has been lost
        // -----------------------------------------------------------------------
        // Call this periodically from the server main loop.
        // If current state is TRACKING and elapsed time since last packet exceeds
        // LOST_CONTACT_TIMEOUT_MS (Config.h), auto-transitions to LOST_CONTACT.
        //
        // Returns:
        //   TransitionResult with success=true  if state changed to LOST_CONTACT,
        //                    or  success=false if no timeout detected (no-op).
        // -----------------------------------------------------------------------
        TransitionResult CheckContactTimeout();

        // -----------------------------------------------------------------------
        // Accessors
        // -----------------------------------------------------------------------
        FlightState GetCurrentState() const;
        uint32_t    GetFlightId() const;

        // Elapsed milliseconds since last packet (for UI display)
        uint64_t GetMillisSinceLastPacket() const;

    private:
        // -----------------------------------------------------------------------
        // REQ-STM-020: Transition validation
        // -----------------------------------------------------------------------
        // Returns true only for the 10 explicitly defined valid transitions.
        // -----------------------------------------------------------------------
        bool IsValidTransition(FlightState from, FlightState to) const;

        uint32_t    m_flightId;
        FlightState m_currentState;

        // REQ-STM-040: Monotonic clock for contact-loss detection
        std::chrono::steady_clock::time_point m_lastPacketTime;
    };

} // namespace AeroTrack