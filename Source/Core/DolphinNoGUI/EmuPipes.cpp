// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "EmuPipes.h"

#include <algorithm>
#include <chrono>
#include <array>
#include <cstdlib>
#include <fcntl.h>
#include <iostream>
#include <locale>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "Core/Core.h"
#include "Core/State.h"
#include "Core/System.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/PowerPC/BreakPoints.h"

#include "Common/FileUtil.h"
#include "Common/StringUtil.h"

#define FIFO_DELAY_US 500

namespace EmuPipes
{
    int EmuPipes::fd_emu_in;
    int EmuPipes::fd_emu_out;
    std::string EmuPipes::cmdbuf;
    std::string EmuPipes::str_out;
    std::string EmuPipes::str_emu_in;
    AddressSpace::Type EmuPipes::memtype = AddressSpace::Type::Effective;

    hrc_time EmuPipes::t_last = std::chrono::high_resolution_clock::now();
    u32 EmuPipes::memaddr = 0;
    u8 EmuPipes::memval = 0;
    int EmuPipes::loadslot_idx = 1;
    u8 EmuPipes::cpufreg_idx = 0;
    u8 EmuPipes::cpufreg_slot = 0;
    u64 EmuPipes::cpufreg_val = 0;
    int EmuPipes::pipe_init = 0;

EmuPipes::EmuPipes()
{
    std::string dir_path = File::GetUserPath(D_PIPES_IDX);
    str_emu_in = dir_path + "/emu_in";
    fd_emu_in = open(str_emu_in.c_str(), O_RDWR | O_NONBLOCK);
    fd_emu_out = open((dir_path + "/emu_out").c_str(), O_RDWR);
    if((fd_emu_in < 0) | (fd_emu_out < 0)) {
        pipe_init = 0;
        std::cout << "Warning: Couldn't open FIFOs `emu_in`, `emu_out` in " << dir_path << "\n";
        std::cout.flush();
    }
    else {
        pipe_init = 1;
    }
}

EmuPipes::~EmuPipes() {
    close(fd_emu_in);
    close(fd_emu_out);
    pipe_init = 0;
    return;
}

void EmuPipes::ReadPipe(void) {
    if(pipe_init == 0) return; // Give up unless the pipes are good

    // Read pending characters off the pipe.
    char buf[PIPE_BUF];
    ssize_t bytes_read = read(fd_emu_in, buf, sizeof buf);
    while(bytes_read < 0) {
        close(fd_emu_in);
        fd_emu_in = open(str_emu_in.c_str(), O_RDWR | O_NONBLOCK);
        bytes_read = read(fd_emu_in, buf, sizeof buf);
    }
    while (bytes_read > 0)
    {
        cmdbuf.append(buf, bytes_read);
        bytes_read = read(fd_emu_in, buf, sizeof buf);
    }

    hrc_time t_now = std::chrono::high_resolution_clock::now();
    auto t_diff = (std::chrono::duration_cast<std::chrono::microseconds>(t_now-t_last)).count();

    std::size_t newline = cmdbuf.find("\n");
    if ((t_diff > FIFO_DELAY_US) & // Rate-limit fifo input
        (newline != std::string::npos)) {
        std::string cmd = cmdbuf.substr(0, newline);
        ParseCommand(cmd);
        
        cmdbuf.erase(0, newline+1);
        newline = cmdbuf.find("\n");
        t_last = t_now;
    }
}

void EmuPipes::ParseCommand(std::string& cmd) {
    str_out = cmd + " ";
    std::vector<std::string> tokens = SplitString(cmd, ' ');
    
    if(tokens.size() == 0) { 
        HandleParseFail(); 
        return; 
    }
    else if(tokens[0] == "TogglePause") { 
        ::Core::QueueHostJob(EmuPipes::TogglePause, true); 
        return; 
    }
    else if(tokens[0] == "GetPauseState") { 
        ::Core::QueueHostJob(EmuPipes::GetPauseState, true); 
        return; 
    }
    else if(tokens[0] == "FrameAdvance") { 
        ::Core::QueueHostJob(EmuPipes::FrameAdvance, true); 
        return; 
    }
    else if(tokens[0] == "LoadSlot") { 
        if(tokens.size() < 2) {
            HandleParseFail();
            return;
        }
        loadslot_idx = strtoint(tokens[1]);
        if((loadslot_idx < 1) | (loadslot_idx > 10)) {
            HandleParseFail();
            return;
        }
        ::Core::QueueHostJob(EmuPipes::LoadSlot, true); 
        return;
    }
    else if((tokens[0] == "ReadMemory") | (tokens[0] == "WriteMemory")) { 
        bool is_read = (tokens[0] == "ReadMemory");
        if((is_read & (tokens.size() != 3)) | 
          (!is_read & (tokens.size() != 4))) {
            HandleParseFail();
            return;
        }

        if(tokens[1] == "eff") { memtype = AddressSpace::Type::Effective; }
        else if(tokens[1] == "aux") { memtype = AddressSpace::Type::Auxiliary; }
        else if(tokens[1] == "phy") { memtype = AddressSpace::Type::Physical; }
        else if(tokens[1] == "mem1") { memtype = AddressSpace::Type::Mem1; }
        else if(tokens[1] == "mem2") { memtype = AddressSpace::Type::Mem2; }
        else if(tokens[1] == "fake") { memtype = AddressSpace::Type::Fake; }
        else {
            HandleParseFail();
            return;
        }

        memaddr = hextou32(tokens[2]);
        if(is_read) {
            ::Core::QueueHostJob(EmuPipes::ReadMemory, true); 
            return;
        } else {
            memval = hextou8(tokens[3]);
            ::Core::QueueHostJob(EmuPipes::WriteMemory, true); 
            return;
        }
    }
    else if(tokens[0] == "AddMemBreakpoint") { 
        if(tokens.size() < 2) {
            HandleParseFail();
            return;
        }
        memaddr = hextou32(tokens[1]);
        ::Core::RunOnCPUThread(EmuPipes::WriteMemory, true); 
        return;
    }
    else if((tokens[0] == "ReadCPUFReg") | (tokens[0] == "WriteCPUFReg")) { 
        bool is_read = (tokens[0] == "ReadCPUFReg");
        if((is_read & (tokens.size() != 3)) |
           (!is_read & (tokens.size() != 4))) {
            HandleParseFail();
            return;
        }
        cpufreg_idx = hextou32(tokens[1]);
        cpufreg_slot = hextou32(tokens[2]);
        if((cpufreg_idx < 1) | (cpufreg_idx > 31) | 
        !((cpufreg_slot == 0) | (cpufreg_slot == 1))) {
            HandleParseFail();
            return;
        }
        if(is_read) {
            ::Core::RunOnCPUThread(EmuPipes::ReadCPUFReg, true); 
            return;
        }
        else {
            cpufreg_val = hextou32(tokens[3]);
            ::Core::RunOnCPUThread(EmuPipes::WriteCPUFReg, true); 
            return;
        }
    }
    HandleParseFail();
    return;
}

void EmuPipes::HandleParseFail(void) {
    str_out += "-1";
    str_out += "\n";
    write(fd_emu_out, str_out.c_str(), str_out.length());
    std::cout << str_out; 
    std::cout.flush();
    str_out = "";
    return; 
}

void EmuPipes::HandleParseSuccess(void) {
    str_out += "\n";
    write(fd_emu_out, str_out.c_str(), str_out.length());
    std::cout << str_out; 
    std::cout.flush();
    str_out = "";
    return;
}

std::string EmuPipes::u8tohex(u8 val) {
    std::stringstream is;
    is.imbue(std::locale::classic());
    is << std::hex << (int) val;
    return is.str();
}

int EmuPipes::strtoint(std::string str_int) {
    std::stringstream is;
    is.imbue(std::locale::classic());
    is << str_int;
    int val;
    is >> val;
    return val;
}

std::string EmuPipes::u64tohex(u64 val) {
    std::stringstream is;
    is.imbue(std::locale::classic());
    is << std::hex << val;
    return is.str();
}

u32 EmuPipes::hextou32(std::string str_hex) {
    std::stringstream is;
    u32 val;
    is.imbue(std::locale::classic());
    is << std::hex << str_hex;
    is >> val;
    return val;
}

u8 EmuPipes::hextou8(std::string str_hex) {
    std::stringstream is;
    u8 val;
    is.imbue(std::locale::classic());
    is << std::hex << str_hex;
    is >> val;
    return val;
}

void EmuPipes::TogglePause(void) {
    ::Core::State theState = ::Core::GetState();
    if(theState == ::Core::State::Paused) 
        ::Core::SetState(::Core::State::Running, true); 
    else
        ::Core::SetState(::Core::State::Paused, true); 
    str_out += "0";
    HandleParseSuccess();
}

void EmuPipes::GetPauseState(void) {
    int is_paused = (::Core::GetState() == ::Core::State::Paused);
    std::string str_is_paused = std::to_string(is_paused);
    str_out += str_is_paused;
    HandleParseSuccess();
}

void EmuPipes::FrameAdvance(void) {
    ::Core::DoFrameStep(); 
    str_out += "0";
    HandleParseSuccess();
}

void EmuPipes::LoadSlot(void) {
    ::State::Load(loadslot_idx);
    str_out += "0";
    HandleParseSuccess();
}

void EmuPipes::ReadMemory(void) {
    AddressSpace::Accessors* accessors = AddressSpace::GetAccessors(memtype);
    ::Core::CPUThreadGuard guard(::Core::System::GetInstance());
    u8 val = accessors->ReadU8(guard, memaddr);
    std::string val_str = u8tohex(val);
    str_out += val_str;
    HandleParseSuccess();
} 

void EmuPipes::WriteMemory(void) {
    AddressSpace::Accessors* accessors = AddressSpace::GetAccessors(memtype);
    ::Core::CPUThreadGuard guard(::Core::System::GetInstance());
    accessors->WriteU8(guard, memaddr, memval);
    str_out += "0";
    HandleParseSuccess();
}

void EmuPipes::AddMemBreakpoint(void) {
    ::Core::System& m_system = ::Core::System::GetInstance();
    BreakPoints& bp = m_system.GetPowerPC().GetBreakPoints();
    bp.Add(memaddr);
    str_out += "0";
    HandleParseSuccess();
}

void EmuPipes::ReadCPUFReg(void) {
    ::Core::System& m_system = ::Core::System::GetInstance();
    u64 val;
    if(cpufreg_slot == 0) 
        val = m_system.GetPPCState().ps[cpufreg_idx].PS0AsU64();
    else
        val = m_system.GetPPCState().ps[cpufreg_idx].PS1AsU64();
    std::string str_val = u64tohex(val);
    str_out += str_val;
    HandleParseSuccess();
}

void EmuPipes::WriteCPUFReg(void) {
    ::Core::System& m_system = ::Core::System::GetInstance();
    if(cpufreg_slot == 0) 
        m_system.GetPPCState().ps[cpufreg_idx].SetPS0(cpufreg_val);
    else
        m_system.GetPPCState().ps[cpufreg_idx].SetPS1(cpufreg_val);
    str_out += "0";
    HandleParseSuccess();
} 
}