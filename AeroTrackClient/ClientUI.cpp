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

    static const char* ClientStateToString(ClientState state) noexcept {
        switch (state) {
        case ClientState::DISCONNECTED:  return "DISCONNECTED";
        case ClientState::CONNECTING:    return "CONNECTING...";
        case ClientState::CONNECTED:     return "CONNECTED";
        case ClientState::TRACKING:      return "TRACKING";
        case ClientState::DISCONNECTING: return "DISCONNECTING";
        default:                         return "UNKNOWN";
        }
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
        // MISRA Deviation 2: system() in UI module — approved
        std::system("cls");

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
            m_eventLog.erase(m_eventLog.begin());
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
        std::snprintf(line, sizeof(line), "  Callsign : %-8s", m_client.GetCallsign().c_str());
        std::cout << line << "\n";

        std::snprintf(line, sizeof(line), "  Flight ID: %u", m_client.GetFlightId());
        std::cout << line << "\n";

        std::snprintf(line, sizeof(line), "  Sector   : %u", m_client.GetSectorId());
        std::cout << line << "\n";

        std::snprintf(line, sizeof(line), "  Status   : %s",
            ClientStateToString(m_client.GetState()));
        std::cout << line << "\n";
    }

    void ClientUI::PrintPositionData() const noexcept {
        // MISRA Deviation 2: cout in UI — approved
        std::cout << "  POSITION DATA\n";

        const PositionReporter& pr = m_client.GetPositionReporter();
        char line[64];

        std::snprintf(line, sizeof(line), "  Latitude : %.4f deg", pr.GetLatitude());
        std::cout << line << "\n";

        std::snprintf(line, sizeof(line), "  Longitude: %.4f deg", pr.GetLongitude());
        std::cout << line << "\n";

        std::snprintf(line, sizeof(line), "  Altitude : %u ft", pr.GetAltitude());
        std::cout << line << "\n";

        std::snprintf(line, sizeof(line), "  Speed    : %u kts", pr.GetSpeed());
        std::cout << line << "\n";

        std::snprintf(line, sizeof(line), "  Heading  : %u deg", pr.GetHeading());
        std::cout << line << "\n";

        // File transfer progress
        const FileReceiver& fr = m_client.GetFileReceiver();
        if (fr.GetState() == TransferState::RECEIVING) {
            std::snprintf(line, sizeof(line),
                "  Radar DL : [%u%%] %u/%u chunks",
                fr.GetProgressPercent(), fr.GetReceivedChunks(), fr.GetTotalChunks());
            std::cout << line << "\n";
        }
        else if (fr.GetState() == TransferState::COMPLETE) {
            std::snprintf(line, sizeof(line),
                "  Radar DL : COMPLETE -> %s", fr.GetOutputPath().c_str());
            std::cout << line << "\n";
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