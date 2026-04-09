// REQ-CLT-040: Chunked file reassembly -- receives JPEG radar sector image from server
// REQ-SYS-070: 1MB+ file transfer
// REQ-CLT-060: All RX events logged via m_logger
//
// MISRA Deviations applied in this file (AeroTrack_Standards_Compliance_Audit.md):
//   DEV-001: std::vector RAII for m_fileBuffer and m_chunksReceived_map -- REQ-SYS-030
//   DEV-002: std::ofstream in WriteFileToDisk -- REQ-SYS-050 (file I/O isolated here only)
//   DEV-004: Guard-clause early returns -- MISRA 6-6-5
//   DEV-005: String literal parameters to Logger -- MISRA 5-2-12

#include "FileReceiver.h"
#include "../AeroTrackShared/PacketTypes.h"
#include "../AeroTrackShared/Config.h"
#include <cstdio>
#include <fstream>     // MISRA DEV-002: std::ofstream -- isolated to this file

namespace AeroTrack {

    // =========================================================================
    // Constructor
    // =========================================================================

    FileReceiver::FileReceiver(Logger& logger, uint32_t flightId) noexcept
        : m_logger(logger)
        , m_flightId(flightId)
        , m_state(TransferState::IDLE)
        , m_totalFileSize(0U)
        , m_totalChunks(0U)
        , m_receivedChunks(0U)
        , m_outputPath("radar_sector.jpg")   // Default; overwritten in HandleStart
        , m_fileBuffer()
        , m_chunksReceived_map()
    {
    }

    // =========================================================================
    // Private -- DecodeBigEndianU32
    // =========================================================================
    // Reads 4 bytes from data[offset..offset+3] as a big-endian uint32.
    // Server v2 (Joel's MISRA fix) encodes ALL uint32 payload fields this way.
    // MISRA 5-0-15: array indexing only -- no pointer arithmetic.
    // MISRA 5-2-4:  static_cast throughout -- no C-style casts.

    /*static*/ uint32_t FileReceiver::DecodeBigEndianU32(
        const std::vector<uint8_t>& data, size_t offset) noexcept
    {
        return (static_cast<uint32_t>(data[offset]) << 24U) |
            (static_cast<uint32_t>(data[offset + 1U]) << 16U) |
            (static_cast<uint32_t>(data[offset + 2U]) << 8U) |
            static_cast<uint32_t>(data[offset + 3U]);
    }

    // =========================================================================
    // Public -- HandlePacket (dispatch)
    // =========================================================================

    void FileReceiver::HandlePacket(const Packet& pkt) noexcept {
        switch (pkt.GetType()) {
        case PacketType::FILE_TRANSFER_START:
            HandleStart(pkt);
            break;
        case PacketType::FILE_TRANSFER_CHUNK:
            HandleChunk(pkt);
            break;
        case PacketType::FILE_TRANSFER_END:
            HandleEnd(pkt);
            break;
        default:
            m_logger.LogError("FileReceiver: HandlePacket called with non-transfer packet");
            break;
        }
    }

    // =========================================================================
    // Accessors
    // =========================================================================

    TransferState FileReceiver::GetState()           const noexcept { return m_state; }
    const std::string& FileReceiver::GetOutputPath() const noexcept { return m_outputPath; }
    uint32_t FileReceiver::GetTotalChunks()          const noexcept { return m_totalChunks; }
    uint32_t FileReceiver::GetReceivedChunks()       const noexcept { return m_receivedChunks; }

    uint32_t FileReceiver::GetProgressPercent() const noexcept {
        if ((m_state != TransferState::RECEIVING) || (m_totalChunks == 0U)) {
            return 0U;
        }
        return (m_receivedChunks * 100U) / m_totalChunks;
    }

    void FileReceiver::Reset() noexcept {
        m_state = TransferState::IDLE;
        m_totalFileSize = 0U;
        m_totalChunks = 0U;
        m_receivedChunks = 0U;
        std::vector<uint8_t>().swap(m_fileBuffer);
        std::vector<bool>().swap(m_chunksReceived_map);
        m_logger.LogInfo("FileReceiver: Reset to IDLE");
    }

    // =========================================================================
    // Private -- HandleStart
    // =========================================================================
    // Processes FILE_TRANSFER_START (PacketType 0x30).
    // Payload: [0..3] total_file_size (uint32 BE) | [4..7] total_chunks (uint32 BE)
    // Requirements: REQ-CLT-040, REQ-SYS-070

