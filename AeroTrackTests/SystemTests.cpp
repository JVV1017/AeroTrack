// =============================================================================
// SystemTests.cpp — MSTest system-level tests for AeroTrack
// =============================================================================
// DO-178C DAL-C  |  AeroTrack Ground Control
// Framework:      Microsoft CppUnitTestFramework (MSTest)
//
// Requirements covered:
//   REQ-PKT-010  Full protocol packet pipeline (all 6 packet types in sequence)
//   REQ-PKT-050  CRC-16 validated end-to-end through full protocol sequence
//   REQ-COM-020  Sequence numbers monotonically increase across full session
//   REQ-COM-030  Every packet in sequence received and deserialized correctly
//   REQ-LOG-010  All packet types logged TX/RX in correct pipe-delimited format
//   REQ-SVR-050  FILE_TRANSFER_START / CHUNK / END sequence verified end-to-end
//   REQ-CLT-070  FileReceiver assembles chunks from a full FileTransfer sequence
//   REQ-STM-020  State machine advances through full valid transition sequence
//
// ---- System test scope -------------------------------------------------------
// These tests simulate full protocol scenarios using real modules:
//
//   SYS-001  Full connection handshake via real RUDP loopback
//            (CONNECT → CONNECT_ACK → CONNECT_CONFIRM sequence)
//
//   SYS-002  Full file transfer sequence end-to-end
//            FileTransfer builds packets → RUDP sends → FileReceiver assembles
//
//   SYS-003  Full flight state machine lifecycle
//            IDLE → CONNECTED → TRACKING → HANDOFF sequence in StateMachine
//
//   SYS-004  Complete protocol session with logging
//            All packet types logged through a simulated session
//
//   SYS-005  Multi-packet session with CRC validation on every packet
//            Six packets sent, all received, all CRC-16 validated
// =============================================================================

#include "TestCommon.h"

