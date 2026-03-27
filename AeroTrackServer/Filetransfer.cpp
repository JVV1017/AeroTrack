// =============================================================================
// FileTransfer.cpp — Chunked file transfer sender implementation
// =============================================================================
// Requirements: REQ-SVR-050, REQ-SYS-070
// Standard:     MISRA C++ compliant (see FileTransfer.h header comment)
//               MISRA Deviation 2: std::ifstream used here for file reading
// =============================================================================

// MISRA Deviation 2: Stream I/O required to read JPEG file from disk.
// Deviation documented in AeroTrack_Standards_Compliance_Audit.md.
#include <fstream>    // std::ifstream — MISRA Deviation 2

#include "FileTransfer.h"

#include <cstring>    // std::memcpy

namespace AeroTrack {

    // ---------------------------------------------------------------------------
    // Constructor
    // ---------------------------------------------------------------------------
    FileTransfer::FileTransfer()
        : m_fileData()
        , m_flightId(0U)
        , m_totalChunks(0U)
        , m_status(FileTransferStatus::NOT_STARTED)
        , m_filePath()
    {
    }

    // ---------------------------------------------------------------------------
    // LoadFile — read entire JPEG into memory
    // ---------------------------------------------------------------------------
    // MISRA Deviation 2: std::ifstream for file reading.
    // ---------------------------------------------------------------------------
    bool FileTransfer::LoadFile(const std::string& filePath)
    {
        m_filePath = filePath;
        m_fileData.clear();
        m_status = FileTransferStatus::NOT_STARTED;

        // Open file in binary mode
        std::ifstream file(filePath, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            m_status = FileTransferStatus::FAILED;
            return false;
        }

        // Get file size from the end position
        const std::streamsize fileSize = file.tellg();
        if (fileSize <= 0) {
            m_status = FileTransferStatus::FAILED;
            return false;
        }

        // Seek back to start
        file.seekg(0, std::ios::beg);

        // Allocate and read entire file (RAII via std::vector)
        m_fileData.resize(static_cast<size_t>(fileSize));
        if (!file.read(reinterpret_cast<char*>(m_fileData.data()),
            fileSize)) {
            // reinterpret_cast justified: reading raw bytes into uint8_t buffer
            // (Winsock-style cast exemption applies to binary I/O as well)
            m_fileData.clear();
            m_status = FileTransferStatus::FAILED;
            return false;
        }

        m_status = FileTransferStatus::FILE_LOADED;
        return true;
    }

    // ---------------------------------------------------------------------------
    // PrepareTransfer — set flight_id and calculate chunk count
    // ---------------------------------------------------------------------------
    bool FileTransfer::PrepareTransfer(uint32_t flightId)
    {
        if (m_status != FileTransferStatus::FILE_LOADED) {
            return false;  // File must be loaded first
        }

        m_flightId = flightId;

        // Calculate total chunks: ceiling division
        // MISRA: explicit casts, U suffix on literals
        const uint32_t dataSize = static_cast<uint32_t>(m_fileData.size());
        const uint32_t chunkSize = FILE_CHUNK_SIZE;

        m_totalChunks = (dataSize + chunkSize - 1U) / chunkSize;

        m_status = FileTransferStatus::IN_PROGRESS;
        return true;
    }

    // ---------------------------------------------------------------------------
    // BuildStartPacket — FILE_TRANSFER_START
    // ---------------------------------------------------------------------------
    // Payload layout (8 bytes):
    //   [0..3]  total_file_size  (uint32_t, little-endian)
    //   [4..7]  total_chunks     (uint32_t, little-endian)
    // ---------------------------------------------------------------------------
    Packet FileTransfer::BuildStartPacket() const
    {
        Packet pkt(PacketType::FILE_TRANSFER_START, m_flightId);

        // Build payload: total_file_size + total_chunks = 8 bytes
        const uint32_t fileSize = static_cast<uint32_t>(m_fileData.size());
        constexpr uint32_t PAYLOAD_SIZE = 8U;  // 4 + 4 bytes

        std::vector<uint8_t> payload(PAYLOAD_SIZE);

        // Copy total_file_size (bytes 0..3)
        std::memcpy(payload.data(), &fileSize, sizeof(uint32_t));

        // Copy total_chunks (bytes 4..7)
        std::memcpy(&payload[4U], &m_totalChunks, sizeof(uint32_t));

        pkt.SetPayload(payload);

        return pkt;
    }

    // ---------------------------------------------------------------------------
    // BuildChunkPacket — FILE_TRANSFER_CHUNK
    // ---------------------------------------------------------------------------
    // Payload layout (4 + up to 1024 bytes):
    //   [0..3]         chunk_index  (uint32_t, little-endian)
    //   [4..4+N-1]     chunk_data   (N bytes, where N <= FILE_CHUNK_SIZE)
    // ---------------------------------------------------------------------------
    Packet FileTransfer::BuildChunkPacket(uint32_t chunkIndex) const
    {
        // Bounds check
        if (chunkIndex >= m_totalChunks) {
            // Return error packet — caller should check before calling
            Packet errPkt(PacketType::ERROR, m_flightId);
            return errPkt;
        }

        Packet pkt(PacketType::FILE_TRANSFER_CHUNK, m_flightId);

        // Calculate the byte range for this chunk
        const uint32_t dataSize = static_cast<uint32_t>(m_fileData.size());
        const uint32_t startByte = chunkIndex * FILE_CHUNK_SIZE;
        uint32_t chunkBytes = FILE_CHUNK_SIZE;

        // Last chunk may be smaller
        if ((startByte + chunkBytes) > dataSize) {
            chunkBytes = dataSize - startByte;
        }

        // Payload = chunk_index (4 bytes) + chunk_data (chunkBytes)
        const uint32_t payloadSize = 4U + chunkBytes;
        std::vector<uint8_t> payload(payloadSize);

        // Copy chunk_index (bytes 0..3)
        std::memcpy(payload.data(), &chunkIndex, sizeof(uint32_t));

        // Copy chunk_data (bytes 4..4+chunkBytes-1)
        // Using indexed copy instead of pointer arithmetic for MISRA
        // static_cast<size_t> prevents Int-arith sub-expression overflow warning
        for (uint32_t i = 0U; i < chunkBytes; ++i) {
            payload[static_cast<size_t>(4U) + static_cast<size_t>(i)] =
                m_fileData[static_cast<size_t>(startByte) + static_cast<size_t>(i)];
        }

        pkt.SetPayload(payload);

        return pkt;
    }

    // ---------------------------------------------------------------------------
    // BuildEndPacket — FILE_TRANSFER_END
    // ---------------------------------------------------------------------------
    Packet FileTransfer::BuildEndPacket() const
    {
        Packet pkt(PacketType::FILE_TRANSFER_END, m_flightId);
        // Empty payload — no SetPayload call needed (default is empty)
        return pkt;
    }

    // ---------------------------------------------------------------------------
    // Accessors
    // ---------------------------------------------------------------------------
    FileTransferStatus FileTransfer::GetStatus() const
    {
        return m_status;
    }

    uint32_t FileTransfer::GetTotalChunks() const
    {
        return m_totalChunks;
    }

    uint32_t FileTransfer::GetFileSize() const
    {
        return static_cast<uint32_t>(m_fileData.size());
    }

    uint32_t FileTransfer::GetFlightId() const
    {
        return m_flightId;
    }

    bool FileTransfer::MeetsMinimumSizeRequirement() const
    {
        return (static_cast<uint32_t>(m_fileData.size()) >= MIN_FILE_SIZE);
    }

} // namespace AeroTrack