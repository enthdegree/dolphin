// Copyright 2015 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerInterface/Pipes/Pipes.h"

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
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"

namespace ciface::Pipes
{
static const std::array<std::string, 60> s_button_tokens{{
  "A", "B", "1", "2", "-", "+", "Home", // Emulated Wiimote buttons
  "DUp", "DDown", "DLeft", "DRight", 
  "ShakeX", "ShakeY", "ShakeZ", 
  "IRUp", "IRDown", "IRLeft", "IRRight",  
  "AccelUp", "AccelDown", "AccelLeft", "AccelRight", "AccelForward", "AccelBackward", 
  "GyroPitchUp", "GyroPitchDown", 
  "GyroRollLeft", "GyroRollBackward", 
  "GyroYawLeft",  "GyroYawRight",
  "SwingForward", "SwingBackward",
  "NunchukStickUp", "NunchukStickDown", "NunchukStickLeft", "NunchukStickRight",   
  "NunchukShakeX", "NunchukShakeY", "NunchukShakeZ", 
  "NunchukC", "NunchukZ", 
  "X", "Y", "Z", "Start", "L", "R", }}; // GBA buttons
                                                                               
static const std::array<std::string, 10> s_shoulder_tokens{{"L", "R"}};

static const std::array<std::string, 3> s_axis_tokens{{"IR", "MAIN", "C"}};

static double StringToDouble(const std::string& text)
{
  std::istringstream is(text);
  // ignore current locale
  is.imbue(std::locale::classic());
  double result;
  is >> result;
  return result;
}

void PopulateDevices()
{
  // Search the Pipes directory for files that we can open in read-only,
  // non-blocking mode. The device name is the virtual name of the file.
  File::FSTEntry fst;
  std::string dir_path = File::GetUserPath(D_PIPES_IDX);
  if (!File::Exists(dir_path))
    return;
  fst = File::ScanDirectoryTree(dir_path, false);
  if (!fst.isDirectory)
    return;
  for (unsigned int i = 0; i < fst.size; ++i)
  {
    const File::FSTEntry& child = fst.children[i];
    if (child.isDirectory) continue;
    if (child.physicalName.starts_with("emu")) continue; // Don't listen on emulator pipes
    if (child.physicalName.ends_with("_in")) { // Only try to listen on *_in pipes
        int fd_in = open(child.physicalName.c_str(), O_RDONLY | O_NONBLOCK);
        if (fd_in < 0)
          continue;
        std::string str_outname = child.physicalName.substr(0, child.physicalName.size()-3) + "_out";
        int fd_out = open(str_outname.c_str(), O_RDWR);
        if (fd_out < 0)
          continue;
        g_controller_interface.AddDevice(std::make_shared<PipeDevice>(fd_in, fd_out, child.virtualName));
    }
  }
}

PipeDevice::PipeDevice(int fd_in, int fd_out, const std::string& name) : m_fd_in(fd_in), m_fd_out(fd_out), m_name(name)
{
  for (const auto& tok : s_button_tokens)
  {
    PipeInput* btn = new PipeInput("Button " + tok);
    AddInput(btn);
    m_buttons[tok] = btn;
  }
  for (const auto& tok : s_shoulder_tokens)
  {
    AddAxis(tok, 0.0);
  }
  for (const auto& tok : s_axis_tokens)
  {
    AddAxis(tok + " X", 0.5);
    AddAxis(tok + " Y", 0.5);
  }
}

PipeDevice::~PipeDevice()
{
  close(m_fd_in);
  close(m_fd_out);
}

Core::DeviceRemoval PipeDevice::UpdateInput()
{
  // Read any pending characters off the pipe. If we hit a newline,
  // then dequeue a command off the front of m_buf and parse it.
  char buf[32];
  ssize_t bytes_read = read(m_fd_in, buf, sizeof buf);
  while (bytes_read > 0)
  {
    m_buf.append(buf, bytes_read);
    bytes_read = read(m_fd_in, buf, sizeof buf);
  }
  std::size_t newline = m_buf.find("\n");
  while (newline != std::string::npos)
  {
    std::string command = m_buf.substr(0, newline);
    ParseCommand(command);
    m_buf.erase(0, newline + 1);
    newline = m_buf.find("\n");
  }
  return Core::DeviceRemoval::Keep;
}

void PipeDevice::AddAxis(const std::string& name, double value)
{
  // Dolphin uses separate axes for left/right, which complicates things.
  PipeInput* ax_hi = new PipeInput("Axis " + name + " +");
  ax_hi->SetState(value);
  PipeInput* ax_lo = new PipeInput("Axis " + name + " -");
  ax_lo->SetState(value);
  m_axes[name + " +"] = ax_hi;
  m_axes[name + " -"] = ax_lo;
  AddAnalogInputs(ax_lo, ax_hi);
}

void PipeDevice::SetAxis(const std::string& entry, double value)
{
  value = std::clamp(value, 0.0, 1.0);
  double hi = std::max(0.0, value - 0.5) * 2.0;
  double lo = (0.5 - std::min(0.5, value)) * 2.0;
  auto search_hi = m_axes.find(entry + " +");
  if (search_hi != m_axes.end())
    search_hi->second->SetState(hi);
  auto search_lo = m_axes.find(entry + " -");
  if (search_lo != m_axes.end())
    search_lo->second->SetState(lo);
}

void PipeDevice::ParseCommand(const std::string& command)
{
  const std::vector<std::string> tokens = SplitString(command, ' ');
  if (tokens.size() < 2 || tokens.size() > 4)
    return;
  if (tokens[0] == "Press" || tokens[0] == "Release")
  {
    auto search = m_buttons.find(tokens[1]);
    if (search != m_buttons.end())
      search->second->SetState(tokens[0] == "Press" ? 1.0 : 0.0);
  }
  else if (tokens[0] == "Set")
  {
    if (tokens.size() == 3)
    {
      double value = StringToDouble(tokens[2]);
      SetAxis(tokens[1], (value / 2.0) + 0.5);
    }
    else if (tokens.size() == 4)
    {
      double x = StringToDouble(tokens[2]);
      double y = StringToDouble(tokens[3]);
      SetAxis(tokens[1] + " X", x);
      SetAxis(tokens[1] + " Y", y);
    }
  } else {
      return;
  }

  // Publish parse success
  auto fn_publish = [this](void) {write(m_fd_out, "0\n", 2);};
  // write(m_fd_out, (command+"\n").c_str(), command.length()+1);
  ::Core::QueueHostJob(fn_publish, true); 
}
}  // namespace ciface::Pipes
