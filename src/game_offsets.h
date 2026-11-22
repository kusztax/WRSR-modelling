// game_offsets.h - structure offsets inside SOVIET64.exe objects.
//
// DATA, not code addresses: these did not move between 1.1.1.7 and 1.1.1.9
// (source: RailPhysics by TheRealMeowMeow00, GPL-3.0, docs/findings/*, and
// railphysics.cpp). Every one is UNVERIFIED on 1.1.1.13 until the census in
// rail_probe.cpp has logged sane values for it - see docs/04-m1-census.md.
#pragma once

#include <cstdint>

namespace wrsr::off {

// ---- vehicle instance (a train is its lead vehicle) ----
constexpr uintptr_t V_LEADER     = 0x398;   // wagon: the vehicle pulling it; 0 on the lead
constexpr uintptr_t V_WAGON_VEC  = 0x3A0;   // {begin,end} of vehicle* - the consist
constexpr uintptr_t V_REVERSED   = 0x4C4;   // u8: car coupled back to front
constexpr uintptr_t V_PASS_ALL   = 0x5E0;   // u8: non-zero -> the signal check always says go (meaning unknown)
constexpr uintptr_t V_ROUTE_NODES = 0x6A0;  // {begin,end} of node* along the planned way
constexpr uintptr_t V_ROUTE_INDEX = 0x700;  // int: index of the node the train heads for
constexpr uintptr_t V_CHAIN_AT   = 0x4F0;   // chain the train has ARRIVED at
constexpr uintptr_t V_CHAIN_NEXT = 0x528;   // chain the train is engaged with / approaching
constexpr uintptr_t V_DIR        = 0x7A0;   // u8: direction on the current segment
constexpr uintptr_t V_SPEED      = 0xD24;   // float km/h
constexpr uintptr_t V_TARGET     = 0xD28;   // float km/h, effective target speed
constexpr uintptr_t V_TYPE       = 0x1708;  // -> vehicle type
constexpr uintptr_t V_INACTIVE   = 0x1794;  // int: wagon excluded when non-zero

// ---- vehicle type ----
constexpr uintptr_t T_CATEGORY   = 0x294;   // int: 3 wagon, 4 locomotive, 5 service
constexpr uintptr_t T_TRANSPORT  = 0x8600;  // int: transport class, 7 = passengers
constexpr uintptr_t T_TOPSPEED   = 0x8624;  // float km/h

constexpr int CAT_WAGON = 3, CAT_LOCO = 4, CAT_SERVICE = 5;
constexpr int TRANSPORT_PASSENGERS = 7;

// ---- chain (block section / station / any rail "building") ----
constexpr uintptr_t C_TYPE_DESC  = 0x318;   // -> descriptor; desc+0x360 = type id
constexpr uintptr_t D_TYPE_ID    = 0x360;
constexpr uintptr_t C_ENGAGED_AT = 0x604;   // float: distance of the train engaging it
constexpr uintptr_t C_VEHICLES   = 0xC70;   // {begin,end} vehicles present

constexpr int CHAIN_TYPE_PLAIN_BLOCK = 300; // inferred by RailPhysics: track-only section

// ---- track node / signal (source: Rail Operations by pautov-ya, GPL-3.0,
//      railreverse.cpp; their signatures are tested on 1.1.1.9 and 1.1.1.13) ----
constexpr uintptr_t N_SIGNAL     = 0xB8;    // node -> signal object, or null
constexpr uintptr_t S_KIND       = 0x14;    // int: 10 = no signal
constexpr uintptr_t S_FREE_A     = 0x1A8;   // u8: block flag, 1 = free
constexpr uintptr_t S_FREE_B     = 0x1A9;   // u8: block flag, 1 = free
constexpr uintptr_t S_CHAIN      = 0x1AA;   // u8: 1..3 = chain signal
constexpr int SIGNAL_KIND_NONE = 10;

}  // namespace wrsr::off
