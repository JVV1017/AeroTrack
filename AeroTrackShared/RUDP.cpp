// =============================================================================
// RUDP.cpp — Reliable UDP implementation
//
// MISRA Deviation 3: Winsock2 C API is used exclusively in this file.
// All winsock2.h includes, SOCKET types, sockaddr_in structs, and WSA*
// function calls are confined to this translation unit.
//
// Winsock interactions that would otherwise violate strict MISRA C++ rules:
//   MISRA 5-2-4 (C-style cast)  — reinterpret_cast<sockaddr*> required by API
//   MISRA 5-2-8 (void pointer)  — Winsock recv buffers are char*
//   MISRA 5-0-15 (pointer arith)— sendto / recvfrom length args
// All are justified under MISRA C++:2008 §4.3 (library code exceptions).
//
// Requirements addressed:
//   REQ-COM-010  UDP/IP transport via Winsock2 SOCK_DGRAM socket
//   REQ-COM-020  Incrementing sequence numbers assigned in SendReliable
//   REQ-COM-030  ACK sent automatically in Receive; WaitForAck in SendReliable
//   REQ-COM-040  500 ms timeout, up to 3 retries
//   REQ-COM-050  Retransmissions logged via m_logger
//   REQ-COM-060  false returned after RUDP_MAX_RETRIES exhausted
// =============================================================================

#ifdef _WIN32
// Windows: use Winsock2 for all networking
#include <winsock2.h>
#include <ws2tcpip.h>
#else
// macOS/Linux: use standard POSIX socket headers instead
#include <sys/socket.h> // Core socket functions (socket, bind, sendto, recvfrom)
#include <netinet/in.h> // sockaddr_in, INADDR_ANY, htons
#include <arpa/inet.h>  // inet_pton, inet_ntop (IP address conversion)
#include <unistd.h>     // close() — the macOS way to close a socket

// "SOCKET" is a special Windows type. On macOS a socket is just an int
using SOCKET = int;

// "DWORD" is a Windows 32-bit unsigned integer type. uint32_t is the macOS equivalent
using DWORD = uint32_t;

// On Windows INVALID_SOCKET is a special constant. On macOS -1 means the same thing
constexpr SOCKET INVALID_SOCKET = -1;

// On Windows SOCKET_ERROR is a special constant. On macOS -1 means the same thing
constexpr int SOCKET_ERROR = -1;

// WSAStartup/WSACleanup are Windows-only functions that initialise networking.
// macOS doesn't need them, so we create empty versions that do nothing
struct WSADATA
{
};
inline int WSAStartup(int, WSADATA *) { return 0; }
inline void WSACleanup() {}

// closesocket() is the Windows way to close a socket.
// We redirect it to close() which is the macOS equivalent
inline int closesocket(SOCKET s) { return ::close(s); }

// MAKEWORD packs two version numbers for WSAStartup on Windows.
// Since our WSAStartup does nothing, this just returns 0
inline int MAKEWORD(int, int) { return 0; }
#endif

// Undefine Windows macros that conflict with our enum values
#undef ERROR

#pragma comment(lib, "Ws2_32.lib")
// ---- END Winsock include block — do not replicate in any other file ----------

#include "RUDP.h"
#include "Logger.h"
#include "Config.h"

#include <cstring>
#include <string>

namespace AeroTrack
{

    // =========================================================================
    // File-scope helpers — Winsock ↔ Endpoint conversion
    // Not exposed in the header; internal linkage only.
    // =========================================================================

    static sockaddr_in EndpointToSockAddr(const Endpoint &ep)
    {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(ep.port);
        inet_pton(AF_INET, ep.ip.c_str(), &addr.sin_addr);
        return addr;
    }

    static Endpoint SockAddrToEndpoint(const sockaddr_in &addr)
    {
        Endpoint ep;
        ep.port = ntohs(addr.sin_port);

        char ipBuf[INET_ADDRSTRLEN]{};
        inet_ntop(AF_INET, &addr.sin_addr, ipBuf, INET_ADDRSTRLEN);
        ep.ip = std::string(ipBuf);

        return ep;
    }

    // Sets the socket's receive timeout.  Returns false on failure.
    static bool SetRecvTimeout(SOCKET sock, uint32_t timeoutMs)
    {
        const DWORD tv = static_cast<DWORD>(timeoutMs);
        const int result = setsockopt(
            sock,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char *>(&tv),
            static_cast<int>(sizeof(DWORD)));
        return (result != SOCKET_ERROR);
    }

