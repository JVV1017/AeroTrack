#pragma once
// =============================================================================
// ServerUI.h — Console-based ground control dashboard for AeroTrack
// =============================================================================
// Requirements: REQ-SVR-060, REQ-SYS-040
// Standard:     MISRA C++ — MISRA Deviation 2: std::cout used for UI display
//
// Design:
//   ServerUI reads state from FlightRegistry and HandoffManager (const refs)
//   and renders a text-based dashboard to stdout. It also maintains an
//   in-memory circular event log buffer that captures recent events for
//   display (separate from the file-based Logger).
//
//   REQ-SYS-040 compliance: This dashboard layout is distinct from the
//   client's flight terminal UI (ClientUI). No shared UI code between them.
//
//   Display sections:
//     1. Header banner (server identity)
//     2. Status bar (uptime, flight count, pending handoffs)
//     3. Flight tracking table (all registered aircraft)
//     4. Pending handoff details
//     5. Recent event log (last N events)
//     6. Command hints
// =============================================================================
#pragma once

#include "FlightRegistry.h"
#include "HandoffManager.h"

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>

namespace AeroTrack {

    // ---------------------------------------------------------------------------
    // ServerUI — ground control dashboard renderer
    // ---------------------------------------------------------------------------
    class ServerUI {
    public:
        ServerUI();

        // -----------------------------------------------------------------------
        // Set display references — call once during Server::Init()
        // -----------------------------------------------------------------------
        // Non-owning const pointers. ServerUI only reads, never modifies.
        // -----------------------------------------------------------------------
        void Attach(const FlightRegistry* registry,
            const HandoffManager* handoffManager);

        // -----------------------------------------------------------------------
        // Add an event to the rolling event log
        // -----------------------------------------------------------------------
        // Called by Server whenever something notable happens (connect, handoff,
        // timeout, disconnect, file transfer, etc.). Events are timestamped
        // automatically and stored in a circular buffer.
        // -----------------------------------------------------------------------
        void AddEvent(const std::string& event);

        // -----------------------------------------------------------------------
        // Render the full dashboard to stdout
        // -----------------------------------------------------------------------
        // Clears the console and redraws everything. Called periodically from
        // the Server main loop (e.g., after each packet or on a timer).
        // -----------------------------------------------------------------------
        void Render() const;

        // -----------------------------------------------------------------------
        // Print a single status line without full redraw
        // -----------------------------------------------------------------------
        // Useful for logging events between full renders.
        // -----------------------------------------------------------------------
        void PrintStatus(const std::string& message) const;

        // -----------------------------------------------------------------------
        // Configuration
        // -----------------------------------------------------------------------
        void SetMaxEventLogSize(uint32_t maxEvents);

    private:
        // -----------------------------------------------------------------------
        // Rendering helpers
        // -----------------------------------------------------------------------
        void RenderHeader() const;
        void RenderStatusBar() const;
        void RenderFlightTable() const;
        void RenderPendingHandoffs() const;
        void RenderEventLog() const;
        void RenderCommandHints() const;
        void ClearConsole() const;

        // Format a duration as "Xm Ys" or "Xs"
        static std::string FormatDuration(uint64_t milliseconds);

        // Format a position as "lat, lon @ alt ft"
        static std::string FormatPosition(const PositionPayload& pos);

        // -----------------------------------------------------------------------
        // State (read-only references set via Attach)
        // -----------------------------------------------------------------------
        const FlightRegistry* m_registry;
        const HandoffManager* m_handoffManager;

        // -----------------------------------------------------------------------
        // Rolling event log (circular buffer)
        // -----------------------------------------------------------------------
        std::vector<std::string> m_eventLog;
        uint32_t m_maxEvents;

        // -----------------------------------------------------------------------
        // Server start time (for uptime display)
        // -----------------------------------------------------------------------
        std::chrono::steady_clock::time_point m_startTime;
    };

} // namespace AeroTrack