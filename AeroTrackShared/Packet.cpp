// =============================================================================
// Packet.cpp — Packet serialization, CRC-16, and timestamp implementation
//
// Requirements addressed:
//   REQ-PKT-010  Serialize / Deserialize with 27-byte header
//   REQ-PKT-020  std::vector payload (MISRA Deviation 1)
//   REQ-PKT-050  CRC-16/IBM computation and validation
//   REQ-PKT-060  ValidateChecksum() — callers discard on false
// =============================================================================

#include "Packet.h"
#include <cstring>
#include <chrono>
#include <stdexcept>

namespace AeroTrack {

    // Byte offset of the checksum field within PacketHeader.
    // Derived from the packed layout: 1+4+4+8+4+4 = 25.
    // Using offsetof() is safer than a magic number — verified by static_assert
    // on sizeof(PacketHeader) == 27.
    static constexpr uint32_t CHECKSUM_OFFSET =
        static_cast<uint32_t>(offsetof(PacketHeader, checksum));

    static_assert(CHECKSUM_OFFSET == 25U,
        "Checksum field must be at offset 25 in PacketHeader");


    // =========================================================================
    // Constructors
    // =========================================================================

    Packet::Packet()
    {
        // MISRA Fix [V2547]: memset returns void* to the destination — always
        // the same pointer passed in. No meaningful result; discard is intentional.
        (void)std::memset(&m_header, 0, sizeof(PacketHeader));
    }

    Packet::Packet(PacketType type, uint32_t flightId)
    {
        // MISRA Fix [V2547]: same rationale as default constructor above.
        (void)std::memset(&m_header, 0, sizeof(PacketHeader));
        m_header.packet_type = static_cast<uint8_t>(type);
        m_header.flight_id = flightId;
        m_header.timestamp = CurrentTimestampMs();
    }


    // =========================================================================
    // Accessors
    // =========================================================================

    PacketType Packet::GetType() const noexcept
    {
        return static_cast<PacketType>(m_header.packet_type);
    }

    uint32_t Packet::GetSequenceNumber() const noexcept { return m_header.sequence_number; }
    uint32_t Packet::GetAckNumber()      const noexcept { return m_header.ack_number; }
    uint64_t Packet::GetTimestamp()      const noexcept { return m_header.timestamp; }
    uint32_t Packet::GetFlightId()       const noexcept { return m_header.flight_id; }
    uint32_t Packet::GetPayloadLength()  const noexcept { return m_header.payload_length; }
    uint16_t Packet::GetChecksum()       const noexcept { return m_header.checksum; }

    const std::vector<uint8_t>& Packet::GetPayload() const noexcept { return m_payload; }


    // =========================================================================
    // Mutators
    // =========================================================================

    void Packet::SetType(PacketType type) noexcept
    {
        m_header.packet_type = static_cast<uint8_t>(type);
    }

    void Packet::SetSequenceNumber(uint32_t seq) noexcept { m_header.sequence_number = seq; }
    void Packet::SetAckNumber(uint32_t ack)      noexcept { m_header.ack_number = ack; }
    void Packet::SetTimestamp(uint64_t ts)        noexcept { m_header.timestamp = ts; }
    void Packet::SetFlightId(uint32_t id)         noexcept { m_header.flight_id = id; }

    void Packet::SetPayload(const std::vector<uint8_t>& payload)
    {
        m_payload = payload;
        m_header.payload_length = static_cast<uint32_t>(m_payload.size());
    }

    void Packet::SetPayload(const uint8_t* data, uint32_t length)
    {
        if ((data != nullptr) && (length > 0U))
        {
            // MISRA Fix [V2563]: replaced data + length (pointer arithmetic)
            // with an index-based loop (subscript notation only, MISRA 5-0-15).
            m_payload.resize(static_cast<size_t>(length));
            for (uint32_t i = 0U; i < length; ++i)
            {
                m_payload[i] = data[i];
            }
            m_header.payload_length = length;
        }
    }


    // =========================================================================
    // CRC-16/IBM (reflected polynomial 0xA001)
    //
    // Bit-by-bit computation — no lookup table required.
    // Polynomial: 0x8005 reflected = 0xA001
    // Initial value: 0x0000
    //
    // REQ-PKT-050
    // =========================================================================
    // MISRA Note [V2563]: data[i] inside this function uses subscript notation
    // (the MISRA-permitted form of pointer arithmetic, Rule 5-0-15). PVS-Studio
    // flags it because the parameter is typed as const uint8_t* rather than an
    // array. The public API signature cannot be changed to std::vector without
    // breaking all raw-buffer callers (e.g., Deserialize receive buffer).
    // All internal usage passes buffer.data() — a contiguous array allocation.
    uint16_t Packet::ComputeCRC16(const uint8_t* data, uint32_t length) noexcept
    {
        uint16_t crc = 0x0000U;

        for (uint32_t i = 0U; i < length; ++i)
        {
            crc ^= static_cast<uint16_t>(data[i]);

            for (uint8_t bit = 0U; bit < 8U; ++bit)
            {
                if ((crc & 0x0001U) != 0U)
                {
                    crc = static_cast<uint16_t>((crc >> 1U) ^ 0xA001U);
                }
                else
                {
                    crc = static_cast<uint16_t>(crc >> 1U);
                }
            }
        }

        return crc;
    }


