## Search for a good Wii Play Billiards 
# Not a very good search method! 
# Intended as an example for how to use WiiPlayBilliards.py
import time
import numpy as np
import scipy.optimize as spo
import WiiPlayBilliards as wpb

def sum_distance(vx, vy, vz): # Sum-distance from balls to nearest pocket
    ss = 0
    pp = np.array([[90, 55]]) * np.array([[1,1],[0,1],[-1,1],[1,-1],[0,-1],[-1,-1]]) # Pockets
    for iball in range(9):
        if vy[iball] < 0: continue # Sunk
        mtx_diff = pp-np.array([[vx[iball], vz[iball]]]) # Vector from ball to each pocket 
        ss += np.min(np.linalg.norm(mtx_diff,ord=2,axis=1))**4
    return ss
    
def objective(v, vx, vz, nsunkbest, vbest, nsunk, itr, seed): # Objective function: translate dpad and ir into break result
    t0 = time.time()
    vir = v[0:2]
    vdpad = v[2:4]
    
    # There's a rare race condition in emulator read/write commands.
    # Wrap calls in a try-except
    while True: 
        try vxs, vys, vzs = wpb.shoot(vx, vz, vir, vdpad)
        except Exception as ex: continue
        break
    d = sum_distance(vxs, vys, vzs)
    
    itr[0] += 1
    nsunk[0] = sum(vys<0)
    if(nsunk[0] > nsunkbest):
        nsunkbest[0] = nsunk[0]
        vbest[:] = v[:]
        
    str_ir = f"{np.array2string(vir, formatter={'float': lambda x: f'{x:.4f}'})}"
    str_dpad = f"{np.array2string(vdpad, formatter={'float': lambda x: f'{int(x):3d}'})}"
    print(f"seed {int(seed[0]):10d} tr {int(itr[0]):4d} " + \
            f"IR aim = {str_ir} " + 
            f"dpad = {str_dpad} " + 
            f"sank {int(nsunk[0])}, " +
            f"d = {d:.4e}, " + 
            f"({time.time()-t0:.4f} s; best {int(nsunkbest[0])})")
    
    # Save best each time in case something goes wrong
    np.savetxt(str_search, np.concatenate((vbest,vx,vz,seed)), delimiter=',')
    return d

# Trial settings
xlim = [0.4995, 0.5000] # [0.470, 0.530]
zlim = [0.7400, 0.7600] # [0.650, 0.850]
vdpadx = np.array(range(-10, 10))
vdpadz = np.array(range(0, 61))

# Main 
nsunkbest = np.zeros(1)
str_search = "search.csv"
vbest = np.zeros(4)
itr = np.zeros(1)
nsunk = np.zeros(1)

vbounds = np.array([
    [0.47,0.53], # IR X
    [0.6, 0.9], # IR Y
    [-30,30], # Dpad left/right
    [0,60] # Dpad up
])
while True:
    seed = np.array([np.random.randint(0,2**32)]) # Get a random seed

    # There's a rare race condition in emulator read/write commands.
    # Wrap calls in a try-except
    vx = []
    vz = []
    while True:
        try: vx, vz = wpb.get_coors_for_seed(int(seed[0])) 
        except Exception as ex: continue
        break
    x0 = np.array([ # Get a random initial guess
        np.random.uniform(vbounds[0,0], vbounds[0,1]), 
        np.random.uniform(vbounds[1,0], vbounds[1,1]), 
        np.random.randint(vbounds[2,0], vbounds[2,1]+1), 
        np.random.randint(vbounds[3,0], vbounds[3,1]+1), 
    ])
    fn_obj = lambda v: objective(v, vx, vz, nsunkbest, vbest, nsunk, itr, seed)
    ff = fn_obj(x0)
    if(nsunk < 4): continue # Find a hopeful starting point before annealing 
    vopt = spo.dual_annealing(
        func=fn_obj, 
        x0=x0,
        bounds=vbounds, 
        maxfun=400,
        no_local_search=True)