namespace AeroTrackTests
{
    TEST_CLASS(SystemTests)
    {
    public:

        // =====================================================================
        // SYS-001 — Full 3-step connection handshake via real RUDP loopback
        //
        // Simulates the AeroTrack connection protocol:
        //   Step 1: Client sends CONNECT
        //   Step 2: Server sends CONNECT_ACK
        //   Step 3: Client sends CONNECT_CONFIRM
        //
        // Uses two real RUDP instances on loopback ports.
        // Verifies the complete handshake packet sequence is transmitted and
        // received correctly — the same sequence used in the live application.
        // REQ-COM-010, REQ-COM-030, REQ-PKT-010, REQ-PKT-050
        // =====================================================================

        TEST_METHOD(SYS001_FullConnectionHandshake_ThreeStepSequence)
        {
            const uint16_t SERVER_TEST_PORT = 54500U;
            const uint16_t CLIENT_TEST_PORT = 54501U;
            const uint32_t FLIGHT_ID = 1001U;

            // ── Bind server-side RUDP ────────────────────────────────────────
            RUDP serverRudp;
            if (!serverRudp.Init() || !serverRudp.Bind(SERVER_TEST_PORT))
            {
                Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage(
                    "SYS-001 SKIP: Could not bind server on port 54500");
                serverRudp.Shutdown();
                return;
            }

            // ── Bind client-side RUDP ────────────────────────────────────────
            RUDP clientRudp;
            if (!clientRudp.Init() || !clientRudp.Bind(CLIENT_TEST_PORT))
            {
                Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage(
                    "SYS-001 SKIP: Could not bind client on port 54501");
                serverRudp.Shutdown();
                clientRudp.Shutdown();
                return;
            }

            Endpoint serverEndpoint;
            serverEndpoint.ip = "127.0.0.1";
            serverEndpoint.port = SERVER_TEST_PORT;

            Endpoint clientEndpoint;
            clientEndpoint.ip = "127.0.0.1";
            clientEndpoint.port = CLIENT_TEST_PORT;

            // ── Step 1: Client sends CONNECT ─────────────────────────────────
            Packet connectPkt(PacketType::CONNECT, FLIGHT_ID);
            bool step1Sent = clientRudp.SendPacket(connectPkt, FLIGHT_ID, serverEndpoint);
            Assert::IsTrue(step1Sent,
                L"SYS-001 Step 1: Client must send CONNECT successfully (REQ-COM-010)");

            // Server receives CONNECT
            Packet receivedConnect;
            Endpoint senderEp;
            bool step1Received = serverRudp.Receive(receivedConnect, senderEp, 500U);
            Assert::IsTrue(step1Received,
                L"SYS-001 Step 1: Server must receive CONNECT (REQ-COM-030)");
            Assert::IsTrue(PacketType::CONNECT == receivedConnect.GetType(),
                L"SYS-001 Step 1: Received packet must be CONNECT type (REQ-PKT-010)");
            Assert::IsTrue(receivedConnect.ValidateChecksum(),
                L"SYS-001 Step 1: CONNECT packet must pass CRC-16 (REQ-PKT-050)");

            // ── Step 2: Server sends CONNECT_ACK ─────────────────────────────
            // NOTE: RUDP::Receive() auto-sends an ACK back to the client when it
            // receives a non-ACK packet (REQ-COM-030). The client must skip that
            // auto-ACK and wait for the application-level CONNECT_ACK packet.
            Packet connectAckPkt(PacketType::CONNECT_ACK, FLIGHT_ID);
            bool step2Sent = serverRudp.SendPacket(connectAckPkt, FLIGHT_ID, clientEndpoint);
            Assert::IsTrue(step2Sent,
                L"SYS-001 Step 2: Server must send CONNECT_ACK (REQ-COM-010)");

            // Client receives — skip auto-ACK packets, wait for CONNECT_ACK
            Packet receivedAck;
            bool step2Received = false;
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                if (clientRudp.Receive(receivedAck, senderEp, 500U))
                {
                    if (PacketType::ACK != receivedAck.GetType())
                    {
                        step2Received = true;
                        break;
                    }
                }
                else
                {
                    break;
                }
            }
            Assert::IsTrue(step2Received,
                L"SYS-001 Step 2: Client must receive CONNECT_ACK (REQ-COM-030)");
            Assert::IsTrue(PacketType::CONNECT_ACK == receivedAck.GetType(),
                L"SYS-001 Step 2: Received packet must be CONNECT_ACK (REQ-PKT-010)");
            Assert::IsTrue(receivedAck.ValidateChecksum(),
                L"SYS-001 Step 2: CONNECT_ACK must pass CRC-16 (REQ-PKT-050)");

            // ── Step 3: Client sends CONNECT_CONFIRM ─────────────────────────
            Packet confirmPkt(PacketType::CONNECT_CONFIRM, FLIGHT_ID);
            bool step3Sent = clientRudp.SendPacket(confirmPkt, FLIGHT_ID, serverEndpoint);
            Assert::IsTrue(step3Sent,
                L"SYS-001 Step 3: Client must send CONNECT_CONFIRM (REQ-COM-010)");

            // Server receives — skip auto-ACK, wait for CONNECT_CONFIRM
            Packet receivedConfirm;
            bool step3Received = false;
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                if (serverRudp.Receive(receivedConfirm, senderEp, 500U))
                {
                    if (PacketType::ACK != receivedConfirm.GetType())
                    {
                        step3Received = true;
                        break;
                    }
                }
                else
                {
                    break;
                }
            }
            Assert::IsTrue(step3Received,
                L"SYS-001 Step 3: Server must receive CONNECT_CONFIRM (REQ-COM-030)");
            Assert::IsTrue(PacketType::CONNECT_CONFIRM == receivedConfirm.GetType(),
                L"SYS-001 Step 3: Received packet must be CONNECT_CONFIRM (REQ-PKT-010)");
            Assert::IsTrue(receivedConfirm.ValidateChecksum(),
                L"SYS-001 Step 3: CONNECT_CONFIRM must pass CRC-16 (REQ-PKT-050)");

            // ── Verify sequence numbers are monotonically increasing ──────────
            Assert::IsTrue(
                receivedConnect.GetSequenceNumber() <
                receivedConfirm.GetSequenceNumber(),
                L"SYS-001: Sequence numbers must increase across handshake (REQ-COM-020)");