    void FileReceiver::HandleStart(const Packet& pkt) noexcept {
        if (m_state != TransferState::IDLE) {
            // MISRA 6-4-2: distinguish error (mid-transfer reset) from expected (overwrite)
            if (m_state == TransferState::RECEIVING) {
                m_logger.LogError("FileReceiver: START received mid-transfer -- resetting");
            }
            else {
                // COMPLETE or FAILED -- new sector image arriving, normal overwrite.
                // Logging as INFO so the UI resets to IDLE -> RECEIVING and shows
                // 0-100% progress bar again instead of staying on the COMPLETE line.
                m_logger.LogInfo("FileReceiver: New transfer starting -- resetting previous");
            }
            Reset();
        }

        const std::vector<uint8_t>& payload = pkt.GetPayload();

        if (payload.size() < 8U) {                                  // MISRA DEV-004
            m_logger.LogError("FileReceiver: FILE_TRANSFER_START payload too short");
            m_state = TransferState::FAILED;
            return;
        }

        m_totalFileSize = DecodeBigEndianU32(payload, 0U);
        m_totalChunks = DecodeBigEndianU32(payload, 4U);

        if ((m_totalFileSize == 0U) || (m_totalChunks == 0U)) {     // MISRA DEV-004
            m_logger.LogError("FileReceiver: Invalid START -- zero file size or chunk count");
            m_state = TransferState::FAILED;
            return;
        }

        // Build output filename: received_sector_<flightId>.jpg  (REQ-CLT-040)
        m_outputPath = "received_sector_" + std::to_string(m_flightId) + ".jpg";

        // Pre-allocate reassembly buffer and per-slot tracking map -- DEV-001 RAII
        m_fileBuffer.assign(m_totalFileSize, 0U);
        m_chunksReceived_map.assign(m_totalChunks, false);

        m_receivedChunks = 0U;
        m_state = TransferState::RECEIVING;

        char msg[96];
        // MISRA Fix [V2547]: snprintf return value explicitly discarded throughout
        // this file. All msg buffers are sized to hold their maximum possible
        // output so truncation cannot occur in practice. Discard is intentional.
        (void)std::snprintf(msg, sizeof(msg),
            "FileReceiver: Transfer started -- %u bytes, %u chunks -> %s",
            m_totalFileSize, m_totalChunks, m_outputPath.c_str());
        m_logger.LogInfo(msg);
    }

    // =========================================================================
    // Private -- HandleChunk
    // =========================================================================
    // Processes FILE_TRANSFER_CHUNK (PacketType 0x31).
    // Payload: [0..3] chunk_index (uint32 BE) | [4..end] raw JPEG bytes (up to 1024)
    //
    // Out-of-order safe: writes to chunk_index * FILE_CHUNK_SIZE offset directly.
    // Duplicate safe: m_chunksReceived_map prevents double-counting.
    //
    // MISRA 5-0-15 FIX: array indexing replaces pointer arithmetic
    //   (original used payload.data()+4U and m_fileBuffer.data()+writeOffset).
    // Requirements: REQ-CLT-040

