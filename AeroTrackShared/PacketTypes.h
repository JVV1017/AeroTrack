#pragma once
// =============================================================================
// PacketTypes.h — AeroTrack protocol packet type enumeration
//
// REQ-PKT-030: Defines all packet_type field values.
// Using enum class (MISRA C++ 7-2-1): prevents implicit integer conversion
// and avoids name-space pollution.
// =============================================================================

#include <cstdint>

namespace AeroTrack {

    enum class PacketType : uint8_t
    {
        // ---- Connection handshake (3-step) -----------------------------------
        CONNECT          = 0x01U,   // Client → Server: initiate connection
        CONNECT_ACK      = 0x02U,   // Server → Client: accept + assign sector
        CONNECT_CONFIRM  = 0x03U,   // Client → Server: confirm sector assignment
        DISCONNECT       = 0x04U,   // Either direction: graceful teardown

        // ---- Keep-alive and acknowledgement ----------------------------------
        HEARTBEAT        = 0x05U,   // Client → Server: periodic liveness signal
        ACK              = 0x06U,   // Either direction: RUDP acknowledgement

        // ---- Position and tracking -------------------------------------------
        POSITION_REPORT  = 0x10U,   // Client → Server: lat/lon/alt/speed/heading
        TRACKING_ACK     = 0x11U,   // Server → Client: position report accepted

        // ---- Handoff sequence ------------------------------------------------
        HANDOFF_INSTRUCT  = 0x20U,  // Server → Client: sector boundary detected
        HANDOFF_ACK       = 0x21U,  // Client → Server: handoff instruction acknowledged
        HANDOFF_COMPLETE  = 0x22U,  // Server → Client: handoff confirmed
        HANDOFF_FAILED    = 0x23U,  // Server → Client: handoff timed out

        // ---- Large file transfer (REQ-SYS-070) --------------------------------
        FILE_TRANSFER_START = 0x30U, // Server → Client: announce file size + chunk count
        FILE_TRANSFER_CHUNK = 0x31U, // Server → Client: one 1024-byte chunk
        FILE_TRANSFER_END   = 0x32U, // Server → Client: transfer complete

        // ---- Error -----------------------------------------------------------
        ERROR            = 0xFFU    // Any direction: protocol or deserialization error
    };

    // Returns a human-readable C-string for use in log entries (REQ-LOG-010).
    // Returns "UNKNOWN" for values not listed above.
    // noexcept: safe to call from any context, including destructors.
    const char* PacketTypeToString(PacketType type) noexcept;

} // namespace AeroTrack