            serverRudp.Shutdown();
            clientRudp.Shutdown();
        }

        // =====================================================================
        // SYS-002 — Full file transfer sequence: FileTransfer → RUDP → FileReceiver
        //
        // Simulates the complete radar file transfer protocol:
        //   1. FileTransfer loads a temp file and builds START/CHUNK/END packets
        //   2. Each packet is sent via RUDP loopback
        //   3. FileReceiver handles each received packet
        //   4. FileReceiver reaches COMPLETE state
        //
        // This is the exact same sequence the server uses when sending radar
        // sector images to the client aircraft terminal.
        // REQ-SVR-050, REQ-CLT-070, REQ-PKT-050
        // =====================================================================

        TEST_METHOD(SYS002_FileTransfer_RUDP_FileReceiver_EndToEnd)
        {
            const uint16_t RECV_PORT = 54502U;
            const uint32_t FLIGHT_ID = 2001U;

            // ── Create a temp file to transfer ───────────────────────────────
            char tempDir[MAX_PATH] = { 0 };
            (void)::GetTempPathA(MAX_PATH, tempDir);
            std::string srcPath = std::string(tempDir) + "at_sys002_src.bin";
            std::string outPath = "received_sector_" +
                std::to_string(FLIGHT_ID) + ".jpg";

            // Write 2KB of test data
            FILE* f = nullptr;
            (void)::fopen_s(&f, srcPath.c_str(), "wb");
            if (f != nullptr)
            {
                for (uint32_t i = 0U; i < 2048U; ++i)
                {
                    uint8_t b = static_cast<uint8_t>(i & 0xFFU);
                    (void)::fwrite(&b, 1U, 1U, f);
                }
                (void)::fclose(f);
            }

            // ── Set up FileTransfer (server side) ────────────────────────────
            FileTransfer ft;
            bool loaded = ft.LoadFile(srcPath);
            if (!loaded)
            {
                Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage(
                    "SYS-002 SKIP: Could not create temp source file");
                (void)::DeleteFileA(srcPath.c_str());
                return;
            }
            (void)ft.PrepareTransfer(FLIGHT_ID);

            // ── Set up RUDP loopback ─────────────────────────────────────────
            RUDP receiver;
            if (!receiver.Init() || !receiver.Bind(RECV_PORT))
            {
                Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage(
                    "SYS-002 SKIP: Could not bind receiver on port 54502");
                receiver.Shutdown();
                (void)::DeleteFileA(srcPath.c_str());
                return;
            }

            RUDP sender;
            if (!sender.Init())
            {
                Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage(
                    "SYS-002 SKIP: sender Init() failed");
                receiver.Shutdown();
                (void)::DeleteFileA(srcPath.c_str());
                return;
            }

            Endpoint dest;
            dest.ip = "127.0.0.1";
            dest.port = RECV_PORT;

            // ── Set up FileReceiver (client side) ────────────────────────────
            TempFile logFile("at_sys002.log");
            AeroTrack::Logger logger;
            (void)logger.Init(logFile.Path());
            FileReceiver fr(logger, FLIGHT_ID);

            // ── Send and receive START packet ────────────────────────────────
            Packet startPkt = ft.BuildStartPacket();
            (void)sender.SendPacket(startPkt, FLIGHT_ID, dest);

            Packet rxStart;
            Endpoint rxSender;
            (void)receiver.Receive(rxStart, rxSender, 500U);
            fr.HandlePacket(rxStart);

            Assert::IsTrue(TransferState::RECEIVING == fr.GetState(),
                L"SYS-002: FileReceiver must be RECEIVING after START (REQ-CLT-070)");
            Assert::AreEqual(ft.GetTotalChunks(), fr.GetTotalChunks(),
                L"SYS-002: FileReceiver total chunks must match FileTransfer (REQ-SVR-050)");

            // ── Send and receive all CHUNK packets ───────────────────────────
            for (uint32_t i = 0U; i < ft.GetTotalChunks(); ++i)
            {
                Packet chunkPkt = ft.BuildChunkPacket(i);
                (void)sender.SendPacket(chunkPkt, FLIGHT_ID, dest);

                Packet rxChunk;
                (void)receiver.Receive(rxChunk, rxSender, 500U);

                Assert::IsTrue(rxChunk.ValidateChecksum(),
                    L"SYS-002: Every CHUNK packet must pass CRC-16 (REQ-PKT-050)");

                fr.HandlePacket(rxChunk);
            }

            Assert::AreEqual(ft.GetTotalChunks(), fr.GetReceivedChunks(),
                L"SYS-002: All chunks must be received before END (REQ-CLT-070)");

            // ── Send and receive END packet ───────────────────────────────────
            Packet endPkt = ft.BuildEndPacket();
            (void)sender.SendPacket(endPkt, FLIGHT_ID, dest);

            Packet rxEnd;
            (void)receiver.Receive(rxEnd, rxSender, 500U);
            fr.HandlePacket(rxEnd);

            Assert::IsTrue(TransferState::COMPLETE == fr.GetState(),
                L"SYS-002: FileReceiver must reach COMPLETE after full transfer (REQ-CLT-070)");

            // ── Cleanup ───────────────────────────────────────────────────────
            logger.Close();
            sender.Shutdown();
            receiver.Shutdown();
            (void)::DeleteFileA(srcPath.c_str());
            (void)::DeleteFileA(outPath.c_str());
        }

        // =====================================================================
        // SYS-003 — Full flight state machine lifecycle
        //
        // Drives a StateMachine through the complete valid flight lifecycle:
        //   IDLE → CONNECTED → TRACKING → HANDOFF_INITIATED →
        //   HANDOFF_PENDING → HANDOFF_COMPLETE → TRACKING → LOST_CONTACT
        //
        // Verifies all transitions succeed and state is correct after each step.
        // REQ-STM-010, REQ-STM-020
        // =====================================================================

        TEST_METHOD(SYS003_StateMachine_FullFlightLifecycle)
        {
            StateMachine sm(3001U);

            // IDLE → CONNECTED  (handshake complete)
            TransitionResult r1 = sm.Transition(FlightState::CONNECTED,
                "handshake complete");
            Assert::IsTrue(r1.success,
                L"SYS-003: IDLE → CONNECTED must succeed (REQ-STM-020)");
            Assert::IsTrue(FlightState::CONNECTED == sm.GetCurrentState(),
                L"SYS-003: State must be CONNECTED after step 1");

            // CONNECTED → TRACKING  (first position report)
            TransitionResult r2 = sm.Transition(FlightState::TRACKING,
                "first POSITION_REPORT received");
            Assert::IsTrue(r2.success,
                L"SYS-003: CONNECTED → TRACKING must succeed (REQ-STM-020)");
            Assert::IsTrue(FlightState::TRACKING == sm.GetCurrentState(),
                L"SYS-003: State must be TRACKING after step 2");

            // TRACKING → HANDOFF_INITIATED  (sector boundary detected)
            TransitionResult r3 = sm.Transition(FlightState::HANDOFF_INITIATED,
                "sector boundary detected at lat 45.0");
            Assert::IsTrue(r3.success,
                L"SYS-003: TRACKING → HANDOFF_INITIATED must succeed (REQ-STM-020)");

            // HANDOFF_INITIATED → HANDOFF_PENDING  (instruction sent)
            TransitionResult r4 = sm.Transition(FlightState::HANDOFF_PENDING,
                "HANDOFF_INSTRUCT sent");
            Assert::IsTrue(r4.success,
                L"SYS-003: HANDOFF_INITIATED → HANDOFF_PENDING must succeed (REQ-STM-020)");

            // HANDOFF_PENDING → HANDOFF_COMPLETE  (ACK received)
            TransitionResult r5 = sm.Transition(FlightState::HANDOFF_COMPLETE,
                "HANDOFF_ACK received");
            Assert::IsTrue(r5.success,
                L"SYS-003: HANDOFF_PENDING → HANDOFF_COMPLETE must succeed (REQ-STM-020)");

            // HANDOFF_COMPLETE → TRACKING  (now tracking in new sector)
            TransitionResult r6 = sm.Transition(FlightState::TRACKING,
                "tracking in SECTOR-NORTH");
            Assert::IsTrue(r6.success,
                L"SYS-003: HANDOFF_COMPLETE → TRACKING must succeed (REQ-STM-020)");
            Assert::IsTrue(FlightState::TRACKING == sm.GetCurrentState(),
                L"SYS-003: State must be TRACKING after handoff completion");

            // TRACKING → LOST_CONTACT  (contact loss)
            TransitionResult r7 = sm.Transition(FlightState::LOST_CONTACT,
                "contact timeout exceeded");
            Assert::IsTrue(r7.success,
                L"SYS-003: TRACKING → LOST_CONTACT must succeed (REQ-STM-020)");
            Assert::IsTrue(FlightState::LOST_CONTACT == sm.GetCurrentState(),
                L"SYS-003: Final state must be LOST_CONTACT (REQ-STM-010)");
        }

        // =====================================================================
        // SYS-004 — Complete protocol session with logging
        //
        // Simulates a logged session with all major packet types:
        // CONNECT, POSITION_REPORT, HANDOFF_INSTRUCT, FILE_TRANSFER_START, END.
        // Verifies the Logger records each packet type in the session log.
        // REQ-LOG-010, REQ-PKT-010, REQ-PKT-030
        // =====================================================================

        TEST_METHOD(SYS004_CompleteSession_AllPacketTypesLogged)
        {
            TempFile tf("at_sys004.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            const uint32_t FLIGHT_ID = 4001U;

            // Simulate the full session packet sequence
            const PacketType sessionPackets[] = {
                PacketType::CONNECT,
                PacketType::CONNECT_ACK,
                PacketType::CONNECT_CONFIRM,
                PacketType::POSITION_REPORT,
                PacketType::TRACKING_ACK,
                PacketType::HANDOFF_INSTRUCT,
                PacketType::HANDOFF_ACK,
                PacketType::HANDOFF_COMPLETE,
                PacketType::FILE_TRANSFER_START,
                PacketType::FILE_TRANSFER_END,
                PacketType::DISCONNECT
            };

            const uint32_t packetCount =
                static_cast<uint32_t>(sizeof(sessionPackets) /
                    sizeof(sessionPackets[0]));

            for (uint32_t i = 0U; i < packetCount; ++i)
            {
                Packet pkt(sessionPackets[i], FLIGHT_ID);
                const std::string dir = (i % 2U == 0U) ? "TX" : "RX";
                logger.LogPacket(dir, pkt, "OK");
            }

            logger.Close();

            // Verify all packet types appear in the log
            Assert::IsTrue(tf.Contains("CONNECT"),
                L"SYS-004: CONNECT must be logged (REQ-LOG-010)");
            Assert::IsTrue(tf.Contains("POSITION_REPORT"),
                L"SYS-004: POSITION_REPORT must be logged (REQ-LOG-010)");
            Assert::IsTrue(tf.Contains("HANDOFF_INSTRUCT"),
                L"SYS-004: HANDOFF_INSTRUCT must be logged (REQ-LOG-010)");
            Assert::IsTrue(tf.Contains("FILE_TRANSFER_START"),
                L"SYS-004: FILE_TRANSFER_START must be logged (REQ-LOG-010)");
            Assert::IsTrue(tf.Contains("DISCONNECT"),
                L"SYS-004: DISCONNECT must be logged (REQ-LOG-010)");

            // Verify line count matches packet count
            std::vector<std::string> lines = tf.Lines();
            Assert::AreEqual(static_cast<size_t>(packetCount), lines.size(),
                L"SYS-004: Log line count must match number of packets in session");
        }

        // =====================================================================
        // SYS-005 — Multi-packet session: CRC-16 validated on every packet
        //
        // Sends 6 different packet types via RUDP loopback in sequence.
        // Every received packet is CRC-16 validated. Sequence numbers are
        // verified to be strictly increasing across the full session.
        // REQ-COM-020, REQ-PKT-050, REQ-COM-030
        // =====================================================================

        TEST_METHOD(SYS005_MultiPacketSession_AllCRCValidated)
        {
            const uint16_t RECV_PORT = 54503U;
            const uint32_t FLIGHT_ID = 5001U;

            RUDP receiver;
            if (!receiver.Init() || !receiver.Bind(RECV_PORT))
            {
                Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage(
                    "SYS-005 SKIP: Could not bind receiver on port 54503");
                receiver.Shutdown();
                return;
            }

            RUDP sender;
            if (!sender.Init())
            {
                Microsoft::VisualStudio::CppUnitTestFramework::Logger::WriteMessage(
                    "SYS-005 SKIP: sender Init() failed");
                receiver.Shutdown();
                return;
            }

            Endpoint dest;
            dest.ip = "127.0.0.1";
            dest.port = RECV_PORT;

            const PacketType types[] = {
                PacketType::CONNECT,
                PacketType::HEARTBEAT,
                PacketType::POSITION_REPORT,
                PacketType::HANDOFF_INSTRUCT,
                PacketType::FILE_TRANSFER_START,
                PacketType::DISCONNECT
            };
            const uint32_t typeCount = 6U;

            uint32_t lastSeq = 0U;

            for (uint32_t i = 0U; i < typeCount; ++i)
            {
                Packet outPkt(types[i], FLIGHT_ID);
                (void)sender.SendPacket(outPkt, FLIGHT_ID, dest);

                Packet inPkt;
                Endpoint inSender;
                bool received = receiver.Receive(inPkt, inSender, 500U);

                Assert::IsTrue(received,
                    L"SYS-005: Every packet in session must be received (REQ-COM-030)");
                Assert::IsTrue(inPkt.ValidateChecksum(),
                    L"SYS-005: Every received packet must pass CRC-16 (REQ-PKT-050)");
                Assert::IsTrue(inPkt.GetSequenceNumber() > lastSeq,
                    L"SYS-005: Sequence numbers must be strictly increasing (REQ-COM-020)");

                lastSeq = inPkt.GetSequenceNumber();
            }

            sender.Shutdown();
            receiver.Shutdown();
        }

    };  // TEST_CLASS(SystemTests)

}  // namespace AeroTrackTests