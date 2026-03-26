#pragma once
// =============================================================================
// Logger.h — AeroTrack TX/RX packet and state transition logging
//
// Requirements addressed:
//   REQ-LOG-010  Log every TX/RX packet in the defined pipe-delimited format
//   REQ-LOG-020  ISO 8601 timestamps with millisecond precision
//   REQ-LOG-030  Server logs to aerotrack_server.log; clients to aerotrack_client_[ID].log
//   REQ-LOG-040  Log file created at startup, appended throughout runtime
//   REQ-LOG-050  Flush to disk after each write
//   REQ-LOG-060  State transition logging (server only)
//
// MISRA Deviation 2:
//   std::ofstream and std::ostringstream are used in Logger.cpp.
//   This deviation is justified by REQ-SYS-040 (file logging) and REQ-LOG-040.
//   Stream I/O is confined to this module and the UI modules only.
//   Stream I/O does NOT appear in Packet.cpp, RUDP.cpp, or StateMachine.cpp.
// =============================================================================

#include "Packet.h"
#include <string>
#include <fstream>   // MISRA Deviation 2 — stream I/O permitted in Logger only

namespace AeroTrack {

    class Logger
    {
    public:
        Logger();
        ~Logger();

        // Non-copyable: owns a file-stream resource
        Logger(const Logger&)            = delete;
        Logger& operator=(const Logger&) = delete;

        // Open (or create) the log file and begin appending.
        // REQ-LOG-030, REQ-LOG-040
        //
        // Suggested filenames:
        //   Server  →  "aerotrack_server.log"
        //   Client  →  "aerotrack_client_" + std::to_string(flightId) + ".log"
        bool Init(const std::string& filename);

        // Flush and close the log file.
        void Close();

        bool IsOpen() const noexcept;

        // ---- Packet event logging (REQ-LOG-010) ------------------------------
        //
        // Produces one line per call:
        //   YYYY-MM-DD HH:MM:SS.mmm | TX | POSITION_REPORT | SEQ:0047 | FLT:801 | SIZE:51 | STATUS:OK
        //
        // direction : "TX" or "RX"
        // status    : "OK", "RETRANSMIT", or "ERROR"
        void LogPacket(const std::string& direction,
                       const Packet&      packet,
                       const std::string& status = "OK");

        // ---- State transition logging (REQ-LOG-060, server only) -------------
        //
        // Produces one line per call:
        //   YYYY-MM-DD HH:MM:SS.mmm | STATE_CHANGE | FLT:801 | FROM:CONNECTED | TO:TRACKING | TRIGGER:POSITION_REPORT
        void LogStateChange(uint32_t           flightId,
                            const std::string& fromState,
                            const std::string& toState,
                            const std::string& trigger);

        // ---- General diagnostics ---------------------------------------------
        void LogInfo(const std::string& message);
        void LogError(const std::string& message);

    private:
        std::ofstream m_file;  // MISRA Deviation 2

        // Returns current local time formatted as YYYY-MM-DD HH:MM:SS.mmm
        // REQ-LOG-020
        static std::string CurrentTimestamp();

        // Write entry + newline, then flush immediately (REQ-LOG-050)
        void WriteEntry(const std::string& entry);
    };

} // namespace AeroTrack
