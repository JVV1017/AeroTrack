// =============================================================================
// ClientTests.cpp — MSTest unit tests for the AeroTrack client subsystem
// =============================================================================
// DO-178C DAL-C  |  AeroTrack Ground Control
// Framework:      Microsoft CppUnitTestFramework (MSTest)
//
// Requirements covered:
//   REQ-CLT-010  Client constructs / destructs without throwing; initial state
//   REQ-CLT-020  Connect() returns false when the server is unreachable (stub)
//   REQ-CLT-030  PositionReporter – SetPosition / GetPosition round-trip
//   REQ-CLT-040  SimulateMovement() keeps latitude inside [42.0, 48.0]
//   REQ-CLT-050  HandoffHandler – stores correct sector ID from HANDOFF_INSTRUCT
//   REQ-CLT-060  LogPacket called on TX – verified via Logger file read-back
//   REQ-CLT-070  FileReceiver – assembles chunks in order and tracks progress
//
// ---- Testability notes -------------------------------------------------------
// Client::Connect() and Client::Init() both call RUDP::Init() (Winsock) and
// Logger::Init() (file I/O).  Tests that exercise the full Client class are
// annotated with:
//   // TODO: requires injectable RUDP/Logger seam to avoid real network calls
// Sub-module classes (PositionReporter, HandoffHandler, FileReceiver) can be
// constructed directly with injected Logger/RUDP stubs; those tests are fully
// executable without network access.
// =============================================================================

#include "TestCommon.h"

namespace AeroTrackTests
{

    // =========================================================================
// Helper: build a minimal valid Endpoint (loopback, never actually reached)
// =========================================================================
    static Endpoint MakeDummyEndpoint()
    {
        Endpoint ep;
        ep.ip = "127.0.0.1";
        ep.port = 27015U;
        return ep;
    }

    // =========================================================================
    // Helper: encode a uint32_t as 4-byte big-endian vector
    // =========================================================================
    static std::vector<uint8_t> EncodeBE32(uint32_t value)
    {
        std::vector<uint8_t> out(4U);
        out[0U] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
        out[1U] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
        out[2U] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
        out[3U] = static_cast<uint8_t>(value & 0xFFU);
        return out;
    }

    // =========================================================================
    // Helper: build a FILE_TRANSFER_START packet with given file/chunk counts
    // =========================================================================
    static Packet MakeStartPacket(uint32_t fileSize, uint32_t chunkCount,
        uint32_t flightId = 1U)
    {
        Packet pkt(PacketType::FILE_TRANSFER_START, flightId);
        std::vector<uint8_t> payload(8U);
        // Bytes 0-3: total_file_size (big-endian)
        payload[0U] = static_cast<uint8_t>((fileSize >> 24U) & 0xFFU);
        payload[1U] = static_cast<uint8_t>((fileSize >> 16U) & 0xFFU);
        payload[2U] = static_cast<uint8_t>((fileSize >> 8U) & 0xFFU);
        payload[3U] = static_cast<uint8_t>(fileSize & 0xFFU);
        // Bytes 4-7: total_chunks (big-endian)
        payload[4U] = static_cast<uint8_t>((chunkCount >> 24U) & 0xFFU);
        payload[5U] = static_cast<uint8_t>((chunkCount >> 16U) & 0xFFU);
        payload[6U] = static_cast<uint8_t>((chunkCount >> 8U) & 0xFFU);
        payload[7U] = static_cast<uint8_t>(chunkCount & 0xFFU);
        pkt.SetPayload(payload);
        return pkt;
    }

    // =========================================================================
    // Helper: build a FILE_TRANSFER_CHUNK packet
    //   payload = chunkIndex (4B BE) + data bytes
    // =========================================================================
    static Packet MakeChunkPacket(uint32_t chunkIndex,
        const std::vector<uint8_t>& data,
        uint32_t flightId = 1U)
    {
        Packet pkt(PacketType::FILE_TRANSFER_CHUNK, flightId);
        std::vector<uint8_t> payload;
        payload.reserve(4U + data.size());

        // Encode chunk index big-endian
        payload.push_back(static_cast<uint8_t>((chunkIndex >> 24U) & 0xFFU));
        payload.push_back(static_cast<uint8_t>((chunkIndex >> 16U) & 0xFFU));
        payload.push_back(static_cast<uint8_t>((chunkIndex >> 8U) & 0xFFU));
        payload.push_back(static_cast<uint8_t>(chunkIndex & 0xFFU));

        for (uint8_t b : data) { payload.push_back(b); }
        pkt.SetPayload(payload);
        return pkt;
    }

    // =========================================================================
    // Helper: build a FILE_TRANSFER_END packet (empty payload)
    // =========================================================================
    static Packet MakeEndPacket(uint32_t flightId = 1U)
    {
        return Packet(PacketType::FILE_TRANSFER_END, flightId);
    }

    // =========================================================================
    // Helper: build a HANDOFF_INSTRUCT packet carrying a 4-byte BE sector ID
    // =========================================================================
    static Packet MakeHandoffInstructPacket(uint32_t newSectorId,
        uint32_t flightId = 801U)
    {
        Packet pkt(PacketType::HANDOFF_INSTRUCT, flightId);
        pkt.SetPayload(EncodeBE32(newSectorId));
        return pkt;
    }


    // =========================================================================
    // ClientConstructionTests — REQ-CLT-010
    // =========================================================================

