// Copyright 2024 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "EmuPipes.h"

#include <algorithm>
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

#include "Common/FileUtil.h"
#include "Common/StringUtil.h"

namespace EmuPipes
{
    int EmuPipes::fd_emu_in;
    int EmuPipes::fd_emu_out;
    std::string EmuPipes::cmdbuf;
    AddressSpace::Type EmuPipes::memtype;
    u32 EmuPipes::memaddr;
    u8 EmuPipes::memval;
    int EmuPipes::loadslot;
    u8 EmuPipes::cpufreg_idx;
    u8 EmuPipes::cpufreg_slot;
    u64 EmuPipes::cpufreg_val;
    int EmuPipes::pipe_init = 0;

EmuPipes::EmuPipes()
{
    std::string dir_path = File::GetUserPath(D_PIPES_IDX);
    fd_emu_in = open((dir_path + "/emu_in").c_str(), O_RDONLY | O_NONBLOCK);
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

    // Read any pending characters off the pipe. If we hit a newline,
    // then dequeue a command off the front of cmd and parse it.
    char buf[32];
    ssize_t bytes_read = read(fd_emu_in, buf, sizeof buf);
    while (bytes_read > 0)
    {
        cmdbuf.append(buf, bytes_read);
        bytes_read = read(fd_emu_in, buf, sizeof buf);
    }
    std::size_t newline = cmdbuf.find("\n");
    while (newline != std::string::npos)
    {
        std::string cmd = cmdbuf.substr(0, newline);
        ParseCommand(cmd);
        cmdbuf.erase(0, newline + 1);
        newline = cmdbuf.find("\n");
    }
}

void EmuPipes::ParseCommand(std::string& cmd) {
    std::vector<std::string> tokens = SplitString(cmd, ' ');
    if(tokens[0].length() > 0) {
        ::Core::QueueHostJob(EmuPipes::TogglePause);
    }
}

std::string EmuPipes::u8tohex(u8 val) {
    std::stringstream is;
    is.imbue(std::locale::classic());
    is << std::hex << val;
    return is.str();
}

std::string EmuPipes::u64tohex(u64 val) {
    std::stringstream is;
    is.imbue(std::locale::classic());
    is << std::hex << val;
    return is.str();
}

void EmuPipes::TogglePause(void) {
    ::Core::State theState = ::Core::GetState();
    if(theState == ::Core::State::Paused) 
        ::Core::SetState(::Core::State::Running, false); 
    else
        ::Core::SetState(::Core::State::Paused, false); 
    write(fd_emu_out, "0\n", 2);
}

void EmuPipes::GetPauseState(void) {
    int is_paused = (::Core::GetState() == ::Core::State::Paused);
    std::string str_is_paused = std::to_string(is_paused);
    write(fd_emu_out, (str_is_paused+"\n").c_str(), str_is_paused.length()+1); 
}

void EmuPipes::FrameAdvance(void) {
    ::Core::DoFrameStep(); 
    write(fd_emu_out, "0\n", 2); 
}

void EmuPipes::LoadState(void) {
    ::State::Load(loadslot);
    write(fd_emu_out, "0\n", 2); 
}

void EmuPipes::ReadMemory(void) {
    /*
    if(tokens[1] == "eff") { memtype = AddressSpace::Type::Effective; }
    else if(tokens[1] == "aux") { memtype = AddressSpace::Type::Auxiliary; }
    else if(tokens[1] == "phy") { memtype = AddressSpace::Type::Physical; }
    else if(tokens[1] == "mem1") { memtype = AddressSpace::Type::Mem1; }
    else if(tokens[1] == "mem2") { memtype = AddressSpace::Type::Mem2; }
    else if(tokens[1] == "fake") { memtype = AddressSpace::Type::Fake; }
    else return;
    */
    AddressSpace::Accessors* accessors = AddressSpace::GetAccessors(memtype);
    ::Core::CPUThreadGuard guard(::Core::System::GetInstance());
    u8 val = accessors->ReadU8(guard, memaddr);
    std::string val_str = u8tohex(val);
    write(fd_emu_out, (val_str+"\n").c_str(), val_str.length()+1);
} 

void EmuPipes::WriteMemory(void) {
    AddressSpace::Accessors* accessors = AddressSpace::GetAccessors(memtype);
    ::Core::CPUThreadGuard guard(::Core::System::GetInstance());
    accessors->WriteU8(guard, memaddr, memval);
    write(fd_emu_out, "0\n", 2); 
}

void EmuPipes::ReadCPUFReg(void) {
    ::Core::System& m_system = ::Core::System::GetInstance();
    u64 val;
    if(cpufreg_slot == 0) 
        val = m_system.GetPPCState().ps[cpufreg_idx].PS0AsU64();
    else
        val = m_system.GetPPCState().ps[cpufreg_idx].PS1AsU64();
    std::string str_val = u64tohex(val);
    write(fd_emu_out, (str_val+"\n").c_str(), str_val.length()+1); 
}

void EmuPipes::WriteCPUFReg(void) {
    ::Core::System& m_system = ::Core::System::GetInstance();
    if(cpufreg_slot == 0) 
        m_system.GetPPCState().ps[cpufreg_idx].SetPS0(cpufreg_val);
    else
        m_system.GetPPCState().ps[cpufreg_idx].SetPS1(cpufreg_val);
    write(fd_emu_out, "0\n", 2); 
} 
}