// =============================================================================
// FileTransfer.h — Chunked file transfer sender for AeroTrack Ground Control
// =============================================================================
// Requirements: REQ-SVR-050, REQ-SYS-070
// Standard:     MISRA C++ — no Winsock, no raw new/delete, U suffix,
//               fixed-width types, explicit bool conditions
//               MISRA Deviation 2: std::ifstream used for file reading
//
// Design:
//   FileTransfer loads a JPEG file into memory, then produces a sequence
//   of Packet objects (START, N × CHUNK, END) that the Server
//   main loop sends via RUDP. This module performs NO network I/O.
//
//   Architecture spec:
//     1. FILE_TRANSFER_START  — payload: total_file_size (uint32) + total_chunks (uint32)
//     2. FILE_TRANSFER_CHUNK  — payload: chunk_index (uint32) + chunk_data (up to 1024 bytes)
//     3. FILE_TRANSFER_END    — payload: empty (signals completion)
//     Each chunk is ACKed individually by the RUDP layer.
// =============================================================================
#pragma once

#include "Packet.h"
#include "PacketTypes.h"
#include "Config.h"

#include <cstdint>
#include <string>
#include <vector>

namespace AeroTrack {

    // ---------------------------------------------------------------------------
    // FileTransferStatus — tracks the state of a file transfer
    // ---------------------------------------------------------------------------
    enum class FileTransferStatus : uint8_t {
        NOT_STARTED = 0U,
        FILE_LOADED = 1U,
        IN_PROGRESS = 2U,
        COMPLETED = 3U,
        FAILED = 4U
    };

    // ---------------------------------------------------------------------------
    // FileTransfer — loads file, produces chunked packets for RUDP transmission
    // ---------------------------------------------------------------------------
    class FileTransfer {
    public:
        FileTransfer();

        // -----------------------------------------------------------------------
        // Load a file from disk into memory
        // -----------------------------------------------------------------------
        // Returns true if file was loaded successfully and is >= 1 byte.
        // Validates file size is at least 1 MB for REQ-SYS-070 compliance
        // (logs a warning but does not reject smaller files — allows testing
        // with smaller files while production uses 1MB+ JPEG).
        //
        // MISRA Deviation 2: Uses std::ifstream for file reading.
        // -----------------------------------------------------------------------
        bool LoadFile(const std::string& filePath);

        // -----------------------------------------------------------------------
        // Prepare the transfer for a specific flight
        // -----------------------------------------------------------------------
        // Must be called after LoadFile(). Sets the flight_id for packet headers
        // and calculates chunk count.
        // -----------------------------------------------------------------------
        bool PrepareTransfer(uint32_t flightId);

        // -----------------------------------------------------------------------
        // Build the FILE_TRANSFER_START packet
        // -----------------------------------------------------------------------
        // Payload: total_file_size (uint32) + total_chunks (uint32) = 8 bytes
        // -----------------------------------------------------------------------
        Packet BuildStartPacket() const;

        // -----------------------------------------------------------------------
        // Build a FILE_TRANSFER_CHUNK packet for the given chunk index
        // -----------------------------------------------------------------------
        // Payload: chunk_index (uint32) + chunk_data (up to FILE_CHUNK_SIZE bytes)
        // Returns empty packet if index is out of range.
        // -----------------------------------------------------------------------
        Packet BuildChunkPacket(uint32_t chunkIndex) const;

        // -----------------------------------------------------------------------
        // Build the FILE_TRANSFER_END packet
        // -----------------------------------------------------------------------
        // Payload: empty (0 bytes)
        // -----------------------------------------------------------------------
        Packet BuildEndPacket() const;

        // -----------------------------------------------------------------------
        // Accessors
        // -----------------------------------------------------------------------
        FileTransferStatus GetStatus() const;
        uint32_t GetTotalChunks() const;
        uint32_t GetFileSize() const;
        uint32_t GetFlightId() const;

        // Returns true if file is loaded and >= 1 MB (REQ-SYS-070 compliance check)
        bool MeetsMinimumSizeRequirement() const;

    private:
        std::vector<uint8_t> m_fileData;      // Loaded file content (RAII managed)
        uint32_t             m_flightId;       // Target aircraft
        uint32_t             m_totalChunks;    // Calculated from file size / chunk size
        FileTransferStatus   m_status;
        std::string          m_filePath;       // For logging/diagnostics

        // Minimum file size for REQ-SYS-070 compliance (1 MB = 1,048,576 bytes)
        static constexpr uint32_t MIN_FILE_SIZE = 1048576U;
    };

} // namespace AeroTrack