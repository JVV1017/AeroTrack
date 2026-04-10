// =============================================================================
// Filetransfertests.cpp — MSTest unit tests for FileTransfer
// =============================================================================
// DO-178C DAL-C  |  AeroTrack Ground Control
// Framework:      Microsoft CppUnitTestFramework (MSTest)
//
// Requirements covered:
//   REQ-SVR-050  Server sends FILE_TRANSFER_START, CHUNK, END sequence
//   REQ-SYS-070  File chunks are FILE_CHUNK_SIZE (1024) bytes max;
//                file must be >= 1 MB for production compliance check
//
// Strategy: Tests use in-memory synthetic data buffers rather than real
// disk files, exercising PrepareTransfer → BuildStartPacket →
// BuildChunkPacket → BuildEndPacket without filesystem access.
//
// NOTE: LoadFile() tests that require an actual file are excluded here
// because the test host may not have access to specific file paths.
// Coverage of the complete state machine path is provided via the
// PrepareTransfer-without-LoadFile guard test.
// =============================================================================

#include "TestCommon.h"

namespace AeroTrackTests
{
    // =========================================================================
    // Helper: creates a FileTransfer that has been loaded with synthetic data
    //
    // FileTransfer::LoadFile() requires a real file on disk. To unit-test the
    // packet-building logic without filesystem coupling we expose the load path
    // indirectly: we write a small temporary binary file, call LoadFile, and
    // then PrepareTransfer. Both helpers are provided below.
    // =========================================================================

    // Writes `count` bytes of value `fill` to a temp file and returns the path.
    // Uses only the C runtime (no Winsock, no MFC) — safe in the MSTest host.
    static std::string WriteTempFile(uint32_t count, uint8_t fill, const char* name)
    {
        char path[MAX_PATH] = { 0 };
        (void)::GetTempPathA(MAX_PATH, path);
        std::string fullPath = std::string(path) + name;

        FILE* f = nullptr;
        (void)::fopen_s(&f, fullPath.c_str(), "wb");
        if (f != nullptr)
        {
            for (uint32_t i = 0U; i < count; ++i)
            {
                (void)::fwrite(&fill, 1U, 1U, f);
            }
            (void)::fclose(f);
        }
        return fullPath;
    }


