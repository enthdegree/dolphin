## Some rudimentary routines for interfacing Wii Play Biliards via Python
# Expects a few specific save slots in Dolphin. 
# You will have to make these save states yourself:
# - Slot 2: If Wii Play Billiards is paused, loading this state should set the game to right before Billiards performs its first RNG read.
# - Slot 3: Loading this state should set the game to a state where it's ready to take controller input for the break shot.

import time
import struct
import numpy as np
from threading import Thread
from queue import Queue, Empty

# Control settings
timeout_s = 500e-3
pipes_dir = "../Build/Binaries/user/Pipes/"
fname_emu_in = pipes_dir + "emu_in"
fname_emu_out = pipes_dir + "emu_out"
fname_wiimote_in = pipes_dir + "wiimote_in"
fname_wiimote_out = pipes_dir + "wiimote_out"

# Addresses of interest 
sunk_addr = ["1B4CF7B", "1B4DDDB", "1B4D6AB", "1B4E50B", "1B4EC3B", "1B4F36B", "1B4FA9B", "1B501CB", "1B508FB"] # Mem2
x_addr =    ["1B4D07C", "1B4D7AC", "1B4DEDC", "1B4E60C", "1B4ED3C", "1B4F46C", "1B4FB9C", "1B502CC", "1B509FC"] # Mem2
y_addr =    ["1B4D080", "1B4D7B0", "1B4DEE0", "1B4E610", "1B4ED40", "1B4F470", "1B4FBA0", "1B502D0", "1B50A00"] # Mem2
z_addr =    ["1B4D084", "1B4D7B4", "1B4DEE4", "1B4E614", "1B4ED44", "1B4F474", "1B4FBA4", "1B502D4", "1B50A04"] # Mem2
bp_instr =  ["802c1db4", "802c1df4"] # Instruction address of random ball perturbations [x,z]

# Wiimote and emulator fifo read/write setup
def enqueue_output(fifo, queue):
    for line in iter(fifo.readline, b""):
        queue.put(line)
    fifo.close()

def clear_old(queue): # Populate old stuff in queue, clear it out 
    time.sleep(30e-3) 
    while not queue.empty(): queue.get_nowait()

q_emu = Queue()
fifo_emu_out = open(fname_emu_out)
th_emu = Thread(target=enqueue_output, args=(fifo_emu_out, q_emu))
th_emu.daemon = True # thread dies with the program
th_emu.start()
clear_old(q_emu)
print("Emu connected")

q_wiimote = Queue()
fifo_wiimote_out = open(fname_wiimote_out)
th_wiimote = Thread(target=enqueue_output, args=(fifo_wiimote_out, q_wiimote))
th_wiimote.daemon = True # thread dies with the program
th_wiimote.start()
clear_old(q_wiimote)
print("Wiimote connected")

# Basic Python interface 
def emu(cmd): # Send a command to the emulator
    try:
        tok_in = cmd.split(" ")
        fifo_in = open(fname_emu_in,"w")
        fifo_in.write(cmd + "\n")
        fifo_in.close()
        out = q_emu.get(timeout = timeout_s) 
    except Empty:
        raise Exception("emu ate command: " + cmd) 
            
    bad_emu = False # Check that the output corresponds to the command issued
    out = out.replace("\n","")
    tok_out = out.split(" ")
    for itok in range(len(tok_in)):
        if (len(tok_out) <= len(tok_in)) | (not (tok_in[itok] == tok_out[itok])): 
            bad_emu = True
            break
    if bad_emu: 
        raise Exception("emu out doesn't correspond to command: " + cmd + " " + out)
    return tok_out[-1]

def wiimote(cmd, paused=True): # Send a gesture to the wiimote, verify it was received
    fifo_in = open(fname_wiimote_in,"w")
    fifo_in.write(cmd + "\n") 
    fifo_in.close() 
    if paused: emu("UpdateInput")
    try: q_wiimote.get(timeout = timeout_s) 
    except Empty: 
        if not paused: raise Exception("wiimote ate command")
    return

# Conversion functions
def double_to_wiihex(val): # Convert a double to a f32 for the wii CPU float register
    ba_val = bytearray(struct.pack("f", float(val)))  
    str_hex = "".join("{:02x}".format(x) for x in ba_val)
    return str_hex[:8] + "00000000"

