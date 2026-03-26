#pragma once
// =============================================================================
// RUDP.h — Reliable UDP transport layer for AeroTrack
//
// Requirements addressed:
//   REQ-COM-010  UDP/IP base transport
//   REQ-COM-020  Incrementing uint32 sequence numbering on every outgoing packet
//   REQ-COM-030  ACK sent for each successfully received packet
//   REQ-COM-040  Retransmit if ACK not received within 500 ms, up to 3 retries
//   REQ-COM-050  Log all retransmission events (via injected Logger pointer)
//   REQ-COM-060  Report connection error after max retries exhausted
//
// MISRA Deviation 3:
//   All Winsock2 API calls are confined to RUDP.cpp.
//   winsock2.h, SOCKET, sockaddr_in, WSA functions — NOT present in this header.
//   The socket handle is stored as uintptr_t (same width as SOCKET on Win64).
//   Network addresses are abstracted via the Endpoint struct below.
// =============================================================================

#include "Packet.h"
#include <cstdint>
#include <string>

namespace AeroTrack {

    // Forward declaration — avoids circular include with Logger.h
    class Logger;

    // =========================================================================
    // Endpoint — platform-agnostic network address
    //
    // Prevents Winsock types (sockaddr_in, in_addr) from appearing outside
    // RUDP.cpp, satisfying MISRA Deviation 3.
    // =========================================================================
    struct Endpoint
    {
        std::string ip;
        uint16_t    port{ 0U };

        // Returns true if both ip and port are populated
        bool IsValid() const noexcept { return (port != 0U) && !ip.empty(); }
    };


    // =========================================================================
    // RUDP — Reliable UDP transport
    //
    // Typical server usage:
    //   RUDP rudp;
    //   rudp.Init();
    //   rudp.Bind(SERVER_PORT);
    //   rudp.SetLogger(&logger);
    //   // loop:
    //   rudp.Receive(packet, sender);
    //   rudp.SendReliable(responsePacket, flightId, sender);
    //
    // Typical client usage:
    //   RUDP rudp;
    //   rudp.Init();
    //   rudp.SetLogger(&logger);
    //   Endpoint server{ SERVER_IP, SERVER_PORT };
    //   rudp.SendReliable(connectPacket, flightId, server);
    //   rudp.Receive(ackPacket, sender);
    // =========================================================================
    class RUDP
    {
    public:
        RUDP();
        ~RUDP();

        // Non-copyable: owns a socket handle resource
        RUDP(const RUDP&)            = delete;
        RUDP& operator=(const RUDP&) = delete;

        // Attach an optional logger for retransmission and error events.
        // Pointer is non-owning — caller manages Logger lifetime.
        // REQ-COM-050
        void SetLogger(Logger* logger) noexcept;

        // ---- Lifecycle -------------------------------------------------------

        // Initialize Winsock 2.2 and create a UDP/IP socket.
        // Must succeed before calling any other method.
        // REQ-COM-010
        bool Init();

        // Close the socket and call WSACleanup.
        // Safe to call even if Init() was never called or already shut down.
        void Shutdown();

        bool IsInitialized() const noexcept;

        // ---- Server interface ------------------------------------------------

        // Bind the socket to a local port so it can accept incoming datagrams.
        // Server calls this once after Init().
        bool Bind(uint16_t port);

        // ---- Reliable send ---------------------------------------------------

        // Send a packet with RUDP reliability:
        //   1. Assigns next sequence number to packet   (REQ-COM-020)
        //   2. Serializes and sends the packet
        //   3. Waits RUDP_TIMEOUT_MS for a matching ACK (REQ-COM-030)
        //   4. Retransmits up to RUDP_MAX_RETRIES times (REQ-COM-040)
        //   5. Logs each retransmission via m_logger    (REQ-COM-050)
        //
        // Returns true  if ACK received within retries.
        // Returns false if all retries exhausted (REQ-COM-060).
        //
        // `packet` is modified: sequence_number and timestamp are set in-place.
        bool SendReliable(Packet& packet, uint32_t flightId, const Endpoint& dest);

        // Send a bare ACK packet for a received sequence number.
        // Used internally by Receive(); exposed for callers with custom ACK logic.
        // REQ-COM-030
        bool SendAckFor(uint32_t seqNumber, uint32_t flightId, const Endpoint& dest);

        // ---- Receive ---------------------------------------------------------

        // Block for up to timeoutMs milliseconds waiting for an incoming datagram.
        //   - Deserializes the packet                  (REQ-PKT-010)
        //   - Validates checksum; returns false on fail (REQ-PKT-050/060)
        //   - Logs the RX event via m_logger           (REQ-LOG-010)
        //   - Automatically sends ACK for non-ACK pkts (REQ-COM-030)
        //
        // outSender is populated with the sender's IP and port.
        // Returns false on timeout, socket error, or checksum failure.
        bool Receive(Packet&   outPacket,
                     Endpoint& outSender,
                     uint32_t  timeoutMs = 1000U);

    private:
        // SOCKET is defined as UINT_PTR on Windows (unsigned 64-bit on x64).
        // Stored as uintptr_t here to avoid pulling winsock2.h into this header.
        // Cast back to SOCKET in RUDP.cpp before any Winsock calls.
        uintptr_t m_socketHandle;

        uint32_t  m_sequenceNumber;  // Incrementing per-sender counter (REQ-COM-020)
        bool      m_initialized;
        Logger*   m_logger;          // Non-owning; may be nullptr (REQ-COM-050)

        // Returns m_sequenceNumber and post-increments it
        uint32_t GetNextSequenceNumber() noexcept;

        // Blocks up to timeoutMs waiting for an ACK matching expectedSeq.
        // NOTE: If a non-ACK packet arrives during the wait, it is consumed from
        // the socket buffer and lost.  For this demo (sequential traffic), this
        // is acceptable.  A production implementation would use a receive queue.
        bool WaitForAck(uint32_t expectedSeq, uint32_t timeoutMs);

        // Serialize `data` and send via sendto() to `dest`
        bool SendRawBytes(const std::vector<uint8_t>& data, const Endpoint& dest);
    };

} // namespace AeroTrack
