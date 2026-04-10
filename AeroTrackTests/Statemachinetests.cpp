// =============================================================================
// Statemachinetests.cpp — MSTest unit tests for StateMachine
// =============================================================================
// DO-178C DAL-C  |  AeroTrack Ground Control
// Framework:      Microsoft CppUnitTestFramework (MSTest)
//
// Requirements covered:
//   REQ-STM-010  FlightState enum and IDLE initial state
//   REQ-STM-020  10 valid state transitions accepted
//   REQ-STM-030  Invalid state transitions rejected
//   REQ-STM-040  Contact-loss timer resets and timeout detection
// =============================================================================

#include "TestCommon.h"

namespace AeroTrackTests
{
    TEST_CLASS(StateMachineTests)
    {
    public:

        // =====================================================================
        // REQ-STM-010 — Initial state and flight ID accessor
        // =====================================================================

        TEST_METHOD(InitialState_IsIdle)
        {
            // REQ-STM-010
            StateMachine sm(42U);
            Assert::IsTrue(sm.GetCurrentState() == FlightState::IDLE,
                L"StateMachine must start in IDLE state (REQ-STM-010)");
        }

        TEST_METHOD(FlightId_ReturnedCorrectly)
        {
            // REQ-STM-010
            StateMachine sm(801U);
            Assert::AreEqual(801U, sm.GetFlightId(),
                L"GetFlightId must return the value passed to the constructor");
        }

        // =====================================================================
        // REQ-STM-020 — Valid transition #1: IDLE → CONNECTED
        // =====================================================================

        TEST_METHOD(Transition1_Idle_To_Connected_Succeeds)
        {
            // REQ-STM-020  (#1 IDLE → CONNECTED)
            StateMachine sm(1U);
            TransitionResult result = sm.Transition(FlightState::CONNECTED, "handshake complete");

            Assert::IsTrue(result.success,
                L"IDLE → CONNECTED must succeed (REQ-STM-020 transition #1)");
            Assert::IsTrue(result.previousState == FlightState::IDLE,
                L"previousState must be IDLE");
            Assert::IsTrue(result.currentState == FlightState::CONNECTED,
                L"currentState must be CONNECTED after transition");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::CONNECTED,
                L"StateMachine must reflect CONNECTED state");
        }

        // =====================================================================
        // REQ-STM-020 — Valid transition #2: CONNECTED → TRACKING
        // =====================================================================

        TEST_METHOD(Transition2_Connected_To_Tracking_Succeeds)
        {
            // REQ-STM-020  (#2 CONNECTED → TRACKING)
            StateMachine sm(2U);
            (void)sm.Transition(FlightState::CONNECTED, "handshake");
            TransitionResult result = sm.Transition(FlightState::TRACKING, "first POSITION_REPORT");

            Assert::IsTrue(result.success,
                L"CONNECTED → TRACKING must succeed (REQ-STM-020 transition #2)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::TRACKING,
                L"StateMachine must be in TRACKING state");
        }

        // =====================================================================
        // REQ-STM-020 — Valid transition #3: TRACKING → HANDOFF_INITIATED
        // =====================================================================

        TEST_METHOD(Transition3_Tracking_To_HandoffInitiated_Succeeds)
        {
            // REQ-STM-020  (#3 TRACKING → HANDOFF_INITIATED)
            StateMachine sm(3U);
            (void)sm.Transition(FlightState::CONNECTED, "handshake");
            (void)sm.Transition(FlightState::TRACKING, "first position");
            TransitionResult result = sm.Transition(FlightState::HANDOFF_INITIATED, "sector boundary detected");

            Assert::IsTrue(result.success,
                L"TRACKING → HANDOFF_INITIATED must succeed (REQ-STM-020 transition #3)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::HANDOFF_INITIATED,
                L"StateMachine must be in HANDOFF_INITIATED state");
        }

        // =====================================================================
        // REQ-STM-020 — Valid transition #4: HANDOFF_INITIATED → HANDOFF_PENDING
        // =====================================================================