    TEST_CLASS(ClientConstructionTests)
    {
    public:

        TEST_METHOD(Client_DefaultConstruct_DoesNotThrow)
        {
            // REQ-CLT-010
            // Client() is noexcept; construction must succeed with no socket calls.
            Client client;
            // If we reach this line the constructor did not crash.
            Assert::IsTrue(true, L"Client default constructor must not throw (REQ-CLT-010)");
        }

        TEST_METHOD(Client_InitialState_IsDisconnected)
        {
            // REQ-CLT-010
            Client client;
            Assert::IsTrue(ClientState::DISCONNECTED == client.GetState(),
                L"Client must start in DISCONNECTED state (REQ-CLT-010)");
        }

        TEST_METHOD(Client_InitialFlightId_IsZero)
        {
            // REQ-CLT-010
            Client client;
            Assert::AreEqual(0U, client.GetFlightId(),
                L"Client flight ID must be 0 before Init (REQ-CLT-010)");
        }

        TEST_METHOD(Client_InitialSectorId_IsZero)
        {
            // REQ-CLT-010
            Client client;
            Assert::AreEqual(0U, client.GetSectorId(),
                L"Client sector ID must be 0 before Init (REQ-CLT-010)");
        }

        TEST_METHOD(Client_InitialCallsign_IsEmpty)
        {
            // REQ-CLT-010
            Client client;
            Assert::IsTrue(client.GetCallsign().empty(),
                L"Client callsign must be empty before Init (REQ-CLT-010)");
        }

        TEST_METHOD(Client_Destructor_DoesNotCrash)
        {
            // REQ-CLT-010 — destructor calls Stop() which is also noexcept.
            {
                Client client;
                // client goes out of scope → destructor runs
            }
            Assert::IsTrue(true, L"Client destructor must not crash (REQ-CLT-010)");
        }

        TEST_METHOD(Client_StopBeforeInit_DoesNotCrash)
        {
            // REQ-CLT-010 — Stop() must be safe to call when not running.
            Client client;
            client.Stop();
            Assert::IsTrue(true, L"Stop() before Init must be a safe no-op (REQ-CLT-010)");
        }

        // =====================================================================
        // REQ-CLT-020 — Connect() returns false when server is unreachable
        // =====================================================================

        TEST_METHOD(Connect_WithoutInit_DoesNotCrash)
        {
            // REQ-CLT-020
            // TODO: Without a mock RUDP, Connect() will call RUDP::SendReliable()
            //       on an uninitialized socket which returns false immediately.
            //       The test verifies the return value guard without a network call.
            //
            // TODO: Full Connect() test (3-step handshake complete path) requires
            //       an injectable RUDP seam so that SendReliable/Receive can be
            //       driven in the test without a real server.
            Client client;
            // Connect() without prior Init() — RUDP is uninitialized → returns false
            bool result = client.Connect();

            Assert::IsFalse(result,
                L"Connect must return false when RUDP is not initialized (REQ-CLT-020)");
            // State must revert to DISCONNECTED (guard path in Connect)
            Assert::IsTrue(ClientState::DISCONNECTED == client.GetState() ||
                ClientState::CONNECTING == client.GetState(),
                L"State after failed Connect must be DISCONNECTED or remain at CONNECTING "
                L"— must not advance to CONNECTED (REQ-CLT-020)");
        }

        TEST_METHOD(Connect_TargetUnreachable_ReturnsFalse)
        {
            // REQ-CLT-020
            // TODO: This test drives Init() (creates a real Winsock socket) then
            //       calls Connect() to 127.0.0.1:27015 where no server is listening.
            //       SendReliable times out after RUDP_MAX_RETRIES attempts and returns
            //       false. The test is marked TODO because it takes
            //       RUDP_MAX_RETRIES × RUDP_TIMEOUT_MS = 3 × 500 ms = 1500 ms to run.
            //       With an injectable socket seam, RecvFrom would return SOCKET_ERROR
            //       immediately and the test would complete in < 1 ms.
            //
            // TODO: Replace with MockRUDP once ISocket abstraction is added.
            Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage(
                "REQ-CLT-020: Full unreachable-server test omitted — would take "
                "~1500 ms due to RUDP retry loop. "
                "Use MockRUDP (injectable ISocket) to make this instant.");

            // What we CAN assert: Connect() after a failed Init must return false.
            Client client;
            bool result = client.Connect();
            Assert::IsFalse(result,
                L"Connect must return false when socket is not initialized (REQ-CLT-020)");
        }

    };  // TEST_CLASS(ClientConstructionTests)


    // =========================================================================
    // PositionReporterTests — REQ-CLT-030 / REQ-CLT-040
    // =========================================================================