def readmem2(reg, nbytes, asFloat=True): # Read n contiguous bytes of wii mem2
    reg_int = int(reg, 16)
    val_hex = ""
    for ibyte in range(nbytes): 
        val_hex += emu(f"ReadMemory mem2 {reg_int+ibyte:08x}") 
    if asFloat: return struct.unpack('!f', bytes.fromhex(val_hex))[0]
    else: return val_hex
    
def writemem2f32(reg, val): # Write f32 into wii mem2
    val_hex = hex(struct.unpack('<I', struct.pack('<f', val))[0])
    val_hex = val_hex[2:]
    reg_int = int(reg,16)
    for ibyte in range(4): 
        emu(f"WriteMemory mem2 {reg_int+ibyte:08x} {val_hex[(2*ibyte):(2*ibyte+2)]}") 
    return 
    
def tap_dpad(vd): # Tap dpad left right up down n frames in a row
    if vd[0] < 0: xstr = "DLeft"
    else: xstr = "DRight"
    for iframe in range(int(abs(vd[0]))):
        wiimote("Press " + xstr)
        emu("FrameAdvance")
        wiimote("Release " + xstr)

    if vd[1] < 0: ystr = "DDown"
    else: ystr = "DUp"
    for iframe in range(int(abs(vd[1]))):
        wiimote("Press " + ystr)
        emu("FrameAdvance")
        wiimote("Release " + ystr)
    return 

# Higher-level Wii Play Billiards routines
def get_coors_for_seed(u32_seed=0): # Get ball positions for a given rng seed. Expects no Dolphin breakpoints enabled
    while emu("IsPaused") == "0": emu("Pause")
    emu("LoadSlot 2") # While paused, load a game that's paused exactly before the first billiards RNG read
    time.sleep(100e-3)
    reg_int = int('8043c620',16) # Register in "Eff" that contains the RNG seed 
    seed_hex = f"{u32_seed:08x}"
    for ibyte in range(4): # Inject seed
        emu(f"WriteMemory eff {(reg_int+ibyte):08x} {seed_hex[(2*ibyte):(2*ibyte+2)]}")
    for iframe in range(10): emu("FrameAdvance") # Gen ball coordinates

    vx = np.zeros(9)
    vz = np.zeros(9)
    for iball in range(9):
        vx[iball] = readmem2(x_addr[iball],4)
        vz[iball] = readmem2(z_addr[iball],4)
    return vx, vz

def set_balls(vx, vz): # Set a cue with specified ball offsets
    while emu("IsPaused") == "0": emu("Pause")
    emu("LoadSlot 3") # Load save slot at shot time
    for iball in range(9):
        writemem2f32(x_addr[iball],vx[iball])
        writemem2f32(z_addr[iball],vz[iball])
    return
    
def shoot(vx, vz, vir, vdpad): # Play billiards with a specific setup
    # vx, vz = 9 floats, table positions; offset between +/-0.143925.
    # vir = 2 floats; ir pointer [x, y]; elements between 0 and 1
    # vdpad = 2 ints; dpad aim [left/right, up/down]; (sign=direction)
    
    set_balls(vx, vz)
    wiimote("Press Home"), emu("FrameAdvance"), wiimote("Release Home")
    wiimote(f"Set IR {vir[0]:.10f} {vir[1]:.10f}") # Aim IR pointer in home for less dynamics
    for iframe in range(100): emu("FrameAdvance") 
    wiimote("Press Home"), emu("FrameAdvance"), wiimote("Release Home")
    for iframe in range(100): emu("FrameAdvance") 

    tap_dpad(vdpad)
    wiimote("Press B")
    wiimote("Press SwingBackward")
    for iframe in range(20): emu("FrameAdvance") # Wind back
    wiimote("Release SwingBackward")
    wiimote("Press SwingForward")
    for iframe in range(20): emu("FrameAdvance") # Shoot forward
    wiimote("Release SwingForward")
    wiimote("Release B")

    #for iframe in range(800): emu("FrameAdvance") # Settle table
    emu("Resume"), time.sleep(4000e-3) # Settle table

    # Read shot outcome
    vx_shot = np.zeros(9)
    vy_shot = np.zeros(9)
    vz_shot = np.zeros(9)
    for iball in range(9):
        str_sunk = readmem2(sunk_addr[iball],1, asFloat=False)
        b_sunk = (int) (str_sunk == "01")
        vx_shot[iball] = readmem2(x_addr[iball],4)
        vy_shot[iball] = readmem2(y_addr[iball],4)
        vz_shot[iball] = readmem2(z_addr[iball],4)
    return vx_shot, vy_shot, vz_shot