        TEST_METHOD(Transition4_HandoffInitiated_To_HandoffPending_Succeeds)
        {
            // REQ-STM-020  (#4 HANDOFF_INITIATED → HANDOFF_PENDING)
            StateMachine sm(4U);
            (void)sm.Transition(FlightState::CONNECTED, "handshake");
            (void)sm.Transition(FlightState::TRACKING, "first position");
            (void)sm.Transition(FlightState::HANDOFF_INITIATED, "boundary detected");
            TransitionResult result = sm.Transition(FlightState::HANDOFF_PENDING, "instruction sent");

            Assert::IsTrue(result.success,
                L"HANDOFF_INITIATED → HANDOFF_PENDING must succeed (REQ-STM-020 transition #4)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::HANDOFF_PENDING,
                L"StateMachine must be in HANDOFF_PENDING state");
        }

        // =====================================================================
        // REQ-STM-020 — Valid transition #5: HANDOFF_PENDING → HANDOFF_COMPLETE
        // =====================================================================

        TEST_METHOD(Transition5_HandoffPending_To_HandoffComplete_Succeeds)
        {
            // REQ-STM-020  (#5 HANDOFF_PENDING → HANDOFF_COMPLETE)
            StateMachine sm(5U);
            (void)sm.Transition(FlightState::CONNECTED, "h");
            (void)sm.Transition(FlightState::TRACKING, "t");
            (void)sm.Transition(FlightState::HANDOFF_INITIATED, "b");
            (void)sm.Transition(FlightState::HANDOFF_PENDING, "s");
            TransitionResult result = sm.Transition(FlightState::HANDOFF_COMPLETE, "HANDOFF_ACK received");

            Assert::IsTrue(result.success,
                L"HANDOFF_PENDING → HANDOFF_COMPLETE must succeed (REQ-STM-020 transition #5)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::HANDOFF_COMPLETE,
                L"StateMachine must be in HANDOFF_COMPLETE state");
        }

        // =====================================================================
        // REQ-STM-020 — Valid transition #6: HANDOFF_PENDING → HANDOFF_FAILED
        // =====================================================================

        TEST_METHOD(Transition6_HandoffPending_To_HandoffFailed_Succeeds)
        {
            // REQ-STM-020  (#6 HANDOFF_PENDING → HANDOFF_FAILED)
            StateMachine sm(6U);
            (void)sm.Transition(FlightState::CONNECTED, "h");
            (void)sm.Transition(FlightState::TRACKING, "t");
            (void)sm.Transition(FlightState::HANDOFF_INITIATED, "b");
            (void)sm.Transition(FlightState::HANDOFF_PENDING, "s");
            TransitionResult result = sm.Transition(FlightState::HANDOFF_FAILED, "handoff timeout");

            Assert::IsTrue(result.success,
                L"HANDOFF_PENDING → HANDOFF_FAILED must succeed (REQ-STM-020 transition #6)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::HANDOFF_FAILED,
                L"StateMachine must be in HANDOFF_FAILED state");
        }

        // =====================================================================
        // REQ-STM-020 — Valid transition #7: HANDOFF_COMPLETE → TRACKING
        // =====================================================================

        TEST_METHOD(Transition7_HandoffComplete_To_Tracking_Succeeds)
        {
            // REQ-STM-020  (#7 HANDOFF_COMPLETE → TRACKING)
            StateMachine sm(7U);
            (void)sm.Transition(FlightState::CONNECTED, "h");
            (void)sm.Transition(FlightState::TRACKING, "t");
            (void)sm.Transition(FlightState::HANDOFF_INITIATED, "b");
            (void)sm.Transition(FlightState::HANDOFF_PENDING, "s");
            (void)sm.Transition(FlightState::HANDOFF_COMPLETE, "ack");
            TransitionResult result = sm.Transition(FlightState::TRACKING, "new sector active");

            Assert::IsTrue(result.success,
                L"HANDOFF_COMPLETE → TRACKING must succeed (REQ-STM-020 transition #7)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::TRACKING,
                L"StateMachine must be back in TRACKING state after handoff completion");
        }