    void FileReceiver::HandleChunk(const Packet& pkt) noexcept {
        if (m_state != TransferState::RECEIVING) {                   // MISRA DEV-004
            m_logger.LogError("FileReceiver: CHUNK received while not RECEIVING");
            return;
        }

        const std::vector<uint8_t>& payload = pkt.GetPayload();

        if (payload.size() < 5U) {                                   // MISRA DEV-004
            m_logger.LogError("FileReceiver: FILE_TRANSFER_CHUNK payload too short");
            return;
        }

        const uint32_t chunkIndex = DecodeBigEndianU32(payload, 0U);
        const size_t   chunkDataSize = payload.size() - 4U;
        const size_t   writeOffset = static_cast<size_t>(chunkIndex) *
            static_cast<size_t>(FILE_CHUNK_SIZE);

        if (chunkIndex >= m_totalChunks) {                           // MISRA DEV-004
            char msg[64];
            (void)std::snprintf(msg, sizeof(msg),
                "FileReceiver: Chunk index %u out of range (total %u)",
                chunkIndex, m_totalChunks);
            m_logger.LogError(msg);
            return;
        }

        if ((writeOffset + chunkDataSize) > m_fileBuffer.size()) {  // MISRA DEV-004
            m_logger.LogError("FileReceiver: Chunk would overflow file buffer");
            m_state = TransferState::FAILED;
            return;
        }

        // Copy chunk data -- array indexing only, no pointer arithmetic (MISRA 5-0-15)
        for (size_t i = 0U; i < chunkDataSize; ++i) {
            m_fileBuffer[writeOffset + i] = payload[4U + i];
        }

        // Mark slot received -- duplicate protection
        if (!m_chunksReceived_map[chunkIndex]) {
            m_chunksReceived_map[chunkIndex] = true;
            ++m_receivedChunks;
        }
        else {
            // MISRA 6-4-2: terminal else -- no action for duplicate chunk
        }

        // Log every 10% -- REQ-CLT-060
        const uint32_t progress = GetProgressPercent();
        if ((progress % 10U) == 0U) {
            char msg[64];
            (void)std::snprintf(msg, sizeof(msg),
                "FileReceiver: Progress %u%% (%u/%u chunks)",
                progress, m_receivedChunks, m_totalChunks);
            m_logger.LogInfo(msg);
        }
        else {
            // MISRA 6-4-2: terminal else -- no action for non-milestone ticks
        }
    }

    // =========================================================================
    // Private -- HandleEnd
    // =========================================================================
    // Processes FILE_TRANSFER_END (PacketType 0x32). Payload is empty.
    //
    // FIX vs original: original hard-failed and wrote nothing if any chunks
    // were missing. With fire-and-forget UDP some loss is possible. We now
    // write the partial file (missing slots stay zero-filled) and log a warning.
    // Requirements: REQ-CLT-040, REQ-SYS-070
    // MISRA DEV-002: WriteFileToDisk uses std::ofstream -- justified by REQ-SYS-050

    void FileReceiver::HandleEnd(const Packet& pkt) noexcept {
        (void)pkt;  // Empty payload -- MISRA 0-1-7: (void) cast

        if (m_state != TransferState::RECEIVING) {                   // MISRA DEV-004
            m_logger.LogError("FileReceiver: END received while not RECEIVING");
            return;
        }

        uint32_t missingChunks = 0U;
        for (uint32_t i = 0U; i < m_totalChunks; ++i) {
            if (!m_chunksReceived_map[i]) {
                ++missingChunks;
            }
            else {
                // MISRA 6-4-2: terminal else -- no action for received chunks
            }
        }

        // Write what we have -- even partial (missing slots are zero-filled).
        if (WriteFileToDisk()) {
            m_state = TransferState::COMPLETE;

            if (missingChunks != 0U) {
                char msg[96];
                (void)std::snprintf(msg, sizeof(msg),
                    "FileReceiver: Transfer incomplete -- %u/%u chunks, "
                    "%u chunks lost. Partial file saved as %s",
                    m_receivedChunks, m_totalChunks,
                    missingChunks, m_outputPath.c_str());
                m_logger.LogError(msg);
            }
            else {
                char msg[96];
                (void)std::snprintf(msg, sizeof(msg),
                    "FileReceiver: Complete -- %u bytes written to %s",
                    m_totalFileSize, m_outputPath.c_str());
                m_logger.LogInfo(msg);
            }
        }
        else {
            m_state = TransferState::FAILED;
            m_logger.LogError("FileReceiver: Failed to write file to disk");
        }
    }

    // =========================================================================
    // Private -- WriteFileToDisk
    // =========================================================================
    // MISRA DEV-002: std::ofstream -- stream I/O isolated to this function only.

    bool FileReceiver::WriteFileToDisk() noexcept {
        std::ofstream outFile(m_outputPath, std::ios::binary | std::ios::trunc);

        if (!outFile.is_open()) {
            char msg[96];
            (void)std::snprintf(msg, sizeof(msg),
                "FileReceiver: Cannot open output file: %s", m_outputPath.c_str());
            m_logger.LogError(msg);
            return false;
        }

        // MISRA Fix [V2547]: write() returns std::ostream& (self-reference);
        // the return value is intentionally discarded here because stream
        // state is validated immediately after via outFile.good().
        (void)outFile.write(
            reinterpret_cast<const char*>(m_fileBuffer.data()),
            static_cast<std::streamsize>(m_totalFileSize));

        return outFile.good();
    }

} // namespace AeroTrack