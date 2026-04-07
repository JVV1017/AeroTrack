// =============================================================================
// ServerUI.cpp — Console-based ground control dashboard implementation
// =============================================================================
// Requirements: REQ-SVR-060, REQ-SYS-040
// Standard:     MISRA C++ — MISRA Deviation 2: std::cout used for console UI
// =============================================================================

// MISRA Deviation 2: Stream I/O permitted in UI modules per project deviation log
#include <iostream>
#include <iomanip>
#include <sstream>

#include "ServerUI.h"
#include "StateMachine.h"
#include "Config.h"

// <cstdlib> removed — system() call replaced with ANSI escape (MISRA 18-0-3, V2509)

namespace AeroTrack {

    // ---------------------------------------------------------------------------
    // Constructor
    // ---------------------------------------------------------------------------
    ServerUI::ServerUI()
        : m_registry(nullptr)
        , m_handoffManager(nullptr)
        , m_eventLog()
        , m_maxEvents(20U)
        , m_startTime(std::chrono::steady_clock::now())
    {}

    // ---------------------------------------------------------------------------
    // Attach — set read-only data sources
    // ---------------------------------------------------------------------------
    void ServerUI::Attach(const FlightRegistry* registry,
        const HandoffManager* handoffManager)
    {
        m_registry = registry;
        m_handoffManager = handoffManager;
    }

    // ---------------------------------------------------------------------------
    // AddEvent — add timestamped entry to the rolling event log
    // ---------------------------------------------------------------------------
    void ServerUI::AddEvent(const std::string& event)
    {
        // Build timestamp prefix
        const auto now = std::chrono::system_clock::now();
        const auto timeT = std::chrono::system_clock::to_time_t(now);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count() % 1000;

        struct tm timeInfo {};

#if defined(_WIN32)
        // MISRA 0-1-7 (V2547): localtime_s returns errno_t; result is checked.
        const errno_t localErr = localtime_s(&timeInfo, &timeT);
        if (localErr != 0) {
            // Time conversion failed — timeInfo remains zero-initialised;
            // timestamp will show 00:00:00.000 rather than crashing.
        }
#else
        localtime_r(&timeT, &timeInfo);
#endif

        // MISRA DEV-005 (V2578): "%H:%M:%S" is a string literal passed to
        // std::put_time which takes const char*. No modification occurs.
        // See project deviation log DEV-005.
        static const char kTimeFmt[] = "%H:%M:%S";

        std::ostringstream oss;
        oss << std::put_time(&timeInfo, kTimeFmt)
            << "." << std::setw(3) << std::setfill('0') << ms
            << "  " << event;

        m_eventLog.push_back(oss.str());

        // Trim to max size (circular buffer behaviour)
        while (m_eventLog.size() > static_cast<size_t>(m_maxEvents)) {
            // MISRA 0-1-7 (V2547): vector::erase returns iterator; explicitly
            // discarded — the updated iterator is not needed here.
            (void)m_eventLog.erase(m_eventLog.begin());
        }
    }

    // ---------------------------------------------------------------------------
    // Render — full dashboard redraw
    // ---------------------------------------------------------------------------
    // MISRA DEV-004 (V2506): void functions use early return guard clauses.
    // ---------------------------------------------------------------------------
    void ServerUI::Render() const
    {
        ClearConsole();
        RenderHeader();
        RenderStatusBar();
        RenderFlightTable();
        RenderPendingHandoffs();
        RenderEventLog();
        RenderCommandHints();
    }

    // ---------------------------------------------------------------------------
    // PrintStatus — single line output without full redraw
    // ---------------------------------------------------------------------------
    void ServerUI::PrintStatus(const std::string& message) const
    {
        std::cout << "[GCS] " << message << "\n";
    }

    // ---------------------------------------------------------------------------
    // SetMaxEventLogSize
    // ---------------------------------------------------------------------------
    void ServerUI::SetMaxEventLogSize(uint32_t maxEvents)
    {
        m_maxEvents = maxEvents;
    }