        // =====================================================================
        // REQ-STM-020 — Valid transition #8: HANDOFF_FAILED → TRACKING
        // =====================================================================

        TEST_METHOD(Transition8_HandoffFailed_To_Tracking_Succeeds)
        {
            // REQ-STM-020  (#8 HANDOFF_FAILED → TRACKING)
            StateMachine sm(8U);
            (void)sm.Transition(FlightState::CONNECTED, "h");
            (void)sm.Transition(FlightState::TRACKING, "t");
            (void)sm.Transition(FlightState::HANDOFF_INITIATED, "b");
            (void)sm.Transition(FlightState::HANDOFF_PENDING, "s");
            (void)sm.Transition(FlightState::HANDOFF_FAILED, "timeout");
            TransitionResult result = sm.Transition(FlightState::TRACKING, "auto-revert");

            Assert::IsTrue(result.success,
                L"HANDOFF_FAILED → TRACKING must succeed (REQ-STM-020 transition #8)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::TRACKING,
                L"StateMachine must be back in TRACKING state after handoff failure revert");
        }

        // =====================================================================
        // REQ-STM-020 — Valid transition #9: TRACKING → LOST_CONTACT
        // =====================================================================

        TEST_METHOD(Transition9_Tracking_To_LostContact_Succeeds)
        {
            // REQ-STM-020  (#9 TRACKING → LOST_CONTACT)
            StateMachine sm(9U);
            (void)sm.Transition(FlightState::CONNECTED, "h");
            (void)sm.Transition(FlightState::TRACKING, "t");
            TransitionResult result = sm.Transition(FlightState::LOST_CONTACT, "heartbeat timeout");

            Assert::IsTrue(result.success,
                L"TRACKING → LOST_CONTACT must succeed (REQ-STM-020 transition #9)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::LOST_CONTACT,
                L"StateMachine must be in LOST_CONTACT state");
        }

        // =====================================================================
        // REQ-STM-020 — Valid transition #10: LOST_CONTACT → TRACKING
        // =====================================================================

        TEST_METHOD(Transition10_LostContact_To_Tracking_Succeeds)
        {
            // REQ-STM-020  (#10 LOST_CONTACT → TRACKING)
            StateMachine sm(10U);
            (void)sm.Transition(FlightState::CONNECTED, "h");
            (void)sm.Transition(FlightState::TRACKING, "t");
            (void)sm.Transition(FlightState::LOST_CONTACT, "timeout");
            TransitionResult result = sm.Transition(FlightState::TRACKING, "aircraft resumed");

            Assert::IsTrue(result.success,
                L"LOST_CONTACT → TRACKING must succeed (REQ-STM-020 transition #10)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::TRACKING,
                L"StateMachine must be back in TRACKING after contact recovery");
        }

        // =====================================================================
        // REQ-STM-030 — Invalid transitions are rejected
        // =====================================================================

        TEST_METHOD(InvalidTransition_Idle_To_Tracking_Rejected)
        {
            // REQ-STM-030
            StateMachine sm(20U);
            TransitionResult result = sm.Transition(FlightState::TRACKING, "skip step");

            Assert::IsFalse(result.success,
                L"IDLE → TRACKING must be rejected (REQ-STM-030)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::IDLE,
                L"State must remain IDLE after rejected transition");
            Assert::IsNotNull(result.rejectionReason,
                L"rejectionReason must be populated for rejected transition (REQ-STM-030)");
        }

        TEST_METHOD(InvalidTransition_Connected_To_HandoffInitiated_Rejected)
        {
            // REQ-STM-030
            StateMachine sm(21U);
            (void)sm.Transition(FlightState::CONNECTED, "h");
            TransitionResult result = sm.Transition(FlightState::HANDOFF_INITIATED, "illegal");

            Assert::IsFalse(result.success,
                L"CONNECTED → HANDOFF_INITIATED must be rejected (REQ-STM-030)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::CONNECTED,
                L"State must remain CONNECTED after rejected transition");
        }

