// REQ-CLT-040: Chunked file reassembly
// MISRA Deviation 2: ofstream in FileReceiver — approved per compliance audit

#include "FileReceiver.h"
#include "../AeroTrackShared/PacketTypes.h"
#include "../AeroTrackShared/Config.h"
#include <cstdio>
#include <cstring>
#include <fstream>

namespace AeroTrack {

    FileReceiver::FileReceiver(Logger& logger) noexcept
        : m_logger(logger)
        , m_state(TransferState::IDLE)
        , m_totalFileSize(0U)
        , m_totalChunks(0U)
        , m_receivedChunks(0U)
        , m_outputPath("radar_sector.jpg")
    {
    }

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

    TransferState FileReceiver::GetState() const noexcept {
        return m_state;
    }

    uint32_t FileReceiver::GetProgressPercent() const noexcept {
        if ((m_state != TransferState::RECEIVING) || (m_totalChunks == 0U)) {
            return 0U;
        }
        return (m_receivedChunks * 100U) / m_totalChunks;
    }

    const std::string& FileReceiver::GetOutputPath() const noexcept {
        return m_outputPath;
    }

    uint32_t FileReceiver::GetTotalChunks()    const noexcept { return m_totalChunks; }
    uint32_t FileReceiver::GetReceivedChunks() const noexcept { return m_receivedChunks; }

    void FileReceiver::Reset() noexcept {
        m_state = TransferState::IDLE;
        m_totalFileSize = 0U;
        m_totalChunks = 0U;
        m_receivedChunks = 0U;
        m_fileBuffer.clear();
        m_logger.LogInfo("FileReceiver: Reset to IDLE");
    }

    // ---------------------------------------------------------------------------
    // FILE_TRANSFER_START — payload: totalFileSize (4B BE) + totalChunks (4B BE)
    // ---------------------------------------------------------------------------
    void FileReceiver::HandleStart(const Packet& pkt) noexcept {
        if (m_state != TransferState::IDLE) {
            m_logger.LogError("FileReceiver: START received while not IDLE — resetting");
            Reset();
        }

        const std::vector<uint8_t>& payload = pkt.GetPayload();
        if (payload.size() < 8U) {
            m_logger.LogError("FileReceiver: FILE_TRANSFER_START payload too short");
            m_state = TransferState::FAILED;
            return;
        }

        m_totalFileSize =
            (static_cast<uint32_t>(payload[0U]) << 24U) |
            (static_cast<uint32_t>(payload[1U]) << 16U) |
            (static_cast<uint32_t>(payload[2U]) << 8U) |
            static_cast<uint32_t>(payload[3U]);

        m_totalChunks =
            (static_cast<uint32_t>(payload[4U]) << 24U) |
            (static_cast<uint32_t>(payload[5U]) << 16U) |
            (static_cast<uint32_t>(payload[6U]) << 8U) |
            static_cast<uint32_t>(payload[7U]);

        if ((m_totalFileSize == 0U) || (m_totalChunks == 0U)) {
            m_logger.LogError("FileReceiver: Invalid START — zero file size or chunk count");
            m_state = TransferState::FAILED;
            return;
        }

        // Pre-allocate full buffer — MISRA Deviation 1: vector RAII
        m_fileBuffer.resize(m_totalFileSize, 0U);
        m_receivedChunks = 0U;
        m_state = TransferState::RECEIVING;

        char msg[96];
        std::snprintf(msg, sizeof(msg),
            "FileReceiver: Transfer started — %u bytes, %u chunks",
            m_totalFileSize, m_totalChunks);
        m_logger.LogInfo(msg);
    }