    // =========================================================================
    // Constructor / Destructor
    // =========================================================================

    RUDP::RUDP()
        : m_socketHandle(static_cast<uintptr_t>(INVALID_SOCKET)), m_sequenceNumber(1U) // Sequence numbers start at 1 (0 = uninitialized)
          ,
          m_initialized(false), m_logger(nullptr)
    {
    }

    RUDP::~RUDP()
    {
        Shutdown();
    }

    void RUDP::SetLogger(Logger *logger) noexcept
    {
        m_logger = logger;
    }

    bool RUDP::IsInitialized() const noexcept
    {
        return m_initialized;
    }

    // =========================================================================
    // Init — REQ-COM-010
    // =========================================================================
    bool RUDP::Init()
    {
        WSADATA wsaData{};
        const int wsaResult = WSAStartup(MAKEWORD(2U, 2U), &wsaData);
        if (wsaResult != 0)
        {
            return false;
        }

        const SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET)
        {
            WSACleanup();
            return false;
        }

        m_socketHandle = static_cast<uintptr_t>(sock);
        m_initialized = true;
        m_sequenceNumber = 1U;
        return true;
    }

    // =========================================================================
    // Shutdown
    // =========================================================================
    void RUDP::Shutdown()
    {
        if (m_initialized)
        {
            closesocket(static_cast<SOCKET>(m_socketHandle));
            WSACleanup();
            m_socketHandle = static_cast<uintptr_t>(INVALID_SOCKET);
            m_initialized = false;
        }
    }

    // =========================================================================
    // Bind — server use only
    // =========================================================================
    bool RUDP::Bind(uint16_t port)
    {
        if (!m_initialized)
        {
            return false;
        }

        sockaddr_in bindAddr{};
        bindAddr.sin_family = AF_INET;
        bindAddr.sin_port = htons(port);
        bindAddr.sin_addr.s_addr = INADDR_ANY;

        const int result = bind(
            static_cast<SOCKET>(m_socketHandle),
            reinterpret_cast<sockaddr *>(&bindAddr),
            static_cast<int>(sizeof(sockaddr_in)));

        return (result != SOCKET_ERROR);
    }

    // =========================================================================
    // GetNextSequenceNumber — REQ-COM-020
    // =========================================================================
    uint32_t RUDP::GetNextSequenceNumber() noexcept
    {
        return m_sequenceNumber++;
    }

    // =========================================================================
    // SendRawBytes — internal serialized send
    // =========================================================================
    bool RUDP::SendRawBytes(const std::vector<uint8_t> &data, const Endpoint &dest)
    {
        if (!m_initialized || !dest.IsValid())
        {
            return false;
        }

        sockaddr_in destAddr = EndpointToSockAddr(dest);

        const int sent = sendto(
            static_cast<SOCKET>(m_socketHandle),
            reinterpret_cast<const char *>(data.data()),
            static_cast<int>(data.size()),
            0,
            reinterpret_cast<sockaddr *>(&destAddr),
            static_cast<int>(sizeof(sockaddr_in)));

        return (sent != SOCKET_ERROR);
    }

    // =========================================================================
    // WaitForAck — internal, REQ-COM-030
    //
    // Sets SO_RCVTIMEO, then blocks until either:
    //   - An ACK with ack_number == expectedSeq arrives  → returns true
    //   - Timeout or socket error                        → returns false
    //
    // Known limitation: if a non-ACK packet arrives during the wait it is
    // consumed from the socket buffer and not delivered to the application.
    // This is acceptable for the AeroTrack demo (sequential traffic pattern).
    // =========================================================================
    bool RUDP::WaitForAck(uint32_t expectedSeq, uint32_t timeoutMs)
    {
        const SOCKET sock = static_cast<SOCKET>(m_socketHandle);

        // Set receive timeout for this call
        static_cast<void>(SetRecvTimeout(sock, timeoutMs));

        // Buffer sized for the largest ACK packet (header only, no payload)
        uint8_t recvBuf[256U]{};
        sockaddr_in senderAddr{};
        socklen_t addrLen = static_cast<socklen_t>(sizeof(sockaddr_in));

        const int received = recvfrom(
            sock,
            reinterpret_cast<char *>(recvBuf),
            static_cast<int>(sizeof(recvBuf)),
            0,
            reinterpret_cast<sockaddr *>(&senderAddr),
            &addrLen);

        if (received == SOCKET_ERROR)
        {
            return false; // Timeout (WSAETIMEDOUT) or network error
        }

        const Packet incoming = Packet::Deserialize(recvBuf, static_cast<uint32_t>(received));

        return ((incoming.GetType() == PacketType::ACK) &&
                (incoming.GetAckNumber() == expectedSeq));
    }

    // =========================================================================
    // SendReliable — REQ-COM-020, REQ-COM-030, REQ-COM-040, REQ-COM-050/060
    // =========================================================================
    bool RUDP::SendReliable(Packet &packet, uint32_t /*flightId*/, const Endpoint &dest)
    {
        if (!m_initialized || !dest.IsValid())
        {
            return false;
        }

        // Assign sequence number and refresh timestamp
        packet.SetSequenceNumber(GetNextSequenceNumber()); // REQ-COM-020
        packet.SetTimestamp(Packet::CurrentTimestampMs());

        const std::vector<uint8_t> serialized = packet.Serialize();

        for (uint32_t attempt = 0U; attempt <= RUDP_MAX_RETRIES; ++attempt)
        {
            const bool sent = SendRawBytes(serialized, dest);
            if (!sent)
            {
                return false; // Hard socket error — abort immediately
            }

            // Log TX or RETRANSMIT event — REQ-COM-050
            if (m_logger != nullptr)
            {
                const std::string status = (attempt == 0U) ? "OK" : "RETRANSMIT";
                m_logger->LogPacket("TX", packet, status);
            }

            // Wait for matching ACK — REQ-COM-030
            if (WaitForAck(packet.GetSequenceNumber(), RUDP_TIMEOUT_MS))
            {
                return true; // ACK received within timeout
            }

            // If this was the last attempt, fall through to error
        }

        // REQ-COM-060: all retries exhausted
        if (m_logger != nullptr)
        {
            m_logger->LogPacket("TX", packet, "ERROR");
        }

        return false;
    }

    // =========================================================================
    // SendAckFor — REQ-COM-030
    // =========================================================================
    bool RUDP::SendAckFor(uint32_t seqNumber, uint32_t flightId, const Endpoint &dest)
    {
        Packet ack(PacketType::ACK, flightId);
        ack.SetSequenceNumber(GetNextSequenceNumber());
        ack.SetAckNumber(seqNumber);
        ack.SetTimestamp(Packet::CurrentTimestampMs());

        const std::vector<uint8_t> serialized = ack.Serialize();
        return SendRawBytes(serialized, dest);
    }

    // =========================================================================
    // Receive — REQ-COM-030, REQ-PKT-050, REQ-PKT-060, REQ-LOG-010
    // =========================================================================
    bool RUDP::Receive(Packet &outPacket, Endpoint &outSender, uint32_t timeoutMs)
    {
        if (!m_initialized)
        {
            return false;
        }

        const SOCKET sock = static_cast<SOCKET>(m_socketHandle);
        static_cast<void>(SetRecvTimeout(sock, timeoutMs));

        // Buffer large enough for header + largest payload (file chunk: ~1055 bytes)
        // 4096 provides a safe margin without excessive stack usage.
        uint8_t recvBuf[4096U]{};
        sockaddr_in senderAddr{};
        socklen_t addrLen = static_cast<socklen_t>(sizeof(sockaddr_in));

        const int received = recvfrom(
            sock,
            reinterpret_cast<char *>(recvBuf),
            static_cast<int>(sizeof(recvBuf)),
            0,
            reinterpret_cast<sockaddr *>(&senderAddr),
            &addrLen);

        if (received == SOCKET_ERROR)
        {
            return false; // Timeout or network error
        }

        outPacket = Packet::Deserialize(recvBuf, static_cast<uint32_t>(received));
        outSender = SockAddrToEndpoint(senderAddr);

        // REQ-PKT-050/060: Validate checksum — discard and log if invalid
        if (!outPacket.ValidateChecksum())
        {
            if (m_logger != nullptr)
            {
                m_logger->LogPacket("RX", outPacket, "ERROR");
            }
            return false;
        }

        // REQ-LOG-010: Log valid receive
        if (m_logger != nullptr)
        {
            m_logger->LogPacket("RX", outPacket, "OK");
        }

        // REQ-COM-030: Send ACK for all non-ACK packets
        if (outPacket.GetType() != PacketType::ACK)
        {
            static_cast<void>(
                SendAckFor(outPacket.GetSequenceNumber(), outPacket.GetFlightId(), outSender));
        }

        return true;
    }

} // namespace AeroTrack
