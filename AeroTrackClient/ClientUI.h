#pragma once
// REQ-CLT-050: Console flight terminal UI
// REQ-SYS-040: Distinct from ServerUI — no shared UI code

#include <string>
#include <vector>
#include <cstdint>

namespace AeroTrack {

    class Client;

    class ClientUI {
    public:
        explicit ClientUI(const Client& client) noexcept;

        // REQ-CLT-050: Render the full terminal dashboard
        void Render() noexcept;

        // Append to scrolling comm log (last MAX_LOG_LINES entries)
        void AppendEvent(const char* msg) noexcept;

        void PrintCommandHelp() noexcept;

    private:
        const Client& m_client;

        // MISRA Deviation 1: vector RAII per REQ-SYS-030
        std::vector<std::string> m_eventLog;

        uint32_t m_renderTick;

        static constexpr uint32_t RENDER_INTERVAL = 5U;
        static constexpr uint32_t MAX_LOG_LINES = 10U;

        void PrintHeader()       const noexcept;
        void PrintFlightStatus() const noexcept;
        void PrintPositionData() const noexcept;
        void PrintCommLog()      const noexcept;
        void PrintFooter()       const noexcept;
        static void PrintDivider()     noexcept;
    };

} // namespace AeroTrack