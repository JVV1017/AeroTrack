// REQ-CLT-050: Flight terminal console UI
// REQ-SYS-040: Distinct from ServerUI
// MISRA Deviation 2: cout/printf in UI module — approved

#include "ClientUI.h"
#include "Client.h"
#include "PositionReporter.h"
#include "FileReceiver.h"
#include <cstdio>
#include <iostream>

namespace AeroTrack {

    // MISRA Fix [V2520]: Restructured from return-per-case to assign+break pattern.
    // MISRA Rule 6-4-5 requires every switch clause to be terminated by an
    // unconditional 'break' or 'throw'. 'return' does not satisfy this rule even
    // though it exits the function. Adding 'break' after 'return' would produce
    // unreachable code (also a violation), so the correct fix is to assign to a
    // local variable in each case and break, then return after the switch.
    static const char* ClientStateToString(ClientState state) noexcept {
        const char* result = "UNKNOWN";
        switch (state) {
        case ClientState::DISCONNECTED:  result = "DISCONNECTED";  break;
        case ClientState::CONNECTING:    result = "CONNECTING..."; break;
        case ClientState::CONNECTED:     result = "CONNECTED";     break;
        case ClientState::TRACKING:      result = "TRACKING";      break;
        case ClientState::DISCONNECTING: result = "DISCONNECTING"; break;
        default:                         result = "UNKNOWN";        break;
        }
        return result;
    }

    ClientUI::ClientUI(const Client& client) noexcept
        : m_client(client)
        , m_renderTick(0U)
    {
        m_eventLog.reserve(MAX_LOG_LINES);
    }

    void ClientUI::Render() noexcept {
        ++m_renderTick;
        if ((m_renderTick % RENDER_INTERVAL) != 0U) {
            return;
        }
        // MISRA Deviation 2: system() in UI module — approved (REQ-SYS-040)
        // MISRA Fix [V2547]: system() return value explicitly discarded.
        // Return value is the shell exit code; screen-clear failure is
        // non-fatal for the UI render loop, so discard is intentional.
        (void)std::system("cls");

        PrintHeader();
        PrintFlightStatus();
        PrintDivider();
        PrintPositionData();
        PrintDivider();
        PrintCommLog();
        PrintDivider();
        PrintFooter();
    }

    void ClientUI::AppendEvent(const char* msg) noexcept {
        if (msg == nullptr) {
            return;
        }
        if (m_eventLog.size() >= static_cast<size_t>(MAX_LOG_LINES)) {
            // MISRA Deviation 1: erase on vector — RAII managed
            // MISRA Fix [V2547]: erase() returns an iterator to the next element;
            // discarding it is intentional — we only need the oldest entry removed,
            // not a reference to what follows it.
            (void)m_eventLog.erase(m_eventLog.begin());
        }
        m_eventLog.push_back(std::string(msg));
    }

    void ClientUI::PrintCommandHelp() noexcept {
        PrintDivider();
        // MISRA Deviation 2: cout in UI — approved
        std::cout << "  AeroTrack Flight Terminal  |  Ctrl+C to disconnect\n";
        PrintDivider();
    }

    void ClientUI::PrintHeader() const noexcept {
        // MISRA Deviation 2: cout in UI — approved
        std::cout << "+=================================================+\n";
        std::cout << "|        AEROTRACK  FLIGHT  TERMINAL              |\n";
        std::cout << "+=================================================+\n";
    }

    void ClientUI::PrintFlightStatus() const noexcept {
        char line[64];

        // MISRA Fix [V2547]: snprintf return value explicitly discarded throughout
        // this function. Return value is the number of characters that would have
        // been written; buffer is sized to hold all expected values, so truncation
        // is not possible in practice. Discard is intentional.
        (void)std::snprintf(line, sizeof(line), "  Callsign : %-8s", m_client.GetCallsign().c_str());
        std::cout << line << "\n";

        (void)std::snprintf(line, sizeof(line), "  Flight ID: %u", m_client.GetFlightId());
        std::cout << line << "\n";

        (void)std::snprintf(line, sizeof(line), "  Sector   : %u", m_client.GetSectorId());
        std::cout << line << "\n";

        (void)std::snprintf(line, sizeof(line), "  Status   : %s",
            ClientStateToString(m_client.GetState()));
        std::cout << line << "\n";
    }

    void ClientUI::PrintPositionData() const noexcept {
        // MISRA Deviation 2: cout in UI — approved
        std::cout << "  POSITION DATA\n";

        const PositionReporter& pr = m_client.GetPositionReporter();
        char line[64];

        // MISRA Fix [V2547]: snprintf return value explicitly discarded throughout
        // this function — same rationale as PrintFlightStatus above.
        (void)std::snprintf(line, sizeof(line), "  Latitude : %.4f deg", pr.GetLatitude());
        std::cout << line << "\n";

        (void)std::snprintf(line, sizeof(line), "  Longitude: %.4f deg", pr.GetLongitude());
        std::cout << line << "\n";

        (void)std::snprintf(line, sizeof(line), "  Altitude : %u ft", pr.GetAltitude());
        std::cout << line << "\n";

        (void)std::snprintf(line, sizeof(line), "  Speed    : %u kts", pr.GetSpeed());
        std::cout << line << "\n";

        (void)std::snprintf(line, sizeof(line), "  Heading  : %u deg", pr.GetHeading());
        std::cout << line << "\n";

        // File transfer progress
        const FileReceiver& fr = m_client.GetFileReceiver();
        if (fr.GetState() == TransferState::RECEIVING) {
            (void)std::snprintf(line, sizeof(line),
                "  Radar DL : [%u%%] %u/%u chunks",
                fr.GetProgressPercent(), fr.GetReceivedChunks(), fr.GetTotalChunks());
            std::cout << line << "\n";
        }
        else if (fr.GetState() == TransferState::COMPLETE) {
            (void)std::snprintf(line, sizeof(line),
                "  Radar DL : COMPLETE -> %s", fr.GetOutputPath().c_str());
            std::cout << line << "\n";
        }
        else {
            // MISRA Fix [V2516]: terminal else required by MISRA Rule 6-4-2.
            // TransferState::IDLE (and any future states) — no transfer line
            // is shown on the display. This is intentional behaviour.
        }
    }

    void ClientUI::PrintCommLog() const noexcept {
        // MISRA Deviation 2: cout in UI — approved
        std::cout << "  COMM LOG\n";
        if (m_eventLog.empty()) {
            std::cout << "  (no events)\n";
            return;
        }
        for (const std::string& entry : m_eventLog) {
            std::cout << "  > " << entry << "\n";
        }
    }

    void ClientUI::PrintFooter() const noexcept {
        // MISRA Deviation 2: cout in UI — approved
        std::cout << "  Press Ctrl+C to disconnect and exit.\n";
    }

    void ClientUI::PrintDivider() noexcept {
        // MISRA Deviation 2: cout in UI — approved
        std::cout << "  -------------------------------------------------\n";
    }

} // namespace AeroTrack