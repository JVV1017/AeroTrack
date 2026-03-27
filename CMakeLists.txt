cmake_minimum_required(VERSION 3.20)
project(AeroTrack CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(AeroTrackShared
    AeroTrackShared/Logger.cpp
    AeroTrackShared/Packet.cpp
    AeroTrackShared/PacketTypes.cpp
    AeroTrackShared/RUDP.cpp
)
target_include_directories(AeroTrackShared PUBLIC AeroTrackShared)

add_executable(AeroTrackServer
    AeroTrackServer/temp.cpp
)
target_link_libraries(AeroTrackServer PRIVATE AeroTrackShared)

add_executable(AeroTrackClient
    AeroTrackClient/temp.cpp
)
target_link_libraries(AeroTrackClient PRIVATE AeroTrackShared)