    // ---------------------------------------------------------------------------
    // RenderHeader — server identity banner
    // ---------------------------------------------------------------------------
    void ServerUI::RenderHeader() const
    {
        std::cout << "\n";
        std::cout << "================================================================\n";
        std::cout << "       AEROTRACK GROUND CONTROL STATION  [SERVER DASHBOARD]\n";
        std::cout << "       CSCN74000 | DAL-C | DO-178C | MISRA C++\n";
        std::cout << "================================================================\n";
    }

    // ---------------------------------------------------------------------------
    // RenderStatusBar — uptime, flight count, pending handoffs
    // ---------------------------------------------------------------------------
    void ServerUI::RenderStatusBar() const
    {
        const auto now = std::chrono::steady_clock::now();
        const auto uptime = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - m_startTime).count();

        const uint32_t flightCount = (m_registry != nullptr)
            ? m_registry->GetFlightCount() : 0U;
        const uint32_t handoffCount = (m_handoffManager != nullptr)
            ? m_handoffManager->GetPendingHandoffCount() : 0U;

        std::cout << "\n";
        std::cout << "  Uptime: " << FormatDuration(static_cast<uint64_t>(uptime))
            << "   |   Active Flights: " << flightCount
            << "   |   Pending Handoffs: " << handoffCount
            << "   |   Port: " << SERVER_PORT << "\n";
        std::cout << "----------------------------------------------------------------\n";
    }

    // ---------------------------------------------------------------------------
    // RenderFlightTable — all registered aircraft
    // ---------------------------------------------------------------------------
    // REQ-SVR-060: shows connected aircraft, flight states, sector assignments
    // MISRA DEV-004 (V2506): early returns are guard clauses only.
    // ---------------------------------------------------------------------------
    void ServerUI::RenderFlightTable() const
    {
        std::cout << "\n  TRACKED AIRCRAFT\n";
        std::cout << "  " << std::string(60, '-') << "\n";

        if (m_registry == nullptr) {
            std::cout << "  [No registry attached]\n";
            return;
        }

        const auto& flights = m_registry->GetAllFlights();

        if (flights.empty()) {
            std::cout << "  (No aircraft connected)\n";
            return;
        }

        // Table header
        std::cout << "  " << std::left
            << std::setw(8) << "FLT-ID"
            << std::setw(12) << "CALLSIGN"
            << std::setw(20) << "STATE"
            << std::setw(10) << "SECTOR"
            << std::setw(14) << "LAST CONTACT"
            << "\n";
        std::cout << "  " << std::string(60, '-') << "\n";

        for (const auto& pair : flights) {
            const FlightRecord& rec = pair.second;

            const std::string stateStr = FlightStateToString(
                rec.stateMachine.GetCurrentState());

            const uint64_t lastContact = rec.stateMachine.GetMillisSinceLastPacket();
            const std::string contactStr = FormatDuration(lastContact) + " ago";

            std::cout << "  " << std::left
                << std::setw(8) << rec.flightId
                << std::setw(12) << rec.callsign
                << std::setw(20) << stateStr
                << std::setw(10) << rec.sectorId
                << std::setw(14) << contactStr
                << "\n";

            // Position sub-row (if available)
            if (rec.hasPosition) {
                std::cout << "         "
                    << FormatPosition(rec.lastPosition) << "\n";
            }
        }
    }

    // ---------------------------------------------------------------------------
    // RenderPendingHandoffs — detail on in-progress handoffs
    // ---------------------------------------------------------------------------
    // MISRA DEV-004 (V2506): early returns are guard clauses only.
    // ---------------------------------------------------------------------------
    void ServerUI::RenderPendingHandoffs() const
    {
        if (m_handoffManager == nullptr) {
            return;
        }

        if (m_handoffManager->GetPendingHandoffCount() == 0U) {
            return;  // Don't render section if no handoffs pending
        }

        std::cout << "\n  PENDING HANDOFFS\n";
        std::cout << "  " << std::string(60, '-') << "\n";

        if (m_registry == nullptr) {
            return;
        }

        // Iterate flights and check which have pending handoffs
        const auto& flights = m_registry->GetAllFlights();
        for (const auto& pair : flights) {
            const PendingHandoff* ph = m_handoffManager->GetPendingHandoff(pair.first);
            if (ph != nullptr) {
                const auto now = std::chrono::steady_clock::now();
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - ph->initiatedTime).count();

                std::cout << "  Flight " << ph->flightId
                    << ": Sector " << ph->fromSectorId
                    << " -> " << ph->toSectorId
                    << "  [" << (ph->instructSent ? "PENDING" : "INITIATED")
                    << ", " << FormatDuration(static_cast<uint64_t>(elapsed))
                    << " elapsed"
                    << ", timeout at " << FormatDuration(HANDOFF_TIMEOUT_MS)
                    << "]\n";
            }
        }
    }

    // ---------------------------------------------------------------------------
    // RenderEventLog — rolling event log (REQ-SVR-060: event log)
    // ---------------------------------------------------------------------------
    // MISRA DEV-004 (V2506): early return is a guard clause only.
    // ---------------------------------------------------------------------------
    void ServerUI::RenderEventLog() const
    {
        std::cout << "\n  EVENT LOG (last " << m_maxEvents << " events)\n";
        std::cout << "  " << std::string(60, '-') << "\n";

        if (m_eventLog.empty()) {
            std::cout << "  (No events yet)\n";
            return;
        }

        // Show most recent events last (natural chronological order)
        for (const auto& entry : m_eventLog) {
            std::cout << "  " << entry << "\n";
        }
    }

    // ---------------------------------------------------------------------------
    // RenderCommandHints
    // ---------------------------------------------------------------------------
    void ServerUI::RenderCommandHints() const
    {
        std::cout << "\n";
        std::cout << "----------------------------------------------------------------\n";
        std::cout << "  [Ctrl+C] Shutdown   |   Dashboard refreshes on each packet\n";
        std::cout << "================================================================\n";
    }

    // ---------------------------------------------------------------------------
    // ClearConsole — platform-specific console clear
    // ---------------------------------------------------------------------------
    // MISRA 18-0-3 fix (V2509): system("cls") replaced with ANSI escape sequence.
    // \033[2J clears the screen; \033[H resets cursor to home position.
    // Works on Windows 10+ (Virtual Terminal Processing enabled by default) and
    // all Unix-like systems. No subprocess is spawned; no shell is invoked.
    // ---------------------------------------------------------------------------
    void ServerUI::ClearConsole() const
    {
        std::cout << "\033[2J\033[H" << std::flush;
    }

    // ---------------------------------------------------------------------------
    // FormatDuration — convert milliseconds to human-readable string
    // ---------------------------------------------------------------------------
    // MISRA DEV-004 (V2506): early returns are guard clauses only.
    // ---------------------------------------------------------------------------
    std::string ServerUI::FormatDuration(uint64_t milliseconds)
    {
        if (milliseconds < 1000U) {
            return std::to_string(milliseconds) + "ms";
        }

        const uint64_t seconds = milliseconds / 1000U;
        if (seconds < 60U) {
            return std::to_string(seconds) + "s";
        }

        const uint64_t minutes = seconds / 60U;
        const uint64_t remSec = seconds % 60U;
        return std::to_string(minutes) + "m " + std::to_string(remSec) + "s";
    }

    // ---------------------------------------------------------------------------
    // FormatPosition — lat/lon/alt as compact string
    // ---------------------------------------------------------------------------
    std::string ServerUI::FormatPosition(const PositionPayload& pos)
    {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(4)
            << pos.latitude << ", " << pos.longitude
            << " @ " << pos.altitude_ft << " ft"
            << " | " << pos.speed_kts << " kts"
            << " hdg " << pos.heading_deg;
        return oss.str();
    }

} // namespace AeroTrack