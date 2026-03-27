// =============================================================================
// Logger.cpp — Logging implementation
//
// MISRA Deviation 2: std::ofstream and std::ostringstream are used exclusively
// in this file.  Justified by REQ-LOG-040 (persistent file logging) and
// REQ-SYS-040 (console/file output requirement).
//
// Requirements addressed:
//   REQ-LOG-010  Pipe-delimited TX/RX log format
//   REQ-LOG-020  ISO 8601 with milliseconds (YYYY-MM-DD HH:MM:SS.mmm)
//   REQ-LOG-030  Filename supplied by caller (server vs. client distinction)
//   REQ-LOG-040  Opened at startup with std::ios::app — appended, not overwritten
//   REQ-LOG-050  m_file.flush() called after every WriteEntry
//   REQ-LOG-060  Pipe-delimited STATE_CHANGE log format
// =============================================================================

#include "Logger.h"

#include <chrono>
#include <ctime>
#include <sstream> // MISRA Deviation 2
#include <iomanip> // MISRA Deviation 2

namespace AeroTrack
{

    Logger::Logger() = default;
    Logger::~Logger() { Close(); }

    // =========================================================================
    // Init — REQ-LOG-030, REQ-LOG-040
    // =========================================================================
    bool Logger::Init(const std::string &filename)
    {
        m_file.open(filename, std::ios::out | std::ios::app);
        return m_file.is_open();
    }

    void Logger::Close()
    {
        if (m_file.is_open())
        {
            m_file.flush();
            m_file.close();
        }
    }

    bool Logger::IsOpen() const noexcept
    {
        return m_file.is_open();
    }

    // =========================================================================
    // CurrentTimestamp — REQ-LOG-020
    //
    // Format: YYYY-MM-DD HH:MM:SS.mmm (local time, millisecond precision)
    //
    // Uses localtime_s (MSVC) for thread-safe struct tm population.
    // =========================================================================
    std::string Logger::CurrentTimestamp()
    {
        const auto now = std::chrono::system_clock::now();

        // Milliseconds component (0–999)
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()) %
                        1000;

        const std::time_t t = std::chrono::system_clock::to_time_t(now);

        std::tm tmBuf{};
#ifdef _WIN32
        localtime_s(&tmBuf, &t); // Thread-safe (MSVC / Windows)
#else
        localtime_r(&t, &tmBuf); // Thread-safe (macOS / Linux)
#endif

        std::ostringstream oss;
        oss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S")
            << '.'
            << std::setfill('0') << std::setw(3) << ms.count();

        return oss.str();
    }

    // =========================================================================
    // WriteEntry — REQ-LOG-050
    //
    // Appends the entry string and a newline, then flushes to disk.
    // Flushing after every write prevents data loss on unexpected termination.
    // =========================================================================
    void Logger::WriteEntry(const std::string &entry)
    {
        if (m_file.is_open())
        {
            m_file << entry << '\n';
            m_file.flush(); // REQ-LOG-050
        }
    }

    // =========================================================================
    // LogPacket — REQ-LOG-010
    //
    // Output format:
    //   2026-03-20 08:15:03.412 | TX | POSITION_REPORT | SEQ:0047 | FLT:801 | SIZE:51 | STATUS:OK
    //
    // SIZE is the total wire size: header (27 bytes) + payload bytes.
    // =========================================================================
    void Logger::LogPacket(const std::string &direction,
                           const Packet &packet,
                           const std::string &status)
    {
        const uint32_t wireSize =
            static_cast<uint32_t>(sizeof(PacketHeader)) + packet.GetPayloadLength();

        std::ostringstream oss;
        oss << CurrentTimestamp()
            << " | " << direction
            << " | " << packet.TypeString()
            << " | SEQ:" << std::setw(4) << std::setfill('0') << packet.GetSequenceNumber()
            << " | FLT:" << packet.GetFlightId()
            << " | SIZE:" << wireSize
            << " | STATUS:" << status;

        WriteEntry(oss.str());
    }

    // =========================================================================
    // LogStateChange — REQ-LOG-060
    //
    // Output format:
    //   2026-03-20 08:15:05.001 | STATE_CHANGE | FLT:801 | FROM:CONNECTED | TO:TRACKING | TRIGGER:POSITION_REPORT
    // =========================================================================
    void Logger::LogStateChange(uint32_t flightId,
                                const std::string &fromState,
                                const std::string &toState,
                                const std::string &trigger)
    {
        std::ostringstream oss;
        oss << CurrentTimestamp()
            << " | STATE_CHANGE"
            << " | FLT:" << flightId
            << " | FROM:" << fromState
            << " | TO:" << toState
            << " | TRIGGER:" << trigger;

        WriteEntry(oss.str());
    }

    // =========================================================================
    // LogInfo / LogError — general diagnostics
    // =========================================================================
    void Logger::LogInfo(const std::string &message)
    {
        WriteEntry(CurrentTimestamp() + " | INFO | " + message);
    }

    void Logger::LogError(const std::string &message)
    {
        WriteEntry(CurrentTimestamp() + " | ERROR | " + message);
    }

} // namespace AeroTrack