        TEST_METHOD(InvalidTransition_Tracking_To_Connected_Rejected)
        {
            // REQ-STM-030
            StateMachine sm(22U);
            (void)sm.Transition(FlightState::CONNECTED, "h");
            (void)sm.Transition(FlightState::TRACKING, "t");
            TransitionResult result = sm.Transition(FlightState::CONNECTED, "backward");

            Assert::IsFalse(result.success,
                L"TRACKING → CONNECTED must be rejected (REQ-STM-030)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::TRACKING,
                L"State must remain TRACKING after rejected backward transition");
        }

        TEST_METHOD(InvalidTransition_HandoffFailed_To_LostContact_Rejected)
        {
            // REQ-STM-030
            StateMachine sm(23U);
            (void)sm.Transition(FlightState::CONNECTED, "h");
            (void)sm.Transition(FlightState::TRACKING, "t");
            (void)sm.Transition(FlightState::HANDOFF_INITIATED, "b");
            (void)sm.Transition(FlightState::HANDOFF_PENDING, "s");
            (void)sm.Transition(FlightState::HANDOFF_FAILED, "timeout");
            TransitionResult result = sm.Transition(FlightState::LOST_CONTACT, "illegal");

            Assert::IsFalse(result.success,
                L"HANDOFF_FAILED → LOST_CONTACT must be rejected (REQ-STM-030)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::HANDOFF_FAILED,
                L"State must remain HANDOFF_FAILED after rejected transition");
        }

        TEST_METHOD(InvalidTransition_Idle_To_Idle_Rejected)
        {
            // REQ-STM-030 — self-transitions must be rejected
            StateMachine sm(24U);
            TransitionResult result = sm.Transition(FlightState::IDLE, "no-op");

            Assert::IsFalse(result.success,
                L"IDLE → IDLE (self-transition) must be rejected (REQ-STM-030)");
        }

        // =====================================================================
        // REQ-STM-030 — TransitionResult fields on rejection
        // =====================================================================

        TEST_METHOD(RejectedTransition_PreviousStateUnchanged)
        {
            // REQ-STM-030
            StateMachine sm(25U);
            (void)sm.Transition(FlightState::CONNECTED, "h");
            TransitionResult result = sm.Transition(FlightState::LOST_CONTACT, "illegal");

            Assert::IsTrue(result.previousState == FlightState::CONNECTED,
                L"previousState must reflect state before rejected attempt");
            Assert::IsTrue(result.currentState == FlightState::CONNECTED,
                L"currentState must remain CONNECTED after rejected transition");
        }

        // =====================================================================
        // REQ-STM-040 — RecordPacketReceived resets the contact-loss timer
        // =====================================================================

        TEST_METHOD(RecordPacketReceived_ResetsTimer)
        {
            // REQ-STM-040
            StateMachine sm(30U);

            // Sleep briefly so some elapsed time accumulates
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            uint64_t millisBefore = sm.GetMillisSinceLastPacket();

            sm.RecordPacketReceived();
            uint64_t millisAfter = sm.GetMillisSinceLastPacket();

            Assert::IsTrue(millisAfter < millisBefore,
                L"RecordPacketReceived must reset the contact-loss timer "
                L"so elapsed time decreases (REQ-STM-040)");
        }

        // =====================================================================
        // REQ-STM-040 — CheckContactTimeout detects loss only from TRACKING
        // =====================================================================

        TEST_METHOD(CheckContactTimeout_NoTimeout_InIdleState)
        {
            // REQ-STM-040 — timeout check must be a no-op outside TRACKING
            StateMachine sm(31U);
            // State is IDLE — no timeout should fire
            TransitionResult result = sm.CheckContactTimeout();

            Assert::IsFalse(result.success,
                L"CheckContactTimeout must not fire when state is IDLE (REQ-STM-040)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::IDLE,
                L"State must remain IDLE after no-op timeout check");
        }

