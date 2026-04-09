// =============================================================================
// PacketTypes.cpp — PacketTypeToString implementation
// REQ-PKT-030, REQ-LOG-010
// =============================================================================

#include "PacketTypes.h"

namespace AeroTrack {

    // MISRA Fix [V2520]: Restructured from return-per-case to assign+break pattern.
    // MISRA Rule 6-4-5 requires every switch clause to end with an unconditional
    // 'break' or 'throw'. 'return' does not satisfy the rule. Adding 'break' after
    // 'return' produces unreachable code (also a violation), so the correct fix is
    // to assign to a local variable per case and break, then return after the switch.
    const char* PacketTypeToString(PacketType type) noexcept
    {
        const char* result = "UNKNOWN";
        switch (type)
        {
        case PacketType::CONNECT:              result = "CONNECT";              break;
        case PacketType::CONNECT_ACK:          result = "CONNECT_ACK";          break;
        case PacketType::CONNECT_CONFIRM:      result = "CONNECT_CONFIRM";      break;
        case PacketType::DISCONNECT:           result = "DISCONNECT";           break;
        case PacketType::HEARTBEAT:            result = "HEARTBEAT";            break;
        case PacketType::ACK:                  result = "ACK";                  break;
        case PacketType::POSITION_REPORT:      result = "POSITION_REPORT";      break;
        case PacketType::TRACKING_ACK:         result = "TRACKING_ACK";         break;
        case PacketType::HANDOFF_INSTRUCT:     result = "HANDOFF_INSTRUCT";     break;
        case PacketType::HANDOFF_ACK:          result = "HANDOFF_ACK";          break;
        case PacketType::HANDOFF_COMPLETE:     result = "HANDOFF_COMPLETE";     break;
        case PacketType::HANDOFF_FAILED:       result = "HANDOFF_FAILED";       break;
        case PacketType::FILE_TRANSFER_START:  result = "FILE_TRANSFER_START";  break;
        case PacketType::FILE_TRANSFER_CHUNK:  result = "FILE_TRANSFER_CHUNK";  break;
        case PacketType::FILE_TRANSFER_END:    result = "FILE_TRANSFER_END";    break;
        case PacketType::ERROR:                result = "ERROR";                break;
        default:                               result = "UNKNOWN";              break;
        }
        return result;
    }

} // namespace AeroTrack