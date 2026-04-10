# AeroTrack

Aircraft-to-ground communication system built in C++. A client (aircraft terminal) connects to a server (ground control), sends position reports, handles sector handoffs, and receives large file transfers over a custom reliable UDP layer.

---

## Solution Structure

```
AeroTrack.sln
├── AeroTrackShared/     — Static library used by all projects
├── AeroTrackServer/     — Ground control console app
├── AeroTrackClient/     — Aircraft terminal console app
└── AeroTrackTests/      — MSTest unit, integration, and system tests
```

---

## Build Setup

### Prerequisites

- Visual Studio 2022 with the **Desktop development with C++** workload
- Git

### 1. AeroTrackShared (build this first)

Right-click Solution → Add → New Project → **Static Library (C++)**
- Name: `AeroTrackShared`
- C++ Standard: ISO C++17
- Add files: `Packet.h/cpp`, `PacketTypes.h`, `RUDP.h/cpp`, `Logger.h/cpp`, `FlightData.h`, `Config.h`

### 2. AeroTrackServer

Right-click Solution → Add → New Project → **Console App (C++)**
- Name: `AeroTrackServer`
- Additional Include Directories: `$(SolutionDir)AeroTrackShared`
- Additional Dependencies (Linker → Input): `Ws2_32.lib`
- Add Reference → check **AeroTrackShared**
- Add files: `main.cpp`, `Server.h/cpp`, `Statemachine.h/cpp`, `Flightregistry.h/cpp`, `Handoffmanager.h/cpp`, `Filetransfer.h/cpp`, `Serverui.h/cpp`

### 3. AeroTrackClient

Right-click Solution → Add → New Project → **Console App (C++)**
- Name: `AeroTrackClient`
- Same include/linker config as Server
- Add Reference → check **AeroTrackShared**
- Add files: `main.cpp`, `Client.h/cpp`, `PositionReporter.h/cpp`, `HandoffHandler.h/cpp`, `FileReceiver.h/cpp`, `ClientUI.h/cpp`

### 4. Build

`Ctrl+Shift+B` — build the full solution. AeroTrackShared must compile before the other two; Visual Studio handles this automatically via the project references.

---

## Running the System

Open **two PowerShell terminals** from the repo root and run in order:

**Terminal 1 — Server:**
```powershell
.\x64\Debug\AeroTrackServer.exe
```

**Terminal 2 — Client** (flight ID `1001`, callsign `AC123`):
```powershell
.\x64\Debug\AeroTrackClient.exe 1001 AC123
```

The client connects to `127.0.0.1:27015` by default. These are set in `Config.h` and can be changed there.

---

## How the Server Works

The server is the ground control station. On startup it opens a UDP socket and waits for connections.

**Connection:** When a `CONNECT` packet arrives, it sends back `CONNECT_ACK` and waits for `CONNECT_CONFIRM`. Nothing else is processed until that three-step handshake finishes.

**Tracking:** Once connected, the server expects `POSITION_REPORT` packets every ~2 seconds. Each report updates the flight's position in `FlightRegistry`. The server logs every TX/RX packet to `aerotrack_server.log`.

**State machine:** Each flight has its own `StateMachine` instance running server-side. The states move like this:

```
IDLE → CONNECTED → TRACKING → HANDOFF_INITIATED → HANDOFF_PENDING → HANDOFF_COMPLETE
                                                                    ↘ HANDOFF_FAILED
TRACKING ←→ LOST_CONTACT
```

When the server detects the aircraft is near a sector boundary, it sends `HANDOFF_INSTRUCT` and waits for `HANDOFF_ACK`. If none arrives within 5 seconds, the state rolls back to `TRACKING`.

**File transfer:** The server sends a 1MB+ sector JPEG to the client using chunked `FILE_TRANSFER_CHUNK` packets (1024 bytes each). Each chunk is ACKed individually before the next is sent.

---

## How the Client Works

The client is the aircraft terminal. It takes `flight_id` and `callsign` as command-line arguments.

**Startup:** Initialises Winsock, creates a UDP socket, sends `CONNECT`, waits for `CONNECT_ACK`, then fires back `CONNECT_CONFIRM`.

**Position reports:** `PositionReporter` sends a `POSITION_REPORT` every 2 seconds containing latitude, longitude, altitude, speed, and heading.

**Handoff:** When a `HANDOFF_INSTRUCT` arrives, `HandoffHandler` sends `HANDOFF_ACK` to confirm the sector change.

**File receive:** `FileReceiver` collects incoming chunks, ACKs each one, and reassembles them into the complete file once `FILE_TRANSFER_END` is received.

**Logs:** Written to `aerotrack_client_[FLIGHT_ID].log`.

---

## Shared Library (AeroTrackShared)

This is the static lib both the server and client link against.

### Packet

