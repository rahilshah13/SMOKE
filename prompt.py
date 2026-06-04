import sys
import os 

# Render Immuteables
RESOLUTION = [100000, 100000, 100000, 4]
FPS, SEC = 60, int(sys.argv[2])
CAMERA = (0, 30, 30, -0.65, -0.65)

# Scene Immuteables

# (label, centroid, start tick)
OBJECTS = [ 
    ("floor", (0,0,0), 0), 
    ("paper", (0,0, 10), 0), 
    ("substance_1", (0,0,0), 0), 
    ("flame", (0,0,0), 3)
]

# Scene Muteables

_NODES = {{
    obj[0]: { 
        "centroid": obj[1],
        "mass": 0,
        "density": lambda x: x,
        "ticks": { 0: {"update": lambda x: x}}
        "ppm": []*RESOLUTION
    },
} for obj in OBJECTS }

# a list of key[object:tick] deltas 
_EDGES = [{
    "label": "",
    "tick": 0,
    "energy"
    "dm":0,
    "dx": 0, 
    "dy": 0, 
    "dz": 0, 
    "dt": 0, 
    "dp": 0, 
    "d2x": 0,
    "d2y": 0,
    "d2z": 0,
    "d2theta": 0,
    "d2phi": 0,
}]


# OBJ DENSITY TO .PPM FUNCTIONS
# 


render():
    FRAME = [RESOLUTION for _ in range(FPS * SEC)]
    for E in _EDGES:
        FRAME[E.tick] = _NODES[E.label].ticks[E.tick]["update"](FRAME[E.tick])
    # write FRAME(.ppm[]) to mp4
    return 
     
'''
1. build immutable objects (Nodes)
2. serialize frames
    - apply transformations, including gravity
3. raytracer to RENDER frames + viewing angle to a video
'''