    // ---------------------------------------------------------------------------
    // FILE_TRANSFER_CHUNK — payload: chunkIndex (4B BE) + chunkData (up to 1024B)
    // ---------------------------------------------------------------------------
    void FileReceiver::HandleChunk(const Packet& pkt) noexcept {
        if (m_state != TransferState::RECEIVING) {
            m_logger.LogError("FileReceiver: CHUNK received while not RECEIVING");
            return;
        }

        const std::vector<uint8_t>& payload = pkt.GetPayload();
        if (payload.size() < 5U) {
            m_logger.LogError("FileReceiver: FILE_TRANSFER_CHUNK payload too short");
            return;
        }

        uint32_t chunkIndex =
            (static_cast<uint32_t>(payload[0U]) << 24U) |
            (static_cast<uint32_t>(payload[1U]) << 16U) |
            (static_cast<uint32_t>(payload[2U]) << 8U) |
            static_cast<uint32_t>(payload[3U]);

        if (chunkIndex >= m_totalChunks) {
            char msg[64];
            std::snprintf(msg, sizeof(msg),
                "FileReceiver: Chunk index %u out of range (total %u)",
                chunkIndex, m_totalChunks);
            m_logger.LogError(msg);
            return;
        }

        const uint8_t* chunkData = payload.data() + 4U;
        size_t         chunkDataSize = payload.size() - 4U;
        size_t         writeOffset = static_cast<size_t>(chunkIndex) *
            static_cast<size_t>(FILE_CHUNK_SIZE);

        if ((writeOffset + chunkDataSize) > m_fileBuffer.size()) {
            m_logger.LogError("FileReceiver: Chunk would overflow file buffer");
            m_state = TransferState::FAILED;
            return;
        }

        std::memcpy(m_fileBuffer.data() + writeOffset, chunkData, chunkDataSize);
        ++m_receivedChunks;

        // Log every 10% progress
        uint32_t progress = GetProgressPercent();
        if ((progress % 10U) == 0U) {
            char msg[64];
            std::snprintf(msg, sizeof(msg),
                "FileReceiver: Progress %u%% (%u/%u chunks)",
                progress, m_receivedChunks, m_totalChunks);
            m_logger.LogInfo(msg);
        }
    }

    // ---------------------------------------------------------------------------
    // FILE_TRANSFER_END — verify all chunks received, write to disk
    // ---------------------------------------------------------------------------
    void FileReceiver::HandleEnd(const Packet& pkt) noexcept {
        (void)pkt;

        if (m_state != TransferState::RECEIVING) {
            m_logger.LogError("FileReceiver: END received while not RECEIVING");
            return;
        }

        if (m_receivedChunks != m_totalChunks) {
            char msg[96];
            std::snprintf(msg, sizeof(msg),
                "FileReceiver: END received but only %u/%u chunks received",
                m_receivedChunks, m_totalChunks);
            m_logger.LogError(msg);
            m_state = TransferState::FAILED;
            return;
        }

        if (WriteFileToDisk()) {
            m_state = TransferState::COMPLETE;
            char msg[96];
            std::snprintf(msg, sizeof(msg),
                "FileReceiver: Complete — %u bytes written to %s",
                m_totalFileSize, m_outputPath.c_str());
            m_logger.LogInfo(msg);
        }
        else {
            m_state = TransferState::FAILED;
            m_logger.LogError("FileReceiver: Failed to write file to disk");
        }
    }

    // ---------------------------------------------------------------------------
    // Write assembled buffer to disk
    // MISRA Deviation 2: ofstream — approved for file I/O modules
    // ---------------------------------------------------------------------------
    bool FileReceiver::WriteFileToDisk() noexcept {
        std::ofstream outFile(m_outputPath, std::ios::binary | std::ios::trunc);
        if (!outFile.is_open()) {
            char msg[96];
            std::snprintf(msg, sizeof(msg),
                "FileReceiver: Cannot open output file: %s", m_outputPath.c_str());
            m_logger.LogError(msg);
            return false;
        }
        outFile.write(
            reinterpret_cast<const char*>(m_fileBuffer.data()),
            static_cast<std::streamsize>(m_totalFileSize));
        return outFile.good();
    }

} // namespace AeroTrack