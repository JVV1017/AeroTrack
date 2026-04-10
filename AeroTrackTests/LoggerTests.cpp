// =============================================================================
// LoggerTests.cpp — MSTest unit tests for Logger
// =============================================================================
// DO-178C DAL-C  |  AeroTrack Ground Control
// Framework:      Microsoft CppUnitTestFramework (MSTest)
//
// Requirements covered:
//   REQ-LOG-010  LogPacket() writes a pipe-delimited TX/RX line
//   REQ-LOG-020  CurrentTimestamp() produces ISO-8601 format (YYYY-MM-DD HH:MM:SS.mmm)
//   REQ-LOG-030  Init() opens/creates the log file; caller-supplied filename
//   REQ-LOG-040  Init() appends — does not truncate existing content
//   REQ-LOG-050  Every write is flushed immediately (verified via read-back)
//   REQ-LOG-060  LogStateChange() writes FROM/TO/TRIGGER in pipe-delimited format
//
// File strategy:
//   Each test uses a unique temp-file name rooted in %TEMP% so tests never
//   collide.  A TempFile RAII helper deletes the file on scope exit, giving
//   the same guarantee as a teardown method.
//
// Read-back strategy:
//   After each Logger::Close() the test opens the file with std::ifstream and
//   searches for expected substrings.  This verifies both that bytes were
//   written AND that they were flushed (REQ-LOG-050) — a file that was only
//   buffered in memory would have 0 bytes on disk.
// =============================================================================

#include "TestCommon.h"