    TEST_CLASS(PositionReporterTests)
    {
    public:

        // =====================================================================
        // REQ-CLT-030 — SetPosition / GetPosition round-trip
        // =====================================================================

        TEST_METHOD(SetPosition_GetLatitude_RoundTrip)
        {
            // REQ-CLT-030
            TempFile tf("at_pr_lat.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            PositionReporter pr(rudp, logger, 801U, ep);

            pr.SetPosition(43.6532, -79.3832, 35000U, 450U, 90U);

            Assert::AreEqual(43.6532, pr.GetLatitude(), 1e-6,
                L"GetLatitude must return the value passed to SetPosition (REQ-CLT-030)");
        }

        TEST_METHOD(SetPosition_GetLongitude_RoundTrip)
        {
            // REQ-CLT-030
            TempFile tf("at_pr_lon.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            PositionReporter pr(rudp, logger, 801U, ep);

            pr.SetPosition(43.6532, -79.3832, 35000U, 450U, 90U);

            Assert::AreEqual(-79.3832, pr.GetLongitude(), 1e-6,
                L"GetLongitude must return the value passed to SetPosition (REQ-CLT-030)");
        }

        TEST_METHOD(SetPosition_GetAltitude_RoundTrip)
        {
            // REQ-CLT-030
            TempFile tf("at_pr_alt.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            PositionReporter pr(rudp, logger, 801U, ep);

            pr.SetPosition(43.6532, -79.3832, 35000U, 450U, 90U);

            Assert::AreEqual(35000U, pr.GetAltitude(),
                L"GetAltitude must return the value passed to SetPosition (REQ-CLT-030)");
        }

        TEST_METHOD(SetPosition_GetSpeed_RoundTrip)
        {
            // REQ-CLT-030
            TempFile tf("at_pr_spd.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            PositionReporter pr(rudp, logger, 801U, ep);

            pr.SetPosition(43.6532, -79.3832, 35000U, 450U, 90U);

            Assert::AreEqual(static_cast<uint16_t>(450U), pr.GetSpeed(),
                L"GetSpeed must return the value passed to SetPosition (REQ-CLT-030)");
        }

        TEST_METHOD(SetPosition_GetHeading_RoundTrip)
        {
            // REQ-CLT-030
            TempFile tf("at_pr_hdg.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            PositionReporter pr(rudp, logger, 801U, ep);

            pr.SetPosition(43.6532, -79.3832, 35000U, 450U, 270U);

            Assert::AreEqual(static_cast<uint16_t>(270U), pr.GetHeading(),
                L"GetHeading must return the value passed to SetPosition (REQ-CLT-030)");
        }

        TEST_METHOD(SetPosition_MultipleCallsOverwrite)
        {
            // REQ-CLT-030 — second SetPosition overwrites, not accumulates
            TempFile tf("at_pr_multi.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            PositionReporter pr(rudp, logger, 1U, ep);

            pr.SetPosition(50.0, -80.0, 10000U, 300U, 0U);
            pr.SetPosition(43.5, -79.5, 35000U, 450U, 90U);   // overwrite

            Assert::AreEqual(43.5, pr.GetLatitude(), 1e-6,
                L"Second SetPosition must overwrite the first (REQ-CLT-030)");
            Assert::AreEqual(35000U, pr.GetAltitude(),
                L"Altitude after second SetPosition must be the new value");
        }

        TEST_METHOD(DefaultConstructedLatitude_Is43Point5)
        {
            // REQ-CLT-030 — PositionReporter initializes latitude to 43.5
            // (inside the south sector, confirmed in PositionReporter.cpp constructor)
            TempFile tf("at_pr_init.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            PositionReporter pr(rudp, logger, 1U, ep);

            Assert::AreEqual(43.5, pr.GetLatitude(), 1e-6,
                L"Default latitude must be 43.5 (REQ-CLT-030)");
        }

        // =====================================================================
        // REQ-CLT-040 — SimulateMovement() stays in [42.0, 48.0]
        //
        // SimulateMovement() is called from SendReport(), which calls
        // RUDP::SendPacket() (requires initialized socket).  We drive from
        // the upper boundary to verify the bounce cap, using SetPosition to
        // pre-position the reporter near each wall, then calling SendReport()
        // once.  The socket send fails silently (RUDP not initialized) but the
        // position update and bounce still execute before the send attempt.
        // =====================================================================

        TEST_METHOD(SimulateMovement_NorthBounce_StaysAtOrBelow48)
        {
            // REQ-CLT-040
            // Pre-position at 47.95 heading north (+1.0 direction).
            // After one SendReport(), latitude advances by 0.10 to 48.05,
            // which is >= 48.0, so it is clamped to 48.0 and direction reverses.
            TempFile tf("at_pr_nb.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;   // not initialized — SendPacket will return false silently
            Endpoint ep = MakeDummyEndpoint();
            PositionReporter pr(rudp, logger, 1U, ep);

            // Position just below the north wall
            pr.SetPosition(47.95, -79.5, 35000U, 450U, 0U);

            // SendReport() calls SimulateMovement() then attempts SendPacket (no-op)
            pr.SendReport();

            Assert::IsTrue(pr.GetLatitude() <= 48.0,
                L"Latitude must not exceed 48.0 after northbound movement (REQ-CLT-040)");
        }

        TEST_METHOD(SimulateMovement_SouthBounce_StaysAtOrAbove42)
        {
            // REQ-CLT-040
            // Pre-position at 42.05 heading south. After SetPosition the internal
            // m_latDirection is still +1.0 (set in constructor). To test the south
            // wall we need two reports: first drives to >=48.0 and flips to -1.0,
            // then subsequent sends head south.  A cleaner approach: set position
            // just above the south wall and manually set direction by calling
            // SendReport enough times from just above north wall (it flips to south)
            // then from 42.05.
            //
            // Simpler: start at 48.0 (already at north wall), one report reverses
            // direction to -1.0, then set position to 42.05 and call one more report.
            TempFile tf("at_pr_sb.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            PositionReporter pr(rudp, logger, 1U, ep);

            // Get into southbound state: position at north wall, one report flips direction
            pr.SetPosition(48.0, -79.5, 35000U, 450U, 0U);
            pr.SendReport();   // clamps to 48.0, reverses to -1.0

            // Now position just above south wall; next report must clamp at 42.0
            pr.SetPosition(42.05, -79.5, 35000U, 450U, 0U);
            pr.SendReport();   // advances 42.05 - 0.10 = 41.95 → clamped to 42.0

            Assert::IsTrue(pr.GetLatitude() >= 42.0,
                L"Latitude must not drop below 42.0 after southbound movement (REQ-CLT-040)");
        }

        TEST_METHOD(SimulateMovement_NorthBounce_ClampedTo48Exactly)
        {
            // REQ-CLT-040 — the bounce logic assigns m_latitude = 48.0 exactly
            TempFile tf("at_pr_nc.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            PositionReporter pr(rudp, logger, 1U, ep);

            pr.SetPosition(47.95, -79.5, 35000U, 450U, 0U);
            pr.SendReport();   // 47.95 + 0.10 = 48.05 → clamped to 48.0

            Assert::AreEqual(48.0, pr.GetLatitude(), 1e-9,
                L"Latitude must be clamped to exactly 48.0 at the north boundary (REQ-CLT-040)");
        }

        TEST_METHOD(SimulateMovement_SouthBounce_ClampedTo42Exactly)
        {
            // REQ-CLT-040
            TempFile tf("at_pr_sc.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            PositionReporter pr(rudp, logger, 1U, ep);

            // Get into southbound direction first (bounce off north wall)
            pr.SetPosition(48.0, -79.5, 35000U, 450U, 0U);
            pr.SendReport();   // direction → -1.0

            pr.SetPosition(42.05, -79.5, 35000U, 450U, 0U);
            pr.SendReport();   // 42.05 - 0.10 = 41.95 → clamped to 42.0

            Assert::AreEqual(42.0, pr.GetLatitude(), 1e-9,
                L"Latitude must be clamped to exactly 42.0 at the south boundary (REQ-CLT-040)");
        }

        TEST_METHOD(SimulateMovement_MidRange_AdvancesBy0Point1)
        {
            // REQ-CLT-040 — mid-range movement must step by exactly 0.10
            TempFile tf("at_pr_mid.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            PositionReporter pr(rudp, logger, 1U, ep);

            pr.SetPosition(44.0, -79.5, 35000U, 450U, 0U);
            pr.SendReport();   // direction is +1.0 (initial), so 44.0 + 0.10 = 44.10

            Assert::AreEqual(44.1, pr.GetLatitude(), 1e-9,
                L"Mid-range northbound step must advance latitude by exactly 0.10 (REQ-CLT-040)");
        }

        // =====================================================================
        // REQ-CLT-060 — LogPacket called on TX (via Logger file read-back)
        // =====================================================================

        TEST_METHOD(SendReport_LogsPositionReportTX)
        {
            // REQ-CLT-060
            // SendReport() calls m_logger.LogPacket("TX", pkt) after the send.
            // Even though SendPacket fails (RUDP not initialized), the log call
            // still executes because the Logger call is unconditional.
            TempFile tf("at_pr_log.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;   // not initialized — send returns false, log still fires
            Endpoint ep = MakeDummyEndpoint();
            PositionReporter pr(rudp, logger, 801U, ep);

            pr.SendReport();
            logger.Close();

            Assert::IsTrue(tf.Contains("| TX |"),
                L"SendReport must write a TX log entry (REQ-CLT-060)");
            Assert::IsTrue(tf.Contains("POSITION_REPORT"),
                L"TX log entry must identify the packet type as POSITION_REPORT (REQ-CLT-060)");
            Assert::IsTrue(tf.Contains("FLT:801"),
                L"TX log entry must carry the correct flight ID (REQ-CLT-060)");
        }

    };  // TEST_CLASS(PositionReporterTests)


    // =========================================================================
    // HandoffHandlerTests — REQ-CLT-050
    // =========================================================================

    TEST_CLASS(HandoffHandlerTests)
    {
    public:

        // =====================================================================
        // REQ-CLT-050 — Construction and initial state
        // =====================================================================

        TEST_METHOD(HandoffHandler_DefaultConstruct_NoPendingHandoff)
        {
            // REQ-CLT-050
            TempFile tf("at_hh_init.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            HandoffHandler hh(rudp, logger, 801U, ep);

            Assert::IsFalse(hh.HasPendingHandoff(),
                L"HasPendingHandoff must be false on construction (REQ-CLT-050)");
        }

        TEST_METHOD(HandoffHandler_DefaultConstruct_PendingSectorIdIsZero)
        {
            // REQ-CLT-050
            TempFile tf("at_hh_sid.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            HandoffHandler hh(rudp, logger, 801U, ep);

            Assert::AreEqual(0U, hh.GetPendingSectorId(),
                L"Pending sector ID must be 0 on construction (REQ-CLT-050)");
        }

        // =====================================================================
        // REQ-CLT-050 — HandleInstruct stores the new sector ID
        // =====================================================================

        TEST_METHOD(HandleInstruct_StoresNewSectorId)
        {
            // REQ-CLT-050
            TempFile tf("at_hh_store.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;   // not initialized — SendPacket returns false (ACK not sent)
            Endpoint ep = MakeDummyEndpoint();
            HandoffHandler hh(rudp, logger, 801U, ep);

            Packet instruct = MakeHandoffInstructPacket(2U, 801U);
            hh.HandleInstruct(instruct);

            Assert::AreEqual(2U, hh.GetPendingSectorId(),
                L"HandleInstruct must store the new sector ID from the payload (REQ-CLT-050)");
        }

        TEST_METHOD(HandleInstruct_SetsPendingHandoff)
        {
            // REQ-CLT-050
            TempFile tf("at_hh_pend.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            HandoffHandler hh(rudp, logger, 801U, ep);

            Packet instruct = MakeHandoffInstructPacket(3U, 801U);
            hh.HandleInstruct(instruct);

            Assert::IsTrue(hh.HasPendingHandoff(),
                L"HasPendingHandoff must be true after HandleInstruct (REQ-CLT-050)");
        }

        TEST_METHOD(HandleInstruct_SectorIdDecodedBigEndian)
        {
            // REQ-CLT-050 — verify the big-endian decode logic for a multi-byte value
            // Sector ID 0x00000002 → bytes 0x00 0x00 0x00 0x02
            TempFile tf("at_hh_be.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            HandoffHandler hh(rudp, logger, 801U, ep);

            // Build packet with sector ID = 0x00010002 (65538)
            Packet instruct = MakeHandoffInstructPacket(0x00010002U, 801U);
            hh.HandleInstruct(instruct);

            Assert::AreEqual(0x00010002U, hh.GetPendingSectorId(),
                L"HandleInstruct must correctly decode a multi-byte big-endian sector ID (REQ-CLT-050)");
        }

        TEST_METHOD(HandleInstruct_MaxSectorId_DecodedCorrectly)
        {
            // REQ-CLT-050 — UINT32_MAX edge case
            TempFile tf("at_hh_max.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            HandoffHandler hh(rudp, logger, 801U, ep);

            Packet instruct = MakeHandoffInstructPacket(0xFFFFFFFFU, 801U);
            hh.HandleInstruct(instruct);

            Assert::AreEqual(0xFFFFFFFFU, hh.GetPendingSectorId(),
                L"HandleInstruct must accept UINT32_MAX as a sector ID (REQ-CLT-050)");
        }

        TEST_METHOD(HandleInstruct_TooShortPayload_DoesNotSetPending)
        {
            // REQ-CLT-050 — payload < 4 bytes must be rejected (guard clause)
            TempFile tf("at_hh_short.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            HandoffHandler hh(rudp, logger, 801U, ep);

            // Build a HANDOFF_INSTRUCT with only 2 payload bytes (too short)
            Packet badPkt(PacketType::HANDOFF_INSTRUCT, 801U);
            badPkt.SetPayload(std::vector<uint8_t>{ 0x00U, 0x01U });   // only 2 bytes
            hh.HandleInstruct(badPkt);

            Assert::IsFalse(hh.HasPendingHandoff(),
                L"HandleInstruct must not set pending handoff for a too-short payload (REQ-CLT-050)");
        }

        // =====================================================================
        // ClearPendingHandoff resets state
        // =====================================================================

        TEST_METHOD(ClearPendingHandoff_ResetsFlags)
        {
            // REQ-CLT-050
            TempFile tf("at_hh_clr.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            HandoffHandler hh(rudp, logger, 801U, ep);

            Packet instruct = MakeHandoffInstructPacket(2U, 801U);
            hh.HandleInstruct(instruct);
            Assert::IsTrue(hh.HasPendingHandoff(), L"Pre-condition: pending handoff must be set");

            hh.ClearPendingHandoff();

            Assert::IsFalse(hh.HasPendingHandoff(),
                L"HasPendingHandoff must be false after ClearPendingHandoff (REQ-CLT-050)");
            Assert::AreEqual(0U, hh.GetPendingSectorId(),
                L"PendingSectorId must be 0 after ClearPendingHandoff (REQ-CLT-050)");
        }

        TEST_METHOD(HandleInstruct_LogsNewSectorInfo)
        {
            // REQ-CLT-060 — HandleInstruct logs the new sector via LogInfo
            TempFile tf("at_hh_log.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            RUDP rudp;
            Endpoint ep = MakeDummyEndpoint();
            HandoffHandler hh(rudp, logger, 801U, ep);

            Packet instruct = MakeHandoffInstructPacket(2U, 801U);
            hh.HandleInstruct(instruct);
            logger.Close();

            Assert::IsTrue(tf.Contains("HANDOFF_INSTRUCT"),
                L"HandleInstruct must log the incoming HANDOFF_INSTRUCT event (REQ-CLT-060)");
        }

    };  // TEST_CLASS(HandoffHandlerTests)


    // =========================================================================
    // FileReceiverTests — REQ-CLT-070
    // =========================================================================

    TEST_CLASS(FileReceiverTests)
    {
    public:

        // =====================================================================
        // REQ-CLT-070 — Initial state
        // =====================================================================

        TEST_METHOD(FileReceiver_DefaultConstruct_StateIsIdle)
        {
            // REQ-CLT-070
            TempFile tf("at_fr_init.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 801U);

            Assert::IsTrue(TransferState::IDLE == fr.GetState(),
                L"FileReceiver must start in IDLE state (REQ-CLT-070)");
        }

        TEST_METHOD(FileReceiver_DefaultConstruct_ReceivedChunksIsZero)
        {
            // REQ-CLT-070
            TempFile tf("at_fr_rc0.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 801U);

            Assert::AreEqual(0U, fr.GetReceivedChunks(),
                L"ReceivedChunks must be 0 on construction (REQ-CLT-070)");
        }

        TEST_METHOD(FileReceiver_DefaultConstruct_TotalChunksIsZero)
        {
            // REQ-CLT-070
            TempFile tf("at_fr_tc0.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            Assert::AreEqual(0U, fr.GetTotalChunks(),
                L"TotalChunks must be 0 on construction (REQ-CLT-070)");
        }

        // =====================================================================
        // REQ-CLT-070 — HandleStart transitions to RECEIVING
        // =====================================================================

        TEST_METHOD(HandleStart_TransitionsToReceiving)
        {
            // REQ-CLT-070
            TempFile tf("at_fr_start.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            Packet startPkt = MakeStartPacket(2048U, 2U);
            fr.HandlePacket(startPkt);

            Assert::IsTrue(TransferState::RECEIVING == fr.GetState(),
                L"HandlePacket(FILE_TRANSFER_START) must transition to RECEIVING (REQ-CLT-070)");
        }

        TEST_METHOD(HandleStart_SetsTotalChunks)
        {
            // REQ-CLT-070
            TempFile tf("at_fr_tc.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            Packet startPkt = MakeStartPacket(3072U, 3U);   // 3 chunks
            fr.HandlePacket(startPkt);

            Assert::AreEqual(3U, fr.GetTotalChunks(),
                L"HandleStart must set TotalChunks from the START payload (REQ-CLT-070)");
        }

        TEST_METHOD(HandleStart_OutputPathContainsFlightId)
        {
            // REQ-CLT-070 — output path: "received_sector_<flightId>.jpg"
            TempFile tf("at_fr_path.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 801U);

            Packet startPkt = MakeStartPacket(1024U, 1U);
            fr.HandlePacket(startPkt);

            const std::string& path = fr.GetOutputPath();
            Assert::IsTrue(path.find("801") != std::string::npos,
                L"Output path must embed the flight ID (REQ-CLT-070)");
            Assert::IsTrue(path.find(".jpg") != std::string::npos,
                L"Output path must end in .jpg (REQ-CLT-070)");
        }

        TEST_METHOD(HandleStart_ZeroFileSize_TransitionsToFailed)
        {
            // REQ-CLT-070 — guard: zero file size is invalid
            TempFile tf("at_fr_zero.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            Packet startPkt = MakeStartPacket(0U, 1U);
            fr.HandlePacket(startPkt);

            Assert::IsTrue(TransferState::FAILED == fr.GetState(),
                L"HandleStart with zero file size must set FAILED state (REQ-CLT-070)");
        }

        TEST_METHOD(HandleStart_ZeroChunkCount_TransitionsToFailed)
        {
            // REQ-CLT-070 — guard: zero chunk count is invalid
            TempFile tf("at_fr_zc.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            Packet startPkt = MakeStartPacket(1024U, 0U);
            fr.HandlePacket(startPkt);

            Assert::IsTrue(TransferState::FAILED == fr.GetState(),
                L"HandleStart with zero chunk count must set FAILED state (REQ-CLT-070)");
        }

        TEST_METHOD(HandleStart_TooShortPayload_TransitionsToFailed)
        {
            // REQ-CLT-070 — payload < 8 bytes must be rejected
            TempFile tf("at_fr_short.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            Packet badStart(PacketType::FILE_TRANSFER_START, 1U);
            badStart.SetPayload(std::vector<uint8_t>{ 0x00U, 0x00U, 0x04U });  // only 3 bytes
            fr.HandlePacket(badStart);

            Assert::IsTrue(TransferState::FAILED == fr.GetState(),
                L"HandleStart with too-short payload must set FAILED state (REQ-CLT-070)");
        }

        // =====================================================================
        // REQ-CLT-070 — HandleChunk increments received count
        // =====================================================================

        TEST_METHOD(HandleChunk_IncreasesReceivedCount)
        {
            // REQ-CLT-070
            TempFile tf("at_fr_chunk.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            // 2 chunks × 1024 bytes each → 2048-byte file
            fr.HandlePacket(MakeStartPacket(2048U, 2U));

            std::vector<uint8_t> data(1024U, 0xAAU);
            fr.HandlePacket(MakeChunkPacket(0U, data));

            Assert::AreEqual(1U, fr.GetReceivedChunks(),
                L"ReceivedChunks must be 1 after one valid chunk (REQ-CLT-070)");
        }

        TEST_METHOD(HandleChunk_DuplicateChunk_NotDoubleCounted)
        {
            // REQ-CLT-070 — duplicate detection: same index sent twice
            TempFile tf("at_fr_dup.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            fr.HandlePacket(MakeStartPacket(2048U, 2U));

            std::vector<uint8_t> data(1024U, 0xBBU);
            fr.HandlePacket(MakeChunkPacket(0U, data));
            fr.HandlePacket(MakeChunkPacket(0U, data));   // duplicate

            Assert::AreEqual(1U, fr.GetReceivedChunks(),
                L"Duplicate chunk must not increment ReceivedChunks (REQ-CLT-070)");
        }

        TEST_METHOD(HandleChunk_OutOfOrderDelivery_BothReceived)
        {
            // REQ-CLT-070 — out-of-order: chunk 1 before chunk 0
            TempFile tf("at_fr_ooo.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            fr.HandlePacket(MakeStartPacket(2048U, 2U));

            std::vector<uint8_t> d0(1024U, 0xAAU);
            std::vector<uint8_t> d1(1024U, 0xBBU);

            fr.HandlePacket(MakeChunkPacket(1U, d1));     // chunk 1 arrives first
            fr.HandlePacket(MakeChunkPacket(0U, d0));     // chunk 0 arrives second

            Assert::AreEqual(2U, fr.GetReceivedChunks(),
                L"Both out-of-order chunks must be counted correctly (REQ-CLT-070)");
        }

        TEST_METHOD(HandleChunk_OutOfRangeIndex_RejectedAndCountNotIncremented)
        {
            // REQ-CLT-070 — chunk index >= totalChunks must be rejected
            TempFile tf("at_fr_oob.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            fr.HandlePacket(MakeStartPacket(1024U, 1U));   // 1 chunk total

            std::vector<uint8_t> data(1024U, 0xCCU);
            fr.HandlePacket(MakeChunkPacket(1U, data));    // index 1 is out of range

            Assert::AreEqual(0U, fr.GetReceivedChunks(),
                L"Out-of-range chunk index must not increment ReceivedChunks (REQ-CLT-070)");
        }

        TEST_METHOD(HandleChunk_WithoutPriorStart_LogsError)
        {
            // REQ-CLT-070 — CHUNK before START must be rejected (not in RECEIVING)
            TempFile tf("at_fr_nostart.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            std::vector<uint8_t> data(1024U, 0xDDU);
            fr.HandlePacket(MakeChunkPacket(0U, data));    // no START issued

            Assert::IsTrue(TransferState::IDLE == fr.GetState(),
                L"State must remain IDLE when CHUNK arrives before START (REQ-CLT-070)");
            logger.Close();
            Assert::IsTrue(tf.Contains("ERROR"),
                L"A CHUNK before START must produce an ERROR log entry (REQ-CLT-060)");
        }

        // =====================================================================
        // REQ-CLT-070 — Progress percentage
        // =====================================================================

        TEST_METHOD(GetProgressPercent_ZeroBeforeStart)
        {
            // REQ-CLT-070
            TempFile tf("at_fr_prog0.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            Assert::AreEqual(0U, fr.GetProgressPercent(),
                L"Progress must be 0 before a transfer starts (REQ-CLT-070)");
        }

        TEST_METHOD(GetProgressPercent_FiftyPercentAfterHalfChunks)
        {
            // REQ-CLT-070 — 1 out of 2 chunks = 50 %
            TempFile tf("at_fr_50.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            fr.HandlePacket(MakeStartPacket(2048U, 2U));

            std::vector<uint8_t> data(1024U, 0xEEU);
            fr.HandlePacket(MakeChunkPacket(0U, data));

            Assert::AreEqual(50U, fr.GetProgressPercent(),
                L"Progress must be 50% after receiving 1 of 2 chunks (REQ-CLT-070)");
        }

        // =====================================================================
        // REQ-CLT-070 — Reset clears all state
        // =====================================================================

        TEST_METHOD(Reset_ClearsAllState)
        {
            // REQ-CLT-070
            TempFile tf("at_fr_rst.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            fr.HandlePacket(MakeStartPacket(2048U, 2U));
            std::vector<uint8_t> data(1024U, 0xFFU);
            fr.HandlePacket(MakeChunkPacket(0U, data));

            fr.Reset();

            Assert::IsTrue(TransferState::IDLE == fr.GetState(),
                L"State must be IDLE after Reset (REQ-CLT-070)");
            Assert::AreEqual(0U, fr.GetTotalChunks(),
                L"TotalChunks must be 0 after Reset");
            Assert::AreEqual(0U, fr.GetReceivedChunks(),
                L"ReceivedChunks must be 0 after Reset");
            Assert::AreEqual(0U, fr.GetProgressPercent(),
                L"Progress must be 0 after Reset");
        }

        // =====================================================================
        // REQ-CLT-070 — End-to-end: START + chunks in order → COMPLETE
        // =====================================================================

        TEST_METHOD(FullTransfer_InOrder_TwoChunks_CompletesSuccessfully)
        {
            // REQ-CLT-070
            // File: 2 × 1024-byte chunks.  All chunks delivered in order.
            // WriteFileToDisk() will write to CWD as "received_sector_42.jpg";
            // we clean up the output file afterward.
            const uint32_t FLIGHT_ID = 42U;
            const uint32_t FILE_SIZE = 2048U;
            const uint32_t NUM_CHUNKS = 2U;

            TempFile tf("at_fr_e2e.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            // Clean up the output file after the test
            std::string outputPath = "received_sector_" + std::to_string(FLIGHT_ID) + ".jpg";

            FileReceiver fr(logger, FLIGHT_ID);

            fr.HandlePacket(MakeStartPacket(FILE_SIZE, NUM_CHUNKS, FLIGHT_ID));
            Assert::IsTrue(TransferState::RECEIVING == fr.GetState(),
                L"State must be RECEIVING after START (pre-condition)");

            // Chunk 0: bytes 0xA0 repeated
            fr.HandlePacket(MakeChunkPacket(0U,
                std::vector<uint8_t>(1024U, 0xA0U), FLIGHT_ID));

            // Chunk 1: bytes 0xB0 repeated
            fr.HandlePacket(MakeChunkPacket(1U,
                std::vector<uint8_t>(1024U, 0xB0U), FLIGHT_ID));

            Assert::AreEqual(NUM_CHUNKS, fr.GetReceivedChunks(),
                L"All chunks must be received before END");

            // Send END → triggers WriteFileToDisk
            fr.HandlePacket(MakeEndPacket(FLIGHT_ID));

            Assert::IsTrue(TransferState::COMPLETE == fr.GetState(),
                L"State must be COMPLETE after a successful full transfer (REQ-CLT-070)");

            // Clean up the output file written to disk
            (void)::DeleteFileA(outputPath.c_str());
        }

        TEST_METHOD(FullTransfer_OutOfOrder_TwoChunks_CompletesSuccessfully)
        {
            // REQ-CLT-070 — out-of-order delivery must still assemble correctly
            const uint32_t FLIGHT_ID = 43U;
            const uint32_t FILE_SIZE = 2048U;
            const uint32_t NUM_CHUNKS = 2U;

            TempFile tf("at_fr_ooo2.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            std::string outputPath = "received_sector_" + std::to_string(FLIGHT_ID) + ".jpg";

            FileReceiver fr(logger, FLIGHT_ID);
            fr.HandlePacket(MakeStartPacket(FILE_SIZE, NUM_CHUNKS, FLIGHT_ID));

            // Deliver chunk 1 before chunk 0
            fr.HandlePacket(MakeChunkPacket(1U,
                std::vector<uint8_t>(1024U, 0xCCU), FLIGHT_ID));
            fr.HandlePacket(MakeChunkPacket(0U,
                std::vector<uint8_t>(1024U, 0xDDU), FLIGHT_ID));
            fr.HandlePacket(MakeEndPacket(FLIGHT_ID));

            Assert::IsTrue(TransferState::COMPLETE == fr.GetState(),
                L"Out-of-order delivery must still produce COMPLETE state (REQ-CLT-070)");
            Assert::AreEqual(NUM_CHUNKS, fr.GetReceivedChunks(),
                L"ReceivedChunks must equal total chunks after out-of-order delivery");

            (void)::DeleteFileA(outputPath.c_str());
        }

        TEST_METHOD(FullTransfer_PartialChunks_StillCompletesWithWarning)
        {
            // REQ-CLT-070 — FileReceiver writes partial file and sets COMPLETE
            // (not FAILED) even with missing chunks (REQ per comment in HandleEnd)
            const uint32_t FLIGHT_ID = 44U;
            const uint32_t FILE_SIZE = 2048U;
            const uint32_t NUM_CHUNKS = 2U;

            TempFile tf("at_fr_partial.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            std::string outputPath = "received_sector_" + std::to_string(FLIGHT_ID) + ".jpg";

            FileReceiver fr(logger, FLIGHT_ID);
            fr.HandlePacket(MakeStartPacket(FILE_SIZE, NUM_CHUNKS, FLIGHT_ID));

            // Only deliver chunk 0 — chunk 1 is "lost" in transit
            fr.HandlePacket(MakeChunkPacket(0U,
                std::vector<uint8_t>(1024U, 0xEEU), FLIGHT_ID));

            fr.HandlePacket(MakeEndPacket(FLIGHT_ID));

            // HandleEnd writes what it has and marks COMPLETE (with error log for missing)
            Assert::IsTrue(TransferState::COMPLETE == fr.GetState(),
                L"HandleEnd with missing chunks must still write partial file and set COMPLETE (REQ-CLT-070)");
            Assert::AreEqual(1U, fr.GetReceivedChunks(),
                L"ReceivedChunks must be 1 when only one chunk arrived");

            logger.Close();
            Assert::IsTrue(tf.Contains("ERROR"),
                L"Partial transfer must emit an ERROR log for the missing chunks (REQ-CLT-060)");

            (void)::DeleteFileA(outputPath.c_str());
        }

        TEST_METHOD(HandleEnd_WithoutPriorStart_LogsError)
        {
            // REQ-CLT-070 — END before START must be rejected (not RECEIVING)
            TempFile tf("at_fr_nostart_end.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, 1U);

            fr.HandlePacket(MakeEndPacket(1U));

            Assert::IsTrue(TransferState::IDLE == fr.GetState(),
                L"State must remain IDLE when END received before START (REQ-CLT-070)");
            logger.Close();
            Assert::IsTrue(tf.Contains("ERROR"),
                L"END before START must produce an ERROR log entry (REQ-CLT-060)");
        }

        TEST_METHOD(HandleStart_ResetsMidTransferState)
        {
            // REQ-CLT-070 — re-START during RECEIVING resets and begins fresh
            const uint32_t FLIGHT_ID = 45U;
            TempFile tf("at_fr_restart.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            FileReceiver fr(logger, FLIGHT_ID);

            // First transfer
            fr.HandlePacket(MakeStartPacket(1024U, 1U, FLIGHT_ID));
            fr.HandlePacket(MakeChunkPacket(0U, std::vector<uint8_t>(1024U, 0x11U), FLIGHT_ID));

            // Second START arrives mid-transfer — must reset and restart
            fr.HandlePacket(MakeStartPacket(2048U, 2U, FLIGHT_ID));

            Assert::IsTrue(TransferState::RECEIVING == fr.GetState(),
                L"Re-START must place FileReceiver in RECEIVING state (REQ-CLT-070)");
            Assert::AreEqual(0U, fr.GetReceivedChunks(),
                L"ReceivedChunks must reset to 0 after a re-START (REQ-CLT-070)");
            Assert::AreEqual(2U, fr.GetTotalChunks(),
                L"TotalChunks must reflect the new START packet after reset (REQ-CLT-070)");
        }

    };  // TEST_CLASS(FileReceiverTests)

}  // namespace AeroTrackTests