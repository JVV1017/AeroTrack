#pragma once
// =============================================================================
// Packet.h — AeroTrack packet definition, serialization, and CRC-16 validation
//
// Requirements addressed:
//   REQ-PKT-010  Fixed 27-byte binary header
//   REQ-PKT-020  Dynamic payload via std::vector (MISRA Deviation 1)
//   REQ-PKT-040  POSITION_REPORT payload layout (24 bytes)
//   REQ-PKT-050  CRC-16 checksum computed and validated on every packet
//   REQ-PKT-060  Invalid checksum packets discarded (caller responsibility)
// =============================================================================

#include "PacketTypes.h"
#include <cstdint>
#include <vector>
#include <string>

namespace AeroTrack {

    // =========================================================================
    // PacketHeader — fixed 27-byte wire layout (REQ-PKT-010)
    //
    // #pragma pack(push, 1): Instructs the compiler to suppress padding bytes
    // between struct members, ensuring the in-memory layout exactly matches
    // the over-the-wire binary format.
    // MISRA C++:2008 16-2-1 — #pragma use documented here and in deviation log.
    //
    // Field layout (all offsets are from byte 0):
    //   offset  0  |  1 byte  |  packet_type
    //   offset  1  |  4 bytes |  sequence_number
    //   offset  5  |  4 bytes |  ack_number
    //   offset  9  |  8 bytes |  timestamp
    //   offset 17  |  4 bytes |  flight_id
    //   offset 21  |  4 bytes |  payload_length
    //   offset 25  |  2 bytes |  checksum
    //                          = 27 bytes total
    // =========================================================================
#pragma pack(push, 1)
    struct PacketHeader
    {
        uint8_t  packet_type;      // PacketType enum value cast to uint8_t
        uint32_t sequence_number;  // RUDP sequence number (starts at 1)
        uint32_t ack_number;       // ACK for the sequence number being acknowledged
        uint64_t timestamp;        // Milliseconds since Unix epoch (REQ-LOG-020)
        uint32_t flight_id;        // Numeric aircraft identifier
        uint32_t payload_length;   // Number of payload bytes following this header
        uint16_t checksum;         // CRC-16/IBM over entire packet (checksum field = 0)
    };
#pragma pack(pop)

    // Compile-time size guard — build fails immediately if struct layout is wrong
    static_assert(sizeof(PacketHeader) == 27U,
        "PacketHeader must be exactly 27 bytes (REQ-PKT-010)");


    // =========================================================================
    // PositionPayload — POSITION_REPORT payload layout (REQ-PKT-040)
    //
    // Packed to match the wire format.  Total = 24 bytes.
    //   offset  0  |  8 bytes |  latitude   (degrees, WGS-84)
    //   offset  8  |  8 bytes |  longitude  (degrees, WGS-84)
    //   offset 16  |  4 bytes |  altitude_ft
    //   offset 20  |  2 bytes |  speed_kts
    //   offset 22  |  2 bytes |  heading_deg (0 – 359)
    // =========================================================================
#pragma pack(push, 1)
    struct PositionPayload
    {
        double   latitude;      // degrees
        double   longitude;     // degrees
        uint32_t altitude_ft;   // feet MSL
        uint16_t speed_kts;     // knots
        uint16_t heading_deg;   // degrees, 0–359
    };
#pragma pack(pop)

    static_assert(sizeof(PositionPayload) == 24U,
        "PositionPayload must be exactly 24 bytes (REQ-PKT-040)");


    // =========================================================================
    // Packet — header + variable-length payload
    //
    // MISRA Deviation 1: payload stored in std::vector<uint8_t> (RAII-managed
    // dynamic memory) per REQ-PKT-020.  No raw new/delete anywhere.
    // =========================================================================
    class Packet
    {
    public:
        // Default-constructed packet has all header fields zero
        Packet();

        // Convenience constructor — sets type, flight_id, and current timestamp
        Packet(PacketType type, uint32_t flightId);

        // ---- Accessors -------------------------------------------------------
        PacketType  GetType()           const noexcept;
        uint32_t    GetSequenceNumber() const noexcept;
        uint32_t    GetAckNumber()      const noexcept;
        uint64_t    GetTimestamp()      const noexcept;
        uint32_t    GetFlightId()       const noexcept;
        uint32_t    GetPayloadLength()  const noexcept;  // mirrors header field
        uint16_t    GetChecksum()       const noexcept;

        // Returns const reference to payload bytes (no copy)
        const std::vector<uint8_t>& GetPayload() const noexcept;

        // ---- Mutators --------------------------------------------------------
        void SetType(PacketType type)                          noexcept;
        void SetSequenceNumber(uint32_t seq)                   noexcept;
        void SetAckNumber(uint32_t ack)                        noexcept;
        void SetTimestamp(uint64_t ts)                         noexcept;
        void SetFlightId(uint32_t id)                          noexcept;

        // Copies data into payload and updates header.payload_length
        void SetPayload(const std::vector<uint8_t>& payload);
        void SetPayload(const uint8_t* data, uint32_t length);

        // ---- Serialization ---------------------------------------------------
        // Produces a byte buffer ready to pass to sendto().
        // CRC-16 is computed over the entire buffer (checksum field = 0 during
        // computation, then written back).  REQ-PKT-010, REQ-PKT-050.
        std::vector<uint8_t> Serialize() const;

        // Deserializes from a raw UDP receive buffer.
        // Returns a Packet with type ERROR if the buffer is too short or the
        // declared payload_length exceeds the available bytes.
        // Caller must then call ValidateChecksum() separately.  REQ-PKT-050.
        static Packet Deserialize(const uint8_t* buffer, uint32_t length);

        // ---- CRC-16/IBM (polynomial 0xA001, reflected) -----------------------
        // Public so callers can reuse it (e.g., file integrity checks).
        // REQ-PKT-050
        static uint16_t ComputeCRC16(const uint8_t* data, uint32_t length) noexcept;

        // Returns true if the stored checksum matches a freshly-computed value.
        // REQ-PKT-050, REQ-PKT-060
        bool ValidateChecksum() const;

        // ---- Utilities -------------------------------------------------------
        // Returns current wall-clock time in milliseconds since epoch.
        // Used by RUDP to timestamp outgoing packets.  REQ-LOG-020.
        static uint64_t CurrentTimestampMs() noexcept;

        // Returns PacketTypeToString(GetType()) as std::string.
        // Used by Logger.  REQ-LOG-010.
        std::string TypeString() const;

    private:
        PacketHeader         m_header;
        std::vector<uint8_t> m_payload;  // MISRA Deviation 1
    };

} // namespace AeroTrack