namespace AeroTrackTests
{
    TEST_CLASS(LoggerTests)
    {
    public:

        // =====================================================================
        // REQ-LOG-030 — Init() creates / opens the log file
        // =====================================================================

        TEST_METHOD(Init_ValidPath_ReturnsTrueAndFileIsOpen)
        {
            // REQ-LOG-030
            TempFile tf("at_log_init.log");
            AeroTrack::Logger logger;
            bool ok = logger.Init(tf.Path());

            Assert::IsTrue(ok,
                L"Init must return true for a writable path (REQ-LOG-030)");
            Assert::IsTrue(logger.IsOpen(),
                L"IsOpen must return true after successful Init (REQ-LOG-030)");

            logger.Close();
        }

        TEST_METHOD(Init_CreatesFileOnDisk)
        {
            // REQ-LOG-030 — the file must exist on disk after Init, even before
            // any log entries are written (std::ios::app creates the file).
            TempFile tf("at_log_create.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            logger.Close();

            // Try to open the file independently to confirm it exists
            std::ifstream check(tf.Path());
            Assert::IsTrue(check.is_open(),
                L"Init must create the log file on disk (REQ-LOG-030)");
        }

        TEST_METHOD(Init_InvalidPath_ReturnsFalse)
        {
            // REQ-LOG-030 — a path in a non-existent directory must fail
            AeroTrack::Logger logger;
            bool ok = logger.Init("Z:\\nonexistent_dir\\at_log_bad.log");

            Assert::IsFalse(ok,
                L"Init must return false for an invalid/inaccessible file path (REQ-LOG-030)");
            Assert::IsFalse(logger.IsOpen(),
                L"IsOpen must return false when Init failed (REQ-LOG-030)");
        }

        TEST_METHOD(IsOpen_BeforeInit_ReturnsFalse)
        {
            // REQ-LOG-030
            AeroTrack::Logger logger;
            Assert::IsFalse(logger.IsOpen(),
                L"IsOpen must return false before Init is called (REQ-LOG-030)");
        }

        TEST_METHOD(IsOpen_AfterClose_ReturnsFalse)
        {
            // REQ-LOG-030
            TempFile tf("at_log_close.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            logger.Close();

            Assert::IsFalse(logger.IsOpen(),
                L"IsOpen must return false after Close (REQ-LOG-030)");
        }

        // =====================================================================
        // REQ-LOG-040 — Init() appends; existing content is NOT truncated
        // =====================================================================

        TEST_METHOD(Init_AppendMode_PreservesExistingContent)
        {
            // REQ-LOG-040
            TempFile tf("at_log_append.log");

            // First logger writes a sentinel line
            {
                AeroTrack::Logger logger1;
                (void)logger1.Init(tf.Path());
                logger1.LogInfo("SENTINEL_LINE_FIRST_OPEN");
                logger1.Close();
            }

            // Second logger opens the same file — must NOT overwrite
            {
                AeroTrack::Logger logger2;
                (void)logger2.Init(tf.Path());
                logger2.LogInfo("SENTINEL_LINE_SECOND_OPEN");
                logger2.Close();
            }

            // Both sentinels must be present
            std::string content = tf.ReadAll();
            Assert::IsTrue(content.find("SENTINEL_LINE_FIRST_OPEN") != std::string::npos,
                L"Init must preserve existing file content (append mode) (REQ-LOG-040)");
            Assert::IsTrue(content.find("SENTINEL_LINE_SECOND_OPEN") != std::string::npos,
                L"Second Init must append new entries after existing content (REQ-LOG-040)");
        }

        TEST_METHOD(Init_AppendMode_SecondOpenDoesNotDuplicateFirstLine)
        {
            // REQ-LOG-040 — the file must have exactly 2 non-empty lines after
            // two separate Init/log/Close cycles.
            TempFile tf("at_log_append2.log");

            {
                AeroTrack::Logger l1;
                (void)l1.Init(tf.Path());
                l1.LogInfo("LINE_A");
                l1.Close();
            }
            {
                AeroTrack::Logger l2;
                (void)l2.Init(tf.Path());
                l2.LogInfo("LINE_B");
                l2.Close();
            }

            std::vector<std::string> lines = tf.Lines();
            Assert::AreEqual(static_cast<size_t>(2U), lines.size(),
                L"Two separate init/log/close cycles must produce exactly 2 lines (REQ-LOG-040)");
        }

        // =====================================================================
        // REQ-LOG-020 — LogInfo() writes message with INFO prefix
        // =====================================================================

        TEST_METHOD(LogInfo_WritesInfoPrefixAndMessage)
        {
            // REQ-LOG-020
            // Line format: <timestamp> | INFO | <message>
            TempFile tf("at_log_info.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            logger.LogInfo("System startup complete");
            logger.Close();

            Assert::IsTrue(tf.Contains("| INFO |"),
                L"LogInfo must write '| INFO |' to the log file (REQ-LOG-020)");
            Assert::IsTrue(tf.Contains("System startup complete"),
                L"LogInfo must write the exact message to the log file (REQ-LOG-020)");
        }

        TEST_METHOD(LogInfo_LineContainsTimestampSeparator)
        {
            // REQ-LOG-020 — timestamp is always the first field; separated by ' | '
            TempFile tf("at_log_info_ts.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            logger.LogInfo("timestamp check");
            logger.Close();

            // The timestamp separator appears between every field
            std::string content = tf.ReadAll();
            const size_t pipeCount = std::count(content.begin(), content.end(), '|');
            // "timestamp | INFO | message" → 2 pipe characters per line
            Assert::IsTrue(pipeCount >= 2U,
                L"Each LogInfo line must contain at least 2 pipe separators (REQ-LOG-020)");
        }

        // =====================================================================
        // REQ-LOG-030 — LogError() writes message with ERROR prefix
        // =====================================================================

        TEST_METHOD(LogError_WritesErrorPrefixAndMessage)
        {
            // REQ-LOG-030
            // Line format: <timestamp> | ERROR | <message>
            TempFile tf("at_log_error.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            logger.LogError("Socket failed to bind");
            logger.Close();

            Assert::IsTrue(tf.Contains("| ERROR |"),
                L"LogError must write '| ERROR |' to the log file (REQ-LOG-030)");
            Assert::IsTrue(tf.Contains("Socket failed to bind"),
                L"LogError must write the exact error message (REQ-LOG-030)");
        }

        TEST_METHOD(LogError_IsDistinctFromLogInfo)
        {
            // REQ-LOG-030 — ERROR prefix must differ from INFO prefix
            TempFile tf("at_log_err_vs_info.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            logger.LogInfo("info message");
            logger.LogError("error message");
            logger.Close();

            std::string content = tf.ReadAll();
            Assert::IsTrue(content.find("| INFO |") != std::string::npos,
                L"LogInfo line must contain '| INFO |'");
            Assert::IsTrue(content.find("| ERROR |") != std::string::npos,
                L"LogError line must contain '| ERROR |'");
        }

        // =====================================================================
        // REQ-LOG-050 — Flush after every write (read-back with file still open)
        // =====================================================================

        TEST_METHOD(LogInfo_FlushedImmediately_ReadableWithoutClose)
        {
            // REQ-LOG-050
            // If flush is working, the content is readable on disk BEFORE Close().
            // We open the file independently while the Logger still holds it open.
            TempFile tf("at_log_flush.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            logger.LogInfo("flush_probe");

            // Read the file while Logger is still open (has not called Close)
            std::string content = tf.ReadAll();

            logger.Close();   // close after read-back

            Assert::IsTrue(content.find("flush_probe") != std::string::npos,
                L"LogInfo content must be readable on disk before Close() is called "
                L"— flush must be called after every write (REQ-LOG-050)");
        }

        TEST_METHOD(LogError_FlushedImmediately_ReadableWithoutClose)
        {
            // REQ-LOG-050
            TempFile tf("at_log_flush_err.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            logger.LogError("flush_error_probe");

            std::string content = tf.ReadAll();
            logger.Close();

            Assert::IsTrue(content.find("flush_error_probe") != std::string::npos,
                L"LogError content must be on disk before Close() (REQ-LOG-050)");
        }

        TEST_METHOD(LogPacket_FlushedImmediately_ReadableWithoutClose)
        {
            // REQ-LOG-050
            TempFile tf("at_log_flush_pkt.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            Packet pkt(PacketType::HEARTBEAT, 1U);
            logger.LogPacket("TX", pkt, "OK");

            std::string content = tf.ReadAll();
            logger.Close();

            Assert::IsTrue(content.find("HEARTBEAT") != std::string::npos,
                L"LogPacket content must be on disk before Close() (REQ-LOG-050)");
        }

        // =====================================================================
        // REQ-LOG-010 — LogPacket() format: direction, type, SEQ, FLT, SIZE, STATUS
        // =====================================================================

        TEST_METHOD(LogPacket_TX_ContainsAllRequiredFields)
        {
            // REQ-LOG-010
            // Expected format:
            //   <ts> | TX | HEARTBEAT | SEQ:0001 | FLT:801 | SIZE:27 | STATUS:OK
            TempFile tf("at_log_pkt_tx.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            Packet pkt(PacketType::HEARTBEAT, 801U);
            pkt.SetSequenceNumber(1U);
            logger.LogPacket("TX", pkt, "OK");

            logger.Close();

            Assert::IsTrue(tf.Contains("| TX |"),
                L"LogPacket must write '| TX |' for a TX event (REQ-LOG-010)");
            Assert::IsTrue(tf.Contains("| HEARTBEAT |"),
                L"LogPacket must write the packet type string (REQ-LOG-010)");
            Assert::IsTrue(tf.Contains("SEQ:"),
                L"LogPacket must write a SEQ: field (REQ-LOG-010)");
            Assert::IsTrue(tf.Contains("FLT:801"),
                L"LogPacket must write the correct FLT: field (REQ-LOG-010)");
            Assert::IsTrue(tf.Contains("SIZE:"),
                L"LogPacket must write a SIZE: field (REQ-LOG-010)");
            Assert::IsTrue(tf.Contains("STATUS:OK"),
                L"LogPacket must write STATUS:OK (REQ-LOG-010)");
        }

        TEST_METHOD(LogPacket_RX_ContainsRxDirection)
        {
            // REQ-LOG-010
            TempFile tf("at_log_pkt_rx.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            Packet pkt(PacketType::ACK, 42U);
            logger.LogPacket("RX", pkt, "OK");

            logger.Close();

            Assert::IsTrue(tf.Contains("| RX |"),
                L"LogPacket must write '| RX |' for an RX event (REQ-LOG-010)");
        }

        TEST_METHOD(LogPacket_SizeIsHeaderPlusPayload)
        {
            // REQ-LOG-010 — SIZE = sizeof(PacketHeader) + payload bytes
            // HEARTBEAT with no payload → SIZE:27
            TempFile tf("at_log_pkt_size.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            Packet pkt(PacketType::HEARTBEAT, 10U);
            // No payload — wire size = 27 bytes (header only)
            logger.LogPacket("TX", pkt, "OK");

            logger.Close();

            Assert::IsTrue(tf.Contains("SIZE:27"),
                L"LogPacket SIZE for a no-payload HEARTBEAT must be 27 (REQ-LOG-010)");
        }

        TEST_METHOD(LogPacket_SizeIncludesPayload)
        {
            // REQ-LOG-010 — POSITION_REPORT with 24-byte payload → SIZE:51
            TempFile tf("at_log_pkt_size_payload.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            Packet pkt(PacketType::POSITION_REPORT, 801U);
            std::vector<uint8_t> payload(24U, 0x00U);
            pkt.SetPayload(payload);
            logger.LogPacket("TX", pkt, "OK");

            logger.Close();

            Assert::IsTrue(tf.Contains("SIZE:51"),
                L"LogPacket SIZE for POSITION_REPORT with 24-byte payload must be 51 (REQ-LOG-010)");
        }

        TEST_METHOD(LogPacket_StatusRetransmit_WrittenCorrectly)
        {
            // REQ-LOG-010 — RETRANSMIT status must appear verbatim
            TempFile tf("at_log_pkt_retx.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            Packet pkt(PacketType::CONNECT, 5U);
            logger.LogPacket("TX", pkt, "RETRANSMIT");

            logger.Close();

            Assert::IsTrue(tf.Contains("STATUS:RETRANSMIT"),
                L"LogPacket must write STATUS:RETRANSMIT when status is RETRANSMIT (REQ-LOG-010)");
        }

        TEST_METHOD(LogPacket_StatusError_WrittenCorrectly)
        {
            // REQ-LOG-010
            TempFile tf("at_log_pkt_err.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            Packet pkt(PacketType::CONNECT, 5U);
            logger.LogPacket("TX", pkt, "ERROR");

            logger.Close();

            Assert::IsTrue(tf.Contains("STATUS:ERROR"),
                L"LogPacket must write STATUS:ERROR when status is ERROR (REQ-LOG-010)");
        }

        TEST_METHOD(LogPacket_DefaultStatusIsOk)
        {
            // REQ-LOG-010 — default parameter for status is "OK"
            TempFile tf("at_log_pkt_default.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            Packet pkt(PacketType::HEARTBEAT, 1U);
            logger.LogPacket("TX", pkt);   // no status argument → defaults to "OK"

            logger.Close();

            Assert::IsTrue(tf.Contains("STATUS:OK"),
                L"LogPacket default status must be OK (REQ-LOG-010)");
        }

        TEST_METHOD(LogPacket_SeqNumberZeroPaddedToFourDigits)
        {
            // REQ-LOG-010 — SEQ field is zero-padded to 4 digits (std::setw(4))
            TempFile tf("at_log_pkt_seq.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            Packet pkt(PacketType::HEARTBEAT, 1U);
            pkt.SetSequenceNumber(7U);
            logger.LogPacket("TX", pkt, "OK");

            logger.Close();

            Assert::IsTrue(tf.Contains("SEQ:0007"),
                L"LogPacket must zero-pad the sequence number to 4 digits (REQ-LOG-010)");
        }

        TEST_METHOD(LogPacket_AllKnownPacketTypes_WrittenWithCorrectTypeString)
        {
            // REQ-LOG-010 — the type string comes from Packet::TypeString()
            // which delegates to PacketTypeToString().  Spot-check two types
            // not covered by other tests.
            TempFile tf("at_log_pkt_types.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            Packet pkt1(PacketType::DISCONNECT, 1U);
            Packet pkt2(PacketType::HANDOFF_INSTRUCT, 2U);
            logger.LogPacket("TX", pkt1, "OK");
            logger.LogPacket("TX", pkt2, "OK");

            logger.Close();

            Assert::IsTrue(tf.Contains("DISCONNECT"),
                L"LogPacket must write DISCONNECT for a DISCONNECT packet (REQ-LOG-010)");
            Assert::IsTrue(tf.Contains("HANDOFF_INSTRUCT"),
                L"LogPacket must write HANDOFF_INSTRUCT for a HANDOFF_INSTRUCT packet (REQ-LOG-010)");
        }

        // =====================================================================
        // REQ-LOG-060 — LogStateChange() format: STATE_CHANGE, FLT, FROM, TO, TRIGGER
        // =====================================================================

        TEST_METHOD(LogStateChange_ContainsStateChangeKeyword)
        {
            // REQ-LOG-060
            // Expected format:
            //   <ts> | STATE_CHANGE | FLT:801 | FROM:CONNECTED | TO:TRACKING | TRIGGER:POSITION_REPORT
            TempFile tf("at_log_stc.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            logger.LogStateChange(801U, "CONNECTED", "TRACKING", "POSITION_REPORT");

            logger.Close();

            Assert::IsTrue(tf.Contains("| STATE_CHANGE"),
                L"LogStateChange must write '| STATE_CHANGE' (REQ-LOG-060)");
        }

        TEST_METHOD(LogStateChange_ContainsFlightId)
        {
            // REQ-LOG-060
            TempFile tf("at_log_stc_flt.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            logger.LogStateChange(999U, "IDLE", "CONNECTED", "handshake");

            logger.Close();

            Assert::IsTrue(tf.Contains("FLT:999"),
                L"LogStateChange must write the correct FLT: field (REQ-LOG-060)");
        }

        TEST_METHOD(LogStateChange_ContainsFromAndToState)
        {
            // REQ-LOG-060
            TempFile tf("at_log_stc_fromto.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            logger.LogStateChange(1U, "TRACKING", "HANDOFF_INITIATED", "sector boundary");

            logger.Close();

            Assert::IsTrue(tf.Contains("FROM:TRACKING"),
                L"LogStateChange must write FROM:<fromState> (REQ-LOG-060)");
            Assert::IsTrue(tf.Contains("TO:HANDOFF_INITIATED"),
                L"LogStateChange must write TO:<toState> (REQ-LOG-060)");
        }

        TEST_METHOD(LogStateChange_ContainsTrigger)
        {
            // REQ-LOG-060
            TempFile tf("at_log_stc_trig.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            logger.LogStateChange(2U, "HANDOFF_PENDING", "HANDOFF_COMPLETE", "HANDOFF_ACK_received");

            logger.Close();

            Assert::IsTrue(tf.Contains("TRIGGER:HANDOFF_ACK_received"),
                L"LogStateChange must write TRIGGER:<trigger> (REQ-LOG-060)");
        }

        TEST_METHOD(LogStateChange_AllFieldsOnOneLine)
        {
            // REQ-LOG-060 — the entire state change record must be on a single line
            TempFile tf("at_log_stc_oneline.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            logger.LogStateChange(801U, "CONNECTED", "TRACKING", "first_position");

            logger.Close();

            std::vector<std::string> lines = tf.Lines();
            Assert::AreEqual(static_cast<size_t>(1U), lines.size(),
                L"LogStateChange must produce exactly one log line (REQ-LOG-060)");

            const std::string& line = lines[0];
            // Verify all 5 field markers appear on that single line
            Assert::IsTrue(line.find("STATE_CHANGE") != std::string::npos, L"Missing STATE_CHANGE");
            Assert::IsTrue(line.find("FLT:801") != std::string::npos, L"Missing FLT:801");
            Assert::IsTrue(line.find("FROM:CONNECTED") != std::string::npos, L"Missing FROM:");
            Assert::IsTrue(line.find("TO:TRACKING") != std::string::npos, L"Missing TO:");
            Assert::IsTrue(line.find("TRIGGER:") != std::string::npos, L"Missing TRIGGER:");
        }

        TEST_METHOD(LogStateChange_PipeDelimitedFormat)
        {
            // REQ-LOG-060 — all fields separated by ' | '
            TempFile tf("at_log_stc_pipes.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            logger.LogStateChange(5U, "IDLE", "CONNECTED", "handshake");

            logger.Close();

            std::vector<std::string> lines = tf.Lines();
            Assert::IsFalse(lines.empty(), L"Log must not be empty");

            const std::string& line = lines[0];
            // Format: <ts> | STATE_CHANGE | FLT:5 | FROM:IDLE | TO:CONNECTED | TRIGGER:handshake
            // → 5 pipe characters
            const size_t pipes = std::count(line.begin(), line.end(), '|');
            Assert::IsTrue(pipes >= 5U,
                L"LogStateChange line must contain at least 5 pipe separators (REQ-LOG-060)");
        }

        TEST_METHOD(LogStateChange_LostContactTransition_WrittenCorrectly)
        {
            // REQ-LOG-060 — exercise a LOST_CONTACT transition (REQ-STM-040 driven)
            TempFile tf("at_log_stc_lost.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            logger.LogStateChange(77U, "TRACKING", "LOST_CONTACT", "Heartbeat timeout (REQ-STM-040)");

            logger.Close();

            Assert::IsTrue(tf.Contains("FROM:TRACKING"),
                L"FROM must be TRACKING for a lost-contact transition");
            Assert::IsTrue(tf.Contains("TO:LOST_CONTACT"),
                L"TO must be LOST_CONTACT for a lost-contact transition");
            Assert::IsTrue(tf.Contains("TRIGGER:Heartbeat timeout"),
                L"TRIGGER must contain the heartbeat timeout description");
        }

        // =====================================================================
        // REQ-LOG-060 — Sequential writes do not corrupt the file
        // =====================================================================

        TEST_METHOD(MultipleWrites_Sequential_AllLinesPresent)
        {
            // REQ-LOG-060 (also covers REQ-LOG-010, REQ-LOG-020, REQ-LOG-030)
            // Write one of every log type in sequence and verify all are present
            // and individually distinct.
            TempFile tf("at_log_multi.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            Packet pktTx(PacketType::POSITION_REPORT, 801U);
            Packet pktRx(PacketType::TRACKING_ACK, 801U);

            logger.LogInfo("multi_test_info");
            logger.LogError("multi_test_error");
            logger.LogPacket("TX", pktTx, "OK");
            logger.LogPacket("RX", pktRx, "OK");
            logger.LogStateChange(801U, "CONNECTED", "TRACKING", "first_pos");

            logger.Close();

            std::vector<std::string> lines = tf.Lines();
            Assert::AreEqual(static_cast<size_t>(5U), lines.size(),
                L"Five log calls must produce exactly 5 lines (REQ-LOG-060)");

            std::string content = tf.ReadAll();
            Assert::IsTrue(content.find("multi_test_info") != std::string::npos, L"LogInfo line missing");
            Assert::IsTrue(content.find("multi_test_error") != std::string::npos, L"LogError line missing");
            Assert::IsTrue(content.find("POSITION_REPORT") != std::string::npos, L"TX packet line missing");
            Assert::IsTrue(content.find("TRACKING_ACK") != std::string::npos, L"RX packet line missing");
            Assert::IsTrue(content.find("STATE_CHANGE") != std::string::npos, L"State change line missing");
        }

        TEST_METHOD(MultipleWrites_PreservesOrder)
        {
            // REQ-LOG-060 — lines must appear in the order written
            TempFile tf("at_log_order.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            logger.LogInfo("line_one");
            logger.LogInfo("line_two");
            logger.LogInfo("line_three");

            logger.Close();

            std::vector<std::string> lines = tf.Lines();
            Assert::AreEqual(static_cast<size_t>(3U), lines.size(),
                L"Three LogInfo calls must produce 3 lines");

            Assert::IsTrue(lines[0].find("line_one") != std::string::npos, L"First line must contain line_one");
            Assert::IsTrue(lines[1].find("line_two") != std::string::npos, L"Second line must contain line_two");
            Assert::IsTrue(lines[2].find("line_three") != std::string::npos, L"Third line must contain line_three");
        }

        TEST_METHOD(MultipleWrites_LargeVolume_NoCorruption)
        {
            // REQ-LOG-060 — write 100 entries and verify count and no truncation
            TempFile tf("at_log_large.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());

            const uint32_t N = 100U;
            for (uint32_t i = 0U; i < N; ++i)
            {
                logger.LogInfo("entry_" + std::to_string(i));
            }

            logger.Close();

            std::vector<std::string> lines = tf.Lines();
            Assert::AreEqual(static_cast<size_t>(N), lines.size(),
                L"100 LogInfo calls must produce exactly 100 lines with no corruption (REQ-LOG-060)");

            // Spot-check first and last entries
            Assert::IsTrue(lines.front().find("entry_0") != std::string::npos,
                L"First line must be entry_0");
            Assert::IsTrue(lines.back().find("entry_99") != std::string::npos,
                L"Last line must be entry_99");
        }

        TEST_METHOD(MultipleWrites_AfterReinit_NoPreviousContentLost)
        {
            // REQ-LOG-040 + REQ-LOG-060 — combined: threeinit cycles on the same
            // file accumulate all entries.
            TempFile tf("at_log_reinit.log");

            for (uint32_t cycle = 0U; cycle < 3U; ++cycle)
            {
                AeroTrack::Logger logger;
                (void)logger.Init(tf.Path());
                logger.LogInfo("cycle_" + std::to_string(cycle));
                logger.Close();
            }

            std::vector<std::string> lines = tf.Lines();
            Assert::AreEqual(static_cast<size_t>(3U), lines.size(),
                L"Three reinit cycles on the same file must accumulate 3 lines (REQ-LOG-040/060)");
            Assert::IsTrue(lines[0].find("cycle_0") != std::string::npos, L"cycle_0 missing");
            Assert::IsTrue(lines[1].find("cycle_1") != std::string::npos, L"cycle_1 missing");
            Assert::IsTrue(lines[2].find("cycle_2") != std::string::npos, L"cycle_2 missing");
        }

        // =====================================================================
        // REQ-LOG-020 — Timestamp format (structural check)
        //
        // We cannot hard-code an expected timestamp value because the clock
        // advances during the test.  Instead we verify structural properties:
        //   - Contains exactly one '.', one '-', one ':' in the expected
        //     positions relative to each other.
        //   - The timestamp prefix is at least 23 characters long
        //     (YYYY-MM-DD HH:MM:SS.mmm = 23 chars).
        // =====================================================================

        TEST_METHOD(LogInfo_TimestampPrefix_HasMinimumLength)
        {
            // REQ-LOG-020
            TempFile tf("at_log_ts_len.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            logger.LogInfo("ts_len_probe");
            logger.Close();

            std::vector<std::string> lines = tf.Lines();
            Assert::IsFalse(lines.empty(), L"Log must contain at least one line");

            const std::string& line = lines[0];
            // The timestamp occupies the first field up to the first ' | '
            const size_t pipePos = line.find(" | ");
            Assert::IsTrue(pipePos != std::string::npos,
                L"Log line must contain ' | ' separator after timestamp");
            // YYYY-MM-DD HH:MM:SS.mmm = 23 characters
            Assert::IsTrue(pipePos >= 23U,
                L"Timestamp prefix must be at least 23 characters (REQ-LOG-020)");
        }

        TEST_METHOD(LogInfo_TimestampPrefix_ContainsDateTimeSeparators)
        {
            // REQ-LOG-020 — verify the 'T'-less ISO-8601-like format:
            // "YYYY-MM-DD HH:MM:SS.mmm" has '-', ':', '.' separators
            TempFile tf("at_log_ts_sep.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            logger.LogInfo("sep_probe");
            logger.Close();

            std::vector<std::string> lines = tf.Lines();
            Assert::IsFalse(lines.empty(), L"Log must contain at least one line");

            const std::string& line = lines[0];
            const size_t pipePos = line.find(" | ");
            Assert::IsTrue(pipePos != std::string::npos, L"Missing ' | ' separator");

            const std::string ts = line.substr(0U, pipePos);

            Assert::IsTrue(ts.find('-') != std::string::npos,
                L"Timestamp must contain '-' (date separator) (REQ-LOG-020)");
            Assert::IsTrue(ts.find(':') != std::string::npos,
                L"Timestamp must contain ':' (time separator) (REQ-LOG-020)");
            Assert::IsTrue(ts.find('.') != std::string::npos,
                L"Timestamp must contain '.' (millisecond separator) (REQ-LOG-020)");
        }

        TEST_METHOD(LogInfo_Timestamp_ThreeDigitMillisecondSuffix)
        {
            // REQ-LOG-020 — millisecond field after '.' must be exactly 3 digits
            TempFile tf("at_log_ts_ms.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            logger.LogInfo("ms_probe");
            logger.Close();

            std::vector<std::string> lines = tf.Lines();
            Assert::IsFalse(lines.empty(), L"Log must contain at least one line");

            const std::string& line = lines[0];
            const size_t dotPos = line.find('.');
            const size_t pipePos = line.find(" | ");

            Assert::IsTrue(dotPos != std::string::npos, L"Timestamp must contain a '.'");
            Assert::IsTrue(pipePos != std::string::npos, L"Line must contain ' | '");
            Assert::IsTrue(dotPos < pipePos, L"'.' must appear before first ' | '");

            // Characters between '.' and first ' | ' should be exactly 3 digits
            const std::string msStr = line.substr(dotPos + 1U, pipePos - dotPos - 1U);
            Assert::AreEqual(static_cast<size_t>(3U), msStr.size(),
                L"Millisecond suffix must be exactly 3 characters (REQ-LOG-020)");

            const bool allDigits = (msStr[0] >= '0' && msStr[0] <= '9') &&
                (msStr[1] >= '0' && msStr[1] <= '9') &&
                (msStr[2] >= '0' && msStr[2] <= '9');
            Assert::IsTrue(allDigits,
                L"Millisecond suffix must consist of exactly 3 decimal digits (REQ-LOG-020)");
        }

        // =====================================================================
        // Defensive: writes to an unopened logger must not crash
        // =====================================================================

        TEST_METHOD(WriteBeforeInit_LogInfo_DoesNotCrash)
        {
            // REQ-LOG-030 — WriteEntry guards with is_open(); calling log methods
            // without Init must be a safe no-op.
            AeroTrack::Logger logger;
            // No Init() — file stream is closed
            logger.LogInfo("should not crash");
            Assert::IsTrue(true, L"LogInfo before Init must not crash (defensive guard)");
        }

        TEST_METHOD(WriteBeforeInit_LogError_DoesNotCrash)
        {
            // REQ-LOG-030
            AeroTrack::Logger logger;
            logger.LogError("should not crash");
            Assert::IsTrue(true, L"LogError before Init must not crash");
        }

        TEST_METHOD(WriteBeforeInit_LogPacket_DoesNotCrash)
        {
            // REQ-LOG-010, REQ-LOG-030
            AeroTrack::Logger logger;
            Packet pkt(PacketType::HEARTBEAT, 1U);
            logger.LogPacket("TX", pkt, "OK");
            Assert::IsTrue(true, L"LogPacket before Init must not crash");
        }

        TEST_METHOD(WriteBeforeInit_LogStateChange_DoesNotCrash)
        {
            // REQ-LOG-060, REQ-LOG-030
            AeroTrack::Logger logger;
            logger.LogStateChange(1U, "IDLE", "CONNECTED", "test");
            Assert::IsTrue(true, L"LogStateChange before Init must not crash");
        }

        TEST_METHOD(Close_CalledTwice_DoesNotCrash)
        {
            // REQ-LOG-030 — Close() uses is_open() guard; double-close is safe.
            TempFile tf("at_log_dbl_close.log");
            AeroTrack::Logger logger;
            (void)logger.Init(tf.Path());
            logger.Close();
            logger.Close();   // second call must be a no-op
            Assert::IsTrue(true, L"Calling Close() twice must not crash");
        }

    };  // TEST_CLASS(LoggerTests)

}  // namespace AeroTrackTests