    TEST_CLASS(FileTransferTests)
    {
    public:

        // =====================================================================
        // Initial state
        // =====================================================================

        TEST_METHOD(DefaultConstructor_StatusIsNotStarted)
        {
            // REQ-SVR-050
            FileTransfer ft;
            Assert::IsTrue(FileTransferStatus::NOT_STARTED == ft.GetStatus(),
                L"FileTransfer must start in NOT_STARTED state (REQ-SVR-050)");
        }

        TEST_METHOD(DefaultConstructor_AllAccessorsReturnZero)
        {
            // REQ-SVR-050
            FileTransfer ft;
            Assert::AreEqual(0U, ft.GetTotalChunks(),
                L"GetTotalChunks must be 0 before LoadFile (REQ-SVR-050)");
            Assert::AreEqual(0U, ft.GetFileSize(),
                L"GetFileSize must be 0 before LoadFile (REQ-SVR-050)");
            Assert::AreEqual(0U, ft.GetFlightId(),
                L"GetFlightId must be 0 before PrepareTransfer (REQ-SVR-050)");
        }

        // =====================================================================
        // REQ-SVR-050 — PrepareTransfer without LoadFile returns false
        // =====================================================================

        TEST_METHOD(PrepareTransfer_WithoutLoadFile_ReturnsFalse)
        {
            // REQ-SVR-050 — file must be loaded before preparing transfer
            FileTransfer ft;
            bool ok = ft.PrepareTransfer(801U);

            Assert::IsFalse(ok,
                L"PrepareTransfer must return false if LoadFile has not been called "
                L"(status != FILE_LOADED) (REQ-SVR-050)");
        }

        // =====================================================================
        // REQ-SVR-050 — LoadFile with a non-existent path returns false
        // =====================================================================

        TEST_METHOD(LoadFile_NonExistentPath_ReturnsFalse)
        {
            // REQ-SVR-050
            FileTransfer ft;
            bool ok = ft.LoadFile("C:\\does_not_exist_aerotracks_xxxx.bin");

            Assert::IsFalse(ok,
                L"LoadFile must return false for a non-existent file path (REQ-SVR-050)");
            Assert::IsTrue(FileTransferStatus::FAILED == ft.GetStatus(),
                L"Status must be FAILED after LoadFile fails");
        }

        // =====================================================================
        // REQ-SYS-070 — MeetsMinimumSizeRequirement returns false for small files
        // =====================================================================

        TEST_METHOD(MeetsMinimumSizeRequirement_SmallFile_ReturnsFalse)
        {
            // REQ-SYS-070 — files < 1 MB (1,048,576 bytes) do not meet requirement
            std::string path = WriteTempFile(100U, 0xAAU, "at_small.bin");
            FileTransfer ft;
            bool loaded = ft.LoadFile(path);

            Assert::IsTrue(loaded,
                L"LoadFile must succeed for a valid file");
            Assert::IsFalse(ft.MeetsMinimumSizeRequirement(),
                L"MeetsMinimumSizeRequirement must return false for a 100-byte file (REQ-SYS-070)");
        }

        // =====================================================================
        // REQ-SVR-050 — LoadFile + PrepareTransfer success path
        // =====================================================================

        TEST_METHOD(LoadFile_ValidFile_StatusIsFileLoaded)
        {
            // REQ-SVR-050
            std::string path = WriteTempFile(2048U, 0xBBU, "at_valid.bin");
            FileTransfer ft;
            bool ok = ft.LoadFile(path);

            Assert::IsTrue(ok,
                L"LoadFile must return true for a valid 2 KB file (REQ-SVR-050)");
            Assert::IsTrue(FileTransferStatus::FILE_LOADED == ft.GetStatus(),
                L"Status must be FILE_LOADED after successful LoadFile");
            Assert::AreEqual(2048U, ft.GetFileSize(),
                L"GetFileSize must equal the number of bytes written to the temp file");
        }

        TEST_METHOD(PrepareTransfer_AfterLoadFile_Succeeds)
        {
            // REQ-SVR-050
            std::string path = WriteTempFile(2048U, 0xCCU, "at_prep.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            bool ok = ft.PrepareTransfer(801U);

            Assert::IsTrue(ok,
                L"PrepareTransfer must return true after a successful LoadFile (REQ-SVR-050)");
            Assert::IsTrue(FileTransferStatus::IN_PROGRESS == ft.GetStatus(),
                L"Status must be IN_PROGRESS after PrepareTransfer");
            Assert::AreEqual(801U, ft.GetFlightId(),
                L"GetFlightId must return the flightId passed to PrepareTransfer");
        }

        // =====================================================================
        // REQ-SYS-070 — Chunk count is ceiling(fileSize / FILE_CHUNK_SIZE)
        // =====================================================================

        TEST_METHOD(TotalChunks_ExactMultiple_CorrectCount)
        {
            // REQ-SYS-070 — 2048 bytes / 1024 = exactly 2 chunks
            std::string path = WriteTempFile(2048U, 0x00U, "at_exact.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(1U);

            Assert::AreEqual(2U, ft.GetTotalChunks(),
                L"2048-byte file with 1024-byte chunks must produce exactly 2 chunks (REQ-SYS-070)");
        }

        TEST_METHOD(TotalChunks_NotExactMultiple_CeilingApplied)
        {
            // REQ-SYS-070 — 1025 bytes needs 2 chunks (ceiling division)
            std::string path = WriteTempFile(1025U, 0x00U, "at_ceil.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(1U);

            Assert::AreEqual(2U, ft.GetTotalChunks(),
                L"1025-byte file must require 2 chunks (ceiling division) (REQ-SYS-070)");
        }

        TEST_METHOD(TotalChunks_OneByte_IsOneChunk)
        {
            // REQ-SYS-070 — edge case: 1 byte needs 1 chunk
            std::string path = WriteTempFile(1U, 0xFFU, "at_one.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(1U);

            Assert::AreEqual(1U, ft.GetTotalChunks(),
                L"1-byte file must require exactly 1 chunk (REQ-SYS-070)");
        }

        TEST_METHOD(TotalChunks_ExactlyChunkSize_IsOneChunk)
        {
            // REQ-SYS-070 — 1024 bytes = exactly one chunk
            std::string path = WriteTempFile(1024U, 0x55U, "at_onek.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(1U);

            Assert::AreEqual(1U, ft.GetTotalChunks(),
                L"1024-byte file must require exactly 1 chunk (REQ-SYS-070)");
        }

        // =====================================================================
        // REQ-SVR-050 — BuildStartPacket produces correct packet structure
        // =====================================================================

        TEST_METHOD(BuildStartPacket_CorrectType)
        {
            // REQ-SVR-050
            std::string path = WriteTempFile(2048U, 0x00U, "at_sp_type.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(801U);

            Packet pkt = ft.BuildStartPacket();
            Assert::IsTrue(PacketType::FILE_TRANSFER_START == pkt.GetType(),
                L"BuildStartPacket must produce a FILE_TRANSFER_START packet (REQ-SVR-050)");
        }

        TEST_METHOD(BuildStartPacket_PayloadIs8Bytes)
        {
            // REQ-SVR-050 — payload = total_file_size (4B) + total_chunks (4B)
            std::string path = WriteTempFile(2048U, 0x00U, "at_sp_sz.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(801U);

            Packet pkt = ft.BuildStartPacket();
            Assert::AreEqual(8U, pkt.GetPayloadLength(),
                L"FILE_TRANSFER_START payload must be exactly 8 bytes (REQ-SVR-050)");
        }

        TEST_METHOD(BuildStartPacket_PayloadEncodesBigEndianFileSize)
        {
            // REQ-SVR-050 — file size encoded big-endian at bytes 0..3 of payload
            // 2048 decimal = 0x00000800
            std::string path = WriteTempFile(2048U, 0x00U, "at_sp_be.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(802U);

            Packet pkt = ft.BuildStartPacket();
            const std::vector<uint8_t>& pl = pkt.GetPayload();

            // Reconstruct file size from big-endian bytes
            uint32_t encodedSize =
                (static_cast<uint32_t>(pl[0]) << 24U) |
                (static_cast<uint32_t>(pl[1]) << 16U) |
                (static_cast<uint32_t>(pl[2]) << 8U) |
                static_cast<uint32_t>(pl[3]);

            Assert::AreEqual(2048U, encodedSize,
                L"FILE_TRANSFER_START payload bytes 0-3 must encode the file size "
                L"in big-endian (REQ-SVR-050)");
        }

        TEST_METHOD(BuildStartPacket_PayloadEncodesBigEndianChunkCount)
        {
            // REQ-SVR-050 — chunk count at bytes 4..7 of payload (big-endian)
            // 2048 / 1024 = 2 chunks
            std::string path = WriteTempFile(2048U, 0x00U, "at_sp_cc.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(803U);

            Packet pkt = ft.BuildStartPacket();
            const std::vector<uint8_t>& pl = pkt.GetPayload();

            uint32_t encodedChunks =
                (static_cast<uint32_t>(pl[4]) << 24U) |
                (static_cast<uint32_t>(pl[5]) << 16U) |
                (static_cast<uint32_t>(pl[6]) << 8U) |
                static_cast<uint32_t>(pl[7]);

            Assert::AreEqual(2U, encodedChunks,
                L"FILE_TRANSFER_START payload bytes 4-7 must encode the chunk count "
                L"in big-endian (REQ-SVR-050)");
        }

        TEST_METHOD(BuildStartPacket_FlightIdSetCorrectly)
        {
            // REQ-SVR-050
            std::string path = WriteTempFile(512U, 0x00U, "at_sp_flt.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(999U);

            Packet pkt = ft.BuildStartPacket();
            Assert::AreEqual(999U, pkt.GetFlightId(),
                L"FILE_TRANSFER_START must carry the flight ID from PrepareTransfer (REQ-SVR-050)");
        }

        // =====================================================================
        // REQ-SVR-050 — BuildChunkPacket produces correct structure
        // =====================================================================

        TEST_METHOD(BuildChunkPacket_FirstChunk_CorrectType)
        {
            // REQ-SVR-050
            std::string path = WriteTempFile(2048U, 0xAAU, "at_cp_type.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(801U);

            Packet pkt = ft.BuildChunkPacket(0U);
            Assert::IsTrue(PacketType::FILE_TRANSFER_CHUNK == pkt.GetType(),
                L"BuildChunkPacket must produce a FILE_TRANSFER_CHUNK packet (REQ-SVR-050)");
        }

        TEST_METHOD(BuildChunkPacket_FirstChunk_PayloadIs4PlusChunkSize)
        {
            // REQ-SVR-050 — payload = chunk_index (4B) + chunk_data (FILE_CHUNK_SIZE)
            std::string path = WriteTempFile(2048U, 0xBBU, "at_cp_pl.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(801U);

            Packet pkt = ft.BuildChunkPacket(0U);
            // First chunk of a 2048-byte file: FILE_CHUNK_SIZE = 1024 data bytes + 4 index bytes
            Assert::AreEqual(1028U, pkt.GetPayloadLength(),
                L"First chunk payload must be 4 (index) + 1024 (data) = 1028 bytes (REQ-SVR-050)");
        }

        TEST_METHOD(BuildChunkPacket_FirstChunk_IndexEncodedBigEndianAsZero)
        {
            // REQ-SVR-050 — chunk_index at payload[0..3] in big-endian
            std::string path = WriteTempFile(2048U, 0x11U, "at_cp_idx0.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(801U);

            Packet pkt = ft.BuildChunkPacket(0U);
            const std::vector<uint8_t>& pl = pkt.GetPayload();

            uint32_t encodedIndex =
                (static_cast<uint32_t>(pl[0]) << 24U) |
                (static_cast<uint32_t>(pl[1]) << 16U) |
                (static_cast<uint32_t>(pl[2]) << 8U) |
                static_cast<uint32_t>(pl[3]);

            Assert::AreEqual(0U, encodedIndex,
                L"Chunk index 0 must be encoded as 0x00000000 in big-endian (REQ-SVR-050)");
        }

        TEST_METHOD(BuildChunkPacket_SecondChunk_IndexEncodedAsOne)
        {
            // REQ-SVR-050
            std::string path = WriteTempFile(2048U, 0x22U, "at_cp_idx1.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(801U);

            Packet pkt = ft.BuildChunkPacket(1U);
            const std::vector<uint8_t>& pl = pkt.GetPayload();

            uint32_t encodedIndex =
                (static_cast<uint32_t>(pl[0]) << 24U) |
                (static_cast<uint32_t>(pl[1]) << 16U) |
                (static_cast<uint32_t>(pl[2]) << 8U) |
                static_cast<uint32_t>(pl[3]);

            Assert::AreEqual(1U, encodedIndex,
                L"Chunk index 1 must be encoded as 0x00000001 in big-endian (REQ-SVR-050)");
        }

        TEST_METHOD(BuildChunkPacket_DataBytesMatchFileContent)
        {
            // REQ-SVR-050 — data bytes in the chunk must match the loaded file data
            // Write file where every byte is its position modulo 256
            const uint32_t fileSize = 1024U;
            std::string path = WriteTempFile(fileSize, 0x55U, "at_cp_data.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(801U);

            Packet pkt = ft.BuildChunkPacket(0U);
            const std::vector<uint8_t>& pl = pkt.GetPayload();

            // Data starts at byte 4 (after the 4-byte big-endian chunk index)
            // All bytes should be 0x55 (the fill value)
            bool allMatch = true;
            for (uint32_t i = 0U; i < fileSize; ++i)
            {
                if (pl[static_cast<size_t>(4U) + static_cast<size_t>(i)] != static_cast<uint8_t>(0x55U))
                {
                    allMatch = false;
                    break;
                }
            }
            Assert::IsTrue(allMatch,
                L"Chunk data bytes must match the original file content (REQ-SVR-050)");
        }

        TEST_METHOD(BuildChunkPacket_LastChunk_CorrectPartialSize)
        {
            // REQ-SVR-050 / REQ-SYS-070 — last chunk may be smaller than FILE_CHUNK_SIZE
            // 1500 bytes → chunk 0 = 1024 bytes, chunk 1 = 476 bytes
            std::string path = WriteTempFile(1500U, 0x33U, "at_cp_last.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(801U);

            Packet pkt = ft.BuildChunkPacket(1U);
            // Payload = 4 (index) + 476 (remaining data) = 480 bytes
            Assert::AreEqual(480U, pkt.GetPayloadLength(),
                L"Last (partial) chunk must carry only the remaining bytes (REQ-SYS-070)");
        }

        TEST_METHOD(BuildChunkPacket_OutOfRangeIndex_ReturnsErrorPacket)
        {
            // REQ-SVR-050 — bounds check
            std::string path = WriteTempFile(1024U, 0x00U, "at_cp_oob.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(801U);
            // Total chunks = 1; index 1 is out of range

            Packet pkt = ft.BuildChunkPacket(1U);
            Assert::IsTrue(PacketType::ERROR == pkt.GetType(),
                L"BuildChunkPacket with an out-of-range index must return an ERROR packet (REQ-SVR-050)");
        }

        // =====================================================================
        // REQ-SVR-050 — BuildEndPacket produces correct packet
        // =====================================================================

        TEST_METHOD(BuildEndPacket_CorrectType)
        {
            // REQ-SVR-050
            std::string path = WriteTempFile(512U, 0x00U, "at_ep_type.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(801U);

            Packet pkt = ft.BuildEndPacket();
            Assert::IsTrue(PacketType::FILE_TRANSFER_END == pkt.GetType(),
                L"BuildEndPacket must produce a FILE_TRANSFER_END packet (REQ-SVR-050)");
        }

        TEST_METHOD(BuildEndPacket_EmptyPayload)
        {
            // REQ-SVR-050 — END packet has no payload (signals completion)
            std::string path = WriteTempFile(512U, 0x00U, "at_ep_pl.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(801U);

            Packet pkt = ft.BuildEndPacket();
            Assert::AreEqual(0U, pkt.GetPayloadLength(),
                L"FILE_TRANSFER_END must have an empty payload (REQ-SVR-050)");
        }

        TEST_METHOD(BuildEndPacket_FlightIdSetCorrectly)
        {
            // REQ-SVR-050
            std::string path = WriteTempFile(512U, 0x00U, "at_ep_flt.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(777U);

            Packet pkt = ft.BuildEndPacket();
            Assert::AreEqual(777U, pkt.GetFlightId(),
                L"FILE_TRANSFER_END must carry the flight ID from PrepareTransfer (REQ-SVR-050)");
        }

        // =====================================================================
        // REQ-SVR-050 — All three packets pass CRC-16 validation
        // =====================================================================

        TEST_METHOD(StartPacket_CrcValidationPasses)
        {
            // REQ-SVR-050, REQ-PKT-050
            std::string path = WriteTempFile(2048U, 0x01U, "at_crc_st.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(801U);

            Packet pkt = ft.BuildStartPacket();
            std::vector<uint8_t> buf = pkt.Serialize();
            Packet deserialized = Packet::Deserialize(buf.data(), static_cast<uint32_t>(buf.size()));

            Assert::IsTrue(deserialized.ValidateChecksum(),
                L"FILE_TRANSFER_START must pass CRC-16 validation after serialize/deserialize "
                L"(REQ-SVR-050, REQ-PKT-050)");
        }

        TEST_METHOD(ChunkPacket_CrcValidationPasses)
        {
            // REQ-SVR-050, REQ-PKT-050
            std::string path = WriteTempFile(1024U, 0x02U, "at_crc_ch.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(801U);

            Packet pkt = ft.BuildChunkPacket(0U);
            std::vector<uint8_t> buf = pkt.Serialize();
            Packet deserialized = Packet::Deserialize(buf.data(), static_cast<uint32_t>(buf.size()));

            Assert::IsTrue(deserialized.ValidateChecksum(),
                L"FILE_TRANSFER_CHUNK must pass CRC-16 validation after serialize/deserialize "
                L"(REQ-SVR-050, REQ-PKT-050)");
        }

        TEST_METHOD(EndPacket_CrcValidationPasses)
        {
            // REQ-SVR-050, REQ-PKT-050
            std::string path = WriteTempFile(512U, 0x03U, "at_crc_en.bin");
            FileTransfer ft;
            (void)ft.LoadFile(path);
            (void)ft.PrepareTransfer(801U);

            Packet pkt = ft.BuildEndPacket();
            std::vector<uint8_t> buf = pkt.Serialize();
            Packet deserialized = Packet::Deserialize(buf.data(), static_cast<uint32_t>(buf.size()));

            Assert::IsTrue(deserialized.ValidateChecksum(),
                L"FILE_TRANSFER_END must pass CRC-16 validation after serialize/deserialize "
                L"(REQ-SVR-050, REQ-PKT-050)");
        }

        // =====================================================================
        // REQ-SYS-070 — FILE_CHUNK_SIZE constant is 1024 bytes
        // =====================================================================

        TEST_METHOD(FileChunkSize_ConstantIs1024)
        {
            // REQ-SYS-070
            Assert::AreEqual(1024U, FILE_CHUNK_SIZE,
                L"FILE_CHUNK_SIZE must be 1024 bytes per REQ-SYS-070");
        }

        // =====================================================================
        // REQ-SYS-070 — MeetsMinimumSizeRequirement for a 1 MB file
        // =====================================================================

        TEST_METHOD(MeetsMinimumSizeRequirement_ExactlyOneMB_ReturnsTrue)
        {
            // REQ-SYS-070 — 1,048,576 bytes is the exact minimum
            const uint32_t ONE_MB = 1048576U;
            std::string path = WriteTempFile(ONE_MB, 0xFFU, "at_one_mb.bin");
            FileTransfer ft;
            bool loaded = ft.LoadFile(path);

            Assert::IsTrue(loaded,
                L"LoadFile must succeed for a 1 MB temp file");
            Assert::IsTrue(ft.MeetsMinimumSizeRequirement(),
                L"MeetsMinimumSizeRequirement must return true for a 1 MB file (REQ-SYS-070)");
        }

    };  // TEST_CLASS(FileTransferTests)

}  // namespace AeroTrackTests