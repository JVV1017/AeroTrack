#pragma once
// REQ-CLT-040: Reassemble chunked FILE_TRANSFER_* packets into a JPEG

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
        explicit FileReceiver(Logger& logger) noexcept;

        // REQ-CLT-040: Route FILE_TRANSFER_START / CHUNK / END to correct handler
        void HandlePacket(const Packet& pkt) noexcept;

        TransferState      GetState()           const noexcept;
        uint32_t           GetProgressPercent() const noexcept;
        const std::string& GetOutputPath()      const noexcept;
        uint32_t           GetTotalChunks()     const noexcept;
        uint32_t           GetReceivedChunks()  const noexcept;
        void               Reset()                    noexcept;

    private:
        Logger& m_logger;

        TransferState        m_state;
        uint32_t             m_totalFileSize;
        uint32_t             m_totalChunks;
        uint32_t             m_receivedChunks;
        std::string          m_outputPath;

        // MISRA Deviation 1: vector RAII per REQ-SYS-030
        std::vector<uint8_t> m_fileBuffer;

        void HandleStart(const Packet& pkt) noexcept;
        void HandleChunk(const Packet& pkt) noexcept;
        void HandleEnd(const Packet& pkt) noexcept;
        bool WriteFileToDisk()              noexcept;
    };

} // namespace AeroTrack