    // =========================================================================
    // Serialize
    //
    // Wire layout:  [PacketHeader — 27 bytes][Payload — 0..N bytes]
    //
    // Checksum computation procedure (REQ-PKT-050):
    //   1. Build full buffer with checksum field = 0
    //   2. Compute CRC-16 over the entire buffer
    //   3. Write CRC-16 back at CHECKSUM_OFFSET (byte 25)
    // =========================================================================
    std::vector<uint8_t> Packet::Serialize() const
    {
        const uint32_t headerSize = static_cast<uint32_t>(sizeof(PacketHeader));
        const uint32_t payloadSize = static_cast<uint32_t>(m_payload.size());
        const uint32_t totalSize = headerSize + payloadSize;

        std::vector<uint8_t> buffer(totalSize, 0U);

        // Copy header into buffer, with checksum zeroed for CRC input
        PacketHeader tempHeader = m_header;
        tempHeader.payload_length = payloadSize;   // ensure header is consistent
        tempHeader.checksum = 0U;

        // MISRA Fix [V2547]: memcpy return values explicitly discarded throughout
        // Serialize() and ValidateChecksum(). Return value is the destination
        // pointer — always the same as the first argument. Discard is intentional.
        // MISRA Fix [V2563]: buffer.data() + offset replaced with &buffer[offset]
        // (subscript notation) per MISRA Rule 5-0-15.
        (void)std::memcpy(buffer.data(), &tempHeader, headerSize);

        // Append payload bytes
        if (payloadSize > 0U)
        {
            (void)std::memcpy(&buffer[headerSize], m_payload.data(), payloadSize);
        }

        // Compute CRC-16 over the full buffer (checksum = 0 at this point)
        const uint16_t crc = ComputeCRC16(buffer.data(), totalSize);

        // Write CRC back using memcpy to avoid any alignment assumption
        (void)std::memcpy(&buffer[CHECKSUM_OFFSET], &crc, sizeof(uint16_t));

        return buffer;
    }


    // =========================================================================
    // Deserialize
    //
    // Returns a Packet with type ERROR if:
    //   - buffer pointer is null
    //   - buffer is shorter than the header
    //   - header.payload_length would read past the end of the buffer
    //
    // Checksum is NOT validated here — caller must call ValidateChecksum()
    // immediately after, per REQ-PKT-050/060.
    // =========================================================================
    Packet Packet::Deserialize(const uint8_t* buffer, uint32_t length)
    {
        const uint32_t headerSize = static_cast<uint32_t>(sizeof(PacketHeader));

        if ((buffer == nullptr) || (length < headerSize))
        {
            return Packet(PacketType::ERROR, 0U);
        }

        Packet packet;
        // MISRA Fix [V2547]: memcpy return value explicitly discarded.
        (void)std::memcpy(&packet.m_header, buffer, headerSize);

        // Guard against truncated or malformed payload length
        const uint32_t expectedTotal = headerSize + packet.m_header.payload_length;
        if (length < expectedTotal)
        {
            return Packet(PacketType::ERROR, 0U);
        }

        // Copy payload if present
        if (packet.m_header.payload_length > 0U)
        {
            packet.m_payload.resize(packet.m_header.payload_length);
            // MISRA Fix [V2547+V2563]: void cast + &buffer[headerSize]
            // replaces buffer + headerSize (pointer arithmetic).
            (void)std::memcpy(packet.m_payload.data(),
                &buffer[headerSize],
                packet.m_header.payload_length);
        }

        return packet;
    }


    // =========================================================================
    // ValidateChecksum — REQ-PKT-050, REQ-PKT-060
    //
    // Re-creates the buffer with checksum = 0, recomputes CRC-16, and compares
    // against the stored value.  Returns false if they differ (caller discards).
    // =========================================================================
    bool Packet::ValidateChecksum() const
    {
        const uint16_t stored = m_header.checksum;
        const uint32_t headerSize = static_cast<uint32_t>(sizeof(PacketHeader));
        const uint32_t payloadSize = static_cast<uint32_t>(m_payload.size());
        const uint32_t totalSize = headerSize + payloadSize;

        std::vector<uint8_t> buffer(totalSize, 0U);

        PacketHeader tempHeader = m_header;
        tempHeader.checksum = 0U;
        // MISRA Fix [V2547+V2563]: void casts + &buffer[offset] subscript
        // notation — same rationale as Serialize() above.
        (void)std::memcpy(buffer.data(), &tempHeader, headerSize);

        if (payloadSize > 0U)
        {
            (void)std::memcpy(&buffer[headerSize], m_payload.data(), payloadSize);
        }

        const uint16_t computed = ComputeCRC16(buffer.data(), totalSize);
        return (computed == stored);
    }


    // =========================================================================
    // CurrentTimestampMs
    //
    // Returns milliseconds since Unix epoch using the system clock.
    // Used to timestamp all outgoing packets.  REQ-LOG-020.
    // =========================================================================
    uint64_t Packet::CurrentTimestampMs() noexcept
    {
        const auto now = std::chrono::system_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch());
        return static_cast<uint64_t>(ms.count());
    }


    // =========================================================================
    // TypeString — REQ-LOG-010
    // =========================================================================
    std::string Packet::TypeString() const
    {
        return std::string(PacketTypeToString(GetType()));
    }

} // namespace AeroTrack