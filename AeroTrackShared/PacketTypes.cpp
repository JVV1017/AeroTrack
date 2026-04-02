// =============================================================================
// PacketTypes.cpp — PacketTypeToString implementation
// REQ-PKT-030, REQ-LOG-010
// =============================================================================

#include "PacketTypes.h"

namespace AeroTrack {

    const char* PacketTypeToString(PacketType type) noexcept
    {
        switch (type)
        {
            case PacketType::CONNECT:              return "CONNECT";
            case PacketType::CONNECT_ACK:          return "CONNECT_ACK";
            case PacketType::CONNECT_CONFIRM:      return "CONNECT_CONFIRM";
            case PacketType::DISCONNECT:           return "DISCONNECT";
            case PacketType::HEARTBEAT:            return "HEARTBEAT";
            case PacketType::ACK:                  return "ACK";
            case PacketType::POSITION_REPORT:      return "POSITION_REPORT";
            case PacketType::TRACKING_ACK:         return "TRACKING_ACK";
            case PacketType::HANDOFF_INSTRUCT:     return "HANDOFF_INSTRUCT";
            case PacketType::HANDOFF_ACK:          return "HANDOFF_ACK";
            case PacketType::HANDOFF_COMPLETE:     return "HANDOFF_COMPLETE";
            case PacketType::HANDOFF_FAILED:       return "HANDOFF_FAILED";
            case PacketType::FILE_TRANSFER_START:  return "FILE_TRANSFER_START";
            case PacketType::FILE_TRANSFER_CHUNK:  return "FILE_TRANSFER_CHUNK";
            case PacketType::FILE_TRANSFER_END:    return "FILE_TRANSFER_END";
            case PacketType::ERROR:                return "ERROR";
            default:                               return "UNKNOWN";
        }
    }

} // namespace AeroTrack
