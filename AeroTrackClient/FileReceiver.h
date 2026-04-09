#pragma once
// REQ-CLT-040: Reassemble chunked FILE_TRANSFER_* packets into a JPEG
// REQ-SYS-070: 1MB+ file transfer, server -> client
// REQ-CLT-060: All RX events logged
//
// MISRA Deviations (see AeroTrack_Standards_Compliance_Audit.md):
//   DEV-001: std::vector dynamic allocation -- justified by REQ-SYS-030
//   DEV-002: std::ofstream in WriteFileToDisk -- justified by REQ-SYS-050

#include "../AeroTrackShared/Packet.h"
#include "../AeroTrackShared/Logger.h"
#include <cstdint>
#include <string>
#include <vector>

namespace AeroTrack {

    enum class TransferState : uint8_t {
        IDLE = 0U,
        RECEIVING = 1U,
        COMPLETE = 2U,
        FAILED = 3U
    };

    class FileReceiver {
    public:
        // flightId -- used to name the output file: received_sector_<flightId>.jpg
        // REQ-CLT-040, REQ-SYS-070
        explicit FileReceiver(Logger& logger, uint32_t flightId) noexcept;

        // REQ-CLT-040: Route FILE_TRANSFER_START / CHUNK / END to the correct handler
        void HandlePacket(const Packet& pkt) noexcept;

        TransferState      GetState()           const noexcept;
        uint32_t           GetProgressPercent() const noexcept;
        const std::string& GetOutputPath()      const noexcept;
        uint32_t           GetTotalChunks()     const noexcept;
        uint32_t           GetReceivedChunks()  const noexcept;
        void               Reset()                    noexcept;

    private:
        Logger& m_logger;
        uint32_t  m_flightId;           // Stored for output filename generation

        TransferState        m_state;
        uint32_t             m_totalFileSize;
        uint32_t             m_totalChunks;
        uint32_t             m_receivedChunks;
        std::string          m_outputPath;

        // DEV-001: std::vector RAII -- dynamic allocation justified by REQ-SYS-030
        std::vector<uint8_t> m_fileBuffer;         // Pre-allocated to totalFileSize bytes
        std::vector<bool>    m_chunksReceived_map; // Per-index arrival tracking -- out-of-order safe,
        // also prevents double-counting duplicate chunks

        void HandleStart(const Packet& pkt) noexcept;
        void HandleChunk(const Packet& pkt) noexcept;
        void HandleEnd(const Packet& pkt)   noexcept;
        bool WriteFileToDisk()              noexcept;

        // DecodeBigEndianU32 -- reads 4 bytes at data[offset..offset+3] as a big-endian uint32.
        // Joel's server v2 encodes ALL uint32 payload fields with explicit BE shift operators.
        // MISRA 5-2-4: only static_cast used -- no C-style casts.
        static uint32_t DecodeBigEndianU32(const std::vector<uint8_t>& data,
            size_t                        offset) noexcept;
    };

} // namespace AeroTrack