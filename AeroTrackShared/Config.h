#pragma once
// =============================================================================
// Config.h — AeroTrack system-wide configuration constants
//
// All integer literals use the U suffix per MISRA C++ unsigned literal rule.
// This header is safe to include in all modules.
// =============================================================================

#include <cstdint>

namespace AeroTrack {

    // ---- Network ----------------------------------------------------------------
    constexpr const char* SERVER_IP   = "127.0.0.1";
    constexpr uint16_t    SERVER_PORT = 27015U;

    // ---- RUDP reliability (REQ-COM-040) -----------------------------------------
    constexpr uint32_t RUDP_TIMEOUT_MS  = 500U;   // ms before retransmit attempt
    constexpr uint32_t RUDP_MAX_RETRIES = 3U;     // max retransmit attempts

    // ---- Heartbeat and contact loss (REQ-STM-040) --------------------------------
    constexpr uint32_t HEARTBEAT_INTERVAL_MS    = 3000U;   // client sends every 3 s
    constexpr uint32_t LOST_CONTACT_TIMEOUT_MS  = 10000U;  // server declares lost at 10 s

    // ---- Position reporting (REQ-CLT-040) ----------------------------------------
    constexpr uint32_t POSITION_REPORT_INTERVAL_MS = 2000U; // client reports every 2 s

    // ---- File transfer (REQ-SYS-070) ---------------------------------------------
    constexpr uint32_t FILE_CHUNK_SIZE = 1024U;  // bytes per FILE_TRANSFER_CHUNK payload

    // ---- Handoff (REQ-STM-030) ---------------------------------------------------
    constexpr uint32_t HANDOFF_TIMEOUT_MS = 5000U;  // server waits 5 s for HANDOFF_ACK

    // ---- Internal socket receive timeout ----------------------------------------
    constexpr uint32_t SOCKET_RECV_TIMEOUT_MS = 1000U;  // default blocking receive timeout

} // namespace AeroTrack