        TEST_METHOD(CheckContactTimeout_NoTimeout_InConnectedState)
        {
            // REQ-STM-040 — timeout check must be a no-op in CONNECTED state
            StateMachine sm(32U);
            (void)sm.Transition(FlightState::CONNECTED, "h");
            TransitionResult result = sm.CheckContactTimeout();

            Assert::IsFalse(result.success,
                L"CheckContactTimeout must not fire when state is CONNECTED (REQ-STM-040)");
        }

        TEST_METHOD(CheckContactTimeout_NoTimeout_WhenTimerFresh)
        {
            // REQ-STM-040 — must NOT fire immediately after RecordPacketReceived
            StateMachine sm(33U);
            (void)sm.Transition(FlightState::CONNECTED, "h");
            (void)sm.Transition(FlightState::TRACKING, "t");

            sm.RecordPacketReceived(); // reset timer right now
            TransitionResult result = sm.CheckContactTimeout();

            Assert::IsFalse(result.success,
                L"CheckContactTimeout must not fire immediately after a packet is received (REQ-STM-040)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::TRACKING,
                L"State must remain TRACKING when contact timer has not expired");
        }

        // =====================================================================
        // REQ-STM-010 — FlightStateToString returns correct strings
        // =====================================================================

        TEST_METHOD(FlightStateToString_ReturnsCorrectLabels)
        {
            // REQ-STM-010
            Assert::AreEqual("IDLE", FlightStateToString(FlightState::IDLE));
            Assert::AreEqual("CONNECTED", FlightStateToString(FlightState::CONNECTED));
            Assert::AreEqual("TRACKING", FlightStateToString(FlightState::TRACKING));
            Assert::AreEqual("HANDOFF_INITIATED", FlightStateToString(FlightState::HANDOFF_INITIATED));
            Assert::AreEqual("HANDOFF_PENDING", FlightStateToString(FlightState::HANDOFF_PENDING));
            Assert::AreEqual("HANDOFF_COMPLETE", FlightStateToString(FlightState::HANDOFF_COMPLETE));
            Assert::AreEqual("HANDOFF_FAILED", FlightStateToString(FlightState::HANDOFF_FAILED));
            Assert::AreEqual("LOST_CONTACT", FlightStateToString(FlightState::LOST_CONTACT));
        }

        // =====================================================================
        // REQ-STM-020 — Trigger string is echoed in TransitionResult
        // =====================================================================

        TEST_METHOD(TransitionResult_TriggerMatchesInput)
        {
            // REQ-STM-020
            StateMachine sm(40U);
            const char* trigger = "handshake complete";
            TransitionResult result = sm.Transition(FlightState::CONNECTED, trigger);

            Assert::IsTrue(result.success,
                L"Transition must succeed for this test to be meaningful");
            Assert::AreEqual(trigger, result.trigger,
                L"TransitionResult::trigger must match the string passed to Transition()");
        }

        TEST_METHOD(CheckContactTimeout_AfterTimeout_TransitionsToLostContact)
        {
            // REQ-STM-040
            // NOTE: This test sleeps for LOST_CONTACT_TIMEOUT_MS + 50ms.
            // With the production value this will be slow. If LOST_CONTACT_TIMEOUT_MS
            // exceeds 1000ms, consider adding a configurable timeout constructor
            // overload to StateMachine to make this test faster.
            StateMachine sm(99U);
            (void)sm.Transition(FlightState::CONNECTED, "h");
            (void)sm.Transition(FlightState::TRACKING, "t");

            std::this_thread::sleep_for(
                std::chrono::milliseconds(LOST_CONTACT_TIMEOUT_MS + 50U));

            TransitionResult result = sm.CheckContactTimeout();

            Assert::IsTrue(result.success,
                L"CheckContactTimeout must fire and return success=true "
                L"after the timeout expires in TRACKING state (REQ-STM-040)");
            Assert::IsTrue(sm.GetCurrentState() == FlightState::LOST_CONTACT,
                L"State must be LOST_CONTACT after contact timeout fires (REQ-STM-040)");
        }

    };  // TEST_CLASS(StateMachineTests)

}  // namespace AeroTrackTests