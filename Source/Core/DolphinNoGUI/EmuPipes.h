// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <chrono>

#include "Core/HW/AddressSpace.h"

// EmuPipes provides a FIFO pipe interface to emulator controls.
// It runs in the Host main loop, reading `Pipes/emu_in`
// and printing data to the fifo `Pipes/emu_out`
using hrc_time = std::chrono::time_point<std::chrono::high_resolution_clock>;

namespace EmuPipes 
{
    class EmuPipes {
    public:
        static int fd_emu_in;
        static int fd_emu_out;
        static std::string str_emu_in;
        static std::string cmdbuf;
        static std::string str_out;

        // stores for arguments to emulator controls (the functions below)
        // We have to do it this way because their function signatures must be
        // (void *)(void)
        static hrc_time t_last;
        static AddressSpace::Type memtype;
        static u32 memaddr;
        static u8 memval;
        static int loadslot_idx;
        static u8 cpufreg_idx;
        static u8 cpufreg_slot;
        static u64 cpufreg_val;
        static int pipe_init;

        static void InitPipes(void);
        static void ClosePipes(void);
        static void ReadPipe(void); // To be run each time Host loops
        
        static void ParseCommand(std::string& cmd); 
        static void HandleParseFail(void);
        static void HandleParseSuccess(void);

        static std::string u8tohex(u8 val); 
        static std::string u64tohex(u64 val); 
        static int strtoint(std::string str_int); 
        static u32 hextou32(std::string str_hex);
        static u8 hextou8(std::string str_hex);

        // To be run on Host thread
        static void TogglePause(void);
        static void GetPauseState(void);
        static void FrameAdvance(void);
        static void LoadSlot(void); // Reads loadslot
        static void ReadMemory(void); // Reads memtype, memaddr
        static void WriteMemory(void); // Reads memtype, memaddr, memval

        // To be run on CPU thread
        static void AddMemBreakpoint(void); // Reads memaddr 
        static void ReadCPUFReg(void); // Reads cpufreg_idx, cpufreg_slot
        static void WriteCPUFReg(void); // Reads cpufreg_idx, cpufreg_slot
};
}