Every message on the wire uses the same 27-byte header:

```
[packet_type | sequence_number | ack_number | timestamp | flight_id | payload_length | checksum]
```

`Packet.cpp` handles serialization and deserialization. `#pragma pack(push, 1)` is used on the header struct to prevent padding — this is documented in-code per MISRA Rule 16-6-1.

### PacketTypes

An enum class defining every packet type in the protocol:

| Range | Purpose |
|---|---|
| `0x01–0x06` | Connection, heartbeat, ACK |
| `0x10–0x11` | Position reports |
| `0x20–0x23` | Handoff sequence |
| `0x30–0x32` | File transfer |
| `0xFF` | Error |

### RUDP

Sits on top of UDP and adds three things: sequence numbers, ACKs, and retransmission. If no ACK arrives within 500ms, the packet is resent. After 3 retries with no response, a connection error is reported. All Winsock calls live in `RUDP.cpp` — no other file touches the socket API directly.

### Logger

Writes timestamped TX/RX entries in ISO 8601 format:

```
2026-03-20 08:15:03.412 | TX | POSITION_REPORT | SEQ:0047 | FLT:AC-801 | SIZE:51 | STATUS:OK
```

---

## Tests (MSTest)

All tests live in `AeroTrackTests` and use the MSTest Native framework (`CppUnitTest.h`). Run them via **Test → Run All Tests** in Visual Studio. There are three tiers.

### Unit Tests

Isolated, no network. Cover packet serialization/deserialization, RUDP sequencing logic, state machine transition validation, logger output format, and connection handling.

### Integration Tests (`IntegrationTests.cpp`)

Two real modules talking to each other — no mocks. Each test is labelled `INT-XXX`.

| Test | What it checks |
|---|---|
| INT-001 | Two RUDP instances exchange a packet over `127.0.0.1` loopback — send, receive, type and flight ID match |
| INT-002 | A `POSITION_REPORT` with a real `PositionPayload` survives a full RUDP send/receive — payload bytes intact, CRC-16 passes |
| INT-003 | Packet + Logger: TX then RX logged in the same file with correct pipe-delimited format |
| INT-004 | RUDP + Logger: Logger captures the TX event written by the RUDP module |
| INT-005 | All header field types (sequence number, flight ID, timestamp, checksum) survive a serialize/deserialize round-trip |
| INT-006 | `PositionPayload` struct values (lat, lon, altitude, speed, heading) are byte-exact after deserialization |
| INT-007 | Two sequential RUDP sends produce strictly monotonic sequence numbers in the log |
| INT-008 | `LogPacket` records the correct wire size for a packet with a non-empty payload |
| INT-010 | All 16 packet type strings appear in a log file after logging one packet of each type |
| INT-011 | `FileTransfer` builds START/CHUNK/END packets; `FileReceiver` handles each directly without a network — state reaches `COMPLETE`, all 3 chunks received, CRC-16 valid on every packet |

> Ports 54400–54401 are used for loopback. If a bind fails (port in use), the test skips with a message rather than failing.

### System Tests (`SystemTests.cpp`)

Full protocol scenarios using real modules end-to-end. Each test is labelled `SYS-XXX`.

| Test | What it covers |
|---|---|
| SYS-001 | Full 3-step handshake over real RUDP loopback: `CONNECT` → `CONNECT_ACK` → `CONNECT_CONFIRM`, CRC-16 validated at each step |
| SYS-002 | Full file transfer: `FileTransfer` builds packets → RUDP sends them → `FileReceiver` reassembles the complete file |
| SYS-003 | Full state machine lifecycle: `IDLE → CONNECTED → TRACKING → HANDOFF_INITIATED → HANDOFF_PENDING → HANDOFF_COMPLETE → TRACKING → LOST_CONTACT` |
| SYS-004 | Complete logged session — all major packet types (`CONNECT` through `DISCONNECT`) written to log, line count matches packet count |
| SYS-005 | 6-packet session over RUDP loopback: every received packet passes CRC-16, sequence numbers are strictly increasing across all 6 |

> Ports 54500–54503 are used for system-level loopback. Same skip-on-bind-failure behaviour as integration tests.

---

## Key Config Values (`Config.h`)

| Constant | Value | Purpose |
|---|---|---|
| `SERVER_IP` | `127.0.0.1` | Loopback for local testing |
| `SERVER_PORT` | `27015` | UDP listen port |
| `RUDP_TIMEOUT_MS` | `500` | Retransmit after 500ms |
| `RUDP_MAX_RETRIES` | `3` | Give up after 3 failures |
| `POSITION_REPORT_INTERVAL_MS` | `2000` | Report every 2s |
| `FILE_CHUNK_SIZE` | `1024` | Bytes per chunk |
| `HANDOFF_TIMEOUT_MS` | `5000` | Handoff ACK deadline |
