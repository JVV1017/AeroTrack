#pragma once
// =============================================================================
// TestCommon.h — Shared includes and using declarations for all AeroTrack
//                MSTest unit test files.
//
// CRITICAL INCLUDE ORDER:
//   1. Winsock (must precede windows.h)
//   2. windows.h
//   3. #undef ERROR  ← MUST be here, before ANY AeroTrack headers
//      (winsock2.h defines ERROR=0 which corrupts PacketType::ERROR enum)
//   4. AeroTrack headers
//   5. MSTest framework
// =============================================================================

// ── Step 1: Windows networking (order matters) ────────────────────────────────
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#include <windows.h>

// ── Step 2: Undefine ERROR macro BEFORE any AeroTrack headers ─────────────────
// winsock2.h / windows.h defines ERROR as 0.
// PacketTypes.h has "ERROR = 0xFFU" inside enum class PacketType.
// If ERROR is still defined here, the enum line becomes "0 = 0xFFU" → syntax error.
#undef ERROR

// ── Step 3: AeroTrack shared headers ─────────────────────────────────────────
#include "Packet.h"
#include "PacketTypes.h"
#include "Logger.h"
#include "RUDP.h"
#include "Config.h"

// ── Step 4: AeroTrack server headers ─────────────────────────────────────────
#include "Statemachine.h"
#include "Flightregistry.h"
#include "Handoffmanager.h"
#include "Server.h"
#include "Filetransfer.h"

// ── Step 5: AeroTrack client headers ─────────────────────────────────────────
#include "Client.h"
#include "PositionReporter.h"
#include "HandoffHandler.h"
#include "FileReceiver.h"

// ── Step 6: MSTest framework ──────────────────────────────────────────────────
#include "CppUnitTest.h"

// ── Step 7: Test helpers ──────────────────────────────────────────────────────
#include "TestHelpers.h"

// ── Step 8: Standard library ──────────────────────────────────────────────────
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <cstring>

// ── MSTest framework ──────────────────────────────────────────────────────────
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// ── AeroTrack types ───────────────────────────────────────────────────────────
// AeroTrack::Logger intentionally excluded — MSTest also has a Logger class.
// Use AeroTrack::Logger explicitly in test code.
using AeroTrack::Packet;
using AeroTrack::PacketType;
using AeroTrack::PacketTypeToString;
using AeroTrack::PacketHeader;
using AeroTrack::PositionPayload;
using AeroTrack::RUDP;
using AeroTrack::Endpoint;
using AeroTrack::StateMachine;
using AeroTrack::FlightState;
using AeroTrack::FlightStateToString;
using AeroTrack::TransitionResult;
using AeroTrack::FlightRegistry;
using AeroTrack::FlightRecord;
using AeroTrack::HandoffManager;
using AeroTrack::SectorDefinition;
using AeroTrack::HandoffAction;
using AeroTrack::HandoffActionType;
using AeroTrack::PendingHandoff;
using AeroTrack::Server;
using AeroTrack::Client;
using AeroTrack::ClientState;
using AeroTrack::PositionReporter;
using AeroTrack::HandoffHandler;
using AeroTrack::FileReceiver;
using AeroTrack::TransferState;
using AeroTrack::FileTransfer;
using AeroTrack::FileTransferStatus;

// ── AeroTrack config constants (all in AeroTrack namespace) ───────────────────
using AeroTrack::SERVER_IP;
using AeroTrack::SERVER_PORT;
using AeroTrack::RUDP_TIMEOUT_MS;
using AeroTrack::RUDP_MAX_RETRIES;
using AeroTrack::HEARTBEAT_INTERVAL_MS;
using AeroTrack::LOST_CONTACT_TIMEOUT_MS;
using AeroTrack::POSITION_REPORT_INTERVAL_MS;
using AeroTrack::FILE_CHUNK_SIZE;
using AeroTrack::HANDOFF_TIMEOUT_MS;
using AeroTrack::SOCKET_RECV_TIMEOUT_MS;