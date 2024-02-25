// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "Core/HW/AddressSpace.h"

// EmuPipes provides a FIFO pipe interface to emulator controls.
// It runs in the Host main loop, reading `Pipes/emu_in`
// and printing data to the fifo `Pipes/emu_out`
namespace EmuPipes 
{
    class EmuPipes {
    public:
        EmuPipes();
        ~EmuPipes();
        
        static int fd_emu_in;
        static int fd_emu_out;
        static std::string cmdbuf;

        // stores for arguments to emulator controls (the functions below)
        // We have to do it this way because their function signatures must be
        // (void *)(void)
        static AddressSpace::Type memtype;
        static u32 memaddr;
        static u8 memval;
        static int loadslot;
        static u8 cpufreg_idx;
        static u8 cpufreg_slot;
        static u64 cpufreg_val;
        static int pipe_init;

        static void ReadPipe(void); // To be run each time Host loops
        static void ParseCommand(std::string& cmd); 
        static std::string u8tohex(u8 val); 
        static std::string u64tohex(u64 val); 

        // To be run on Host thread
        static void GetPauseState(void);
        static void TogglePause(void);
        static void FrameAdvance(void);
        static void LoadState(void); // Reads loadslot
        static void ReadMemory(void); // Reads memtype, memaddr
        static void WriteMemory(void); // Reads memtype, memaddr, memval
        //static void AddMemBreakpoint(void); // Reads memtype, memaddr 

        // To be run on CPU thread
        static void ReadCPUFReg(void); // Reads cpufreg_idx, cpufreg_slot
        static void WriteCPUFReg(void); // Reads cpufreg_idx, cpufreg_slot
};
}