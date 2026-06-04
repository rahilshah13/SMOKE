import sys
import os
import math
import random
import subprocess
import time 

# Render Immuteables
RESOLUTION, FPS, SEC = [720, 720, 3, 4], 60, int(sys.argv[1])
#CAMERA = (0, 90, 90, -0.65, -0.065)

# Scene Immuteables
# (label, centroid, start tick)
OBJECTS = [
    ("floor", (0, 0, 0), 0),
    ("paper", (0, 0, 10), 0),
    ("substance_1", (0, 0, 0), 0),
    ("flame", (0, 0, 0), 3)
]

# Scene Muteables
_NODES = {
    obj[0]: {
        "centroid": obj[1],
        "mass": 0,
        "density": lambda p: 0.0,
        "ticks": { 0: {"update": lambda frame: frame}},
        "ppm": []
    }
    for obj in OBJECTS
}

# a list of key[object:tick] deltas
_EDGES = []


# OBJ DENSITY TO .PPM FUNCTIONS
#

W, H = RESOLUTION[0], RESOLUTION[1]

# -------------------------------------------------------------------
# CAMERA
# -------------------------------------------------------------------
def camera(t):
    yaw = -0.82 + 0.18 * math.sin(t * 0.12)
    pitch = -0.11 + 0.03 * math.sin(t * 0.21)
    distance = 82 - 140 * min(1.0, t / 6.0)

    shake_yaw = (
        math.sin(t * 2.3) * 0.003 +
        math.sin(t * 7.7) * 0.001
    )

    shake_pitch = (
        math.cos(t * 1.9) * 0.002 +
        math.sin(t * 5.1) * 0.001
    )

    return (
        distance,
        yaw + shake_yaw,
        pitch + shake_pitch
    )


def rotate_y(x, z, a):
    ca, sa = math.cos(a), math.sin(a)
    return x * ca - z * sa, x * sa + z * ca


def rotate_x(y, z, a):
    ca, sa = math.cos(a), math.sin(a)
    return y * ca - z * sa, y * sa + z * ca


def project(x, y, z, t):
    dist, yaw, pitch = camera(t)

    x, z = rotate_y(x, z, yaw)
    y, z = rotate_x(y, z, pitch)

    z += dist

    if z <= 0.1:
        return None

    f = 180.0 / z

    sx = int(W * 0.58 + x * f)
    sy = int(H * 0.45 - y * f)

    return sx, sy, z


# -------------------------------------------------------------------
# FRAMEBUFFER
# -------------------------------------------------------------------

def clamp(v, lo=0, hi=255):
    return max(lo, min(hi, int(v)))


def pixel(frame, depth, x, y, z, r, g, b):
    if x < 0 or y < 0 or x >= W or y >= H:
        return

    idx = y * W + x

    if z < depth[idx]:
        depth[idx] = z
        i = idx * 3
        frame[i + 0], frame[i + 1], frame[i + 2] = clamp(r), clamp(g), clamp(b)


# -------------------------------------------------------------------
# POINT CLOUD DENSITY FIELDS
# -------------------------------------------------------------------
def floor_density(p):
    x, y, z = p
    if abs(y + 8) < 0.5:
        return 1.0
    return 0.0


def cigarette_density(p, t):
    x, y, z = p
    r = math.sqrt(y * y + z * z)
    burn = min(10.0, t * 0.7)

    if -18 < x < (18 - burn) and r < 1.2:
        return 1.0

    # ember
    if (18 - burn) < x < (20 - burn) and r < 1.5:
        return 2.0

    return 0.0


def smoke_density(p, t):
    x, y, z = p
    source_x = 18 - min(10.0, t * 0.7)
    density = 0.0

    for i in range(32):
        age = i * 0.18
        py = age * 6.0 + t * 3.5
        spread = 0.3 + py * 0.04

        drift_x = (
            math.sin(py * 0.15 + t * 0.8) * 1.2 +
            math.sin(py * 0.55 + t * 1.7) * 0.5
        )

        drift_z = (
            math.cos(py * 0.12 + t * 0.6) * 1.1 +
            math.cos(py * 0.41 + t * 1.4) * 0.6
        )

        px = source_x + drift_x
        pz = drift_z

        dx = x - px
        dy = y - py
        dz = z - pz

        r2 = dx * dx + dz * dz

        core = math.exp(
            -(r2 / (spread * spread))
            - abs(dy) * 0.35
        )

        density += core * math.exp(-age * 0.05)

    return density * 0.11


# -------------------------------------------------------------------
# POINT CLOUD SAMPLING
# -------------------------------------------------------------------

def sample_density_field(fn, bounds, samples):
    pts = []

    for _ in range(samples):
        x, y, z = random.uniform(bounds[0], bounds[1]), random.uniform(bounds[2], bounds[3]), random.uniform(bounds[4], bounds[5])
        d = fn((x, y, z))

        if d > 0.01:
            if random.random() < min(1.0, d):
                pts.append((x, y, z, d))

    return pts


# -------------------------------------------------------------------
# RENDERERS
# -------------------------------------------------------------------

def render_floor(frame, depth, t):
    pts = sample_density_field(floor_density,(-80, 80, -9, -7, -80, 80),40000)

    for x, y, z, d in pts:
        p = project(x, y, z, t)
        if p is None:
            continue

        sx, sy, sz = p
        shade = 35 + int((z + 80) * 0.3)
        pixel(frame, depth, sx, sy, sz, shade, shade, shade)


def render_cigarette(frame, depth, t):
    pts = sample_density_field(lambda p: cigarette_density(p, t), (-22, 22, -2, 2, -2, 2),50000)

    for x, y, z, d in pts:
        p = project(x, y, z, t)
        if p is None:
            continue

        sx, sy, sz = p
        burn = min(10.0, t * 0.7)

        if x > (16 - burn):
            r, g, b = 255, random.randint(60, 140), 10
        elif x < -12:
            r, g, b = 210, 160, 90
        else:
            r, g, b = 240, 240, 220

        pixel(frame, depth, sx, sy, sz, r, g, b)


def render_smoke(frame, depth, t):
    pts = sample_density_field(lambda p: smoke_density(p, t), (5, 30, -2, 40, -10, 10), 200000)

    for x, y, z, d in pts:
        p = project(x, y, z, t)
        if p is None:
            continue

        sx, sy, sz = p
        c = int(80 + min(175, d * 220))
        pixel(frame, depth, sx, sy, sz, c, c, c)


def render_flame(frame, depth, t):
    if t > 1.5:
        return

    for _ in range(12000):
        x = 20 + random.uniform(-1, 2)
        y = random.uniform(-1.5, 3)
        z = random.uniform(-1.5, 1.5)
        p = project(x, y, z, t)

        if p is None:
            continue

        sx, sy, sz = p
        heat = 0.77 #random.random()
        r, g, b = 255, int(180 * (1.0 - heat)), int(40 * (1.0 - heat))

        pixel(frame, depth, sx, sy, sz, r, g, b)


def render():
    FRAME = [[0] * (W * H * 3) for _ in range(FPS * SEC)]

    for tick in range(FPS * SEC):
        t = tick / FPS
        frame = FRAME[tick]
        depth = [1e9] * (W * H)

        for y in range(H):
            for x in range(W):
                i = (y * W + x) * 3
                v = int(8 + (y / H) * 18)
                frame[i + 0], frame[i + 1], frame[i + 2] = v, v, v


        render_floor(frame, depth, t)
        render_cigarette(frame, depth, t)
        render_smoke(frame, depth, t)
        render_flame(frame, depth, t)

    outdir = "3D-ISO"
    os.makedirs(outdir, exist_ok=True)
    raw_path = os.path.join(outdir, "frames.rgb")

    with open(raw_path, "wb") as f:
        for frame in FRAME:
            f.write(bytearray(frame))

    mp4_path = os.path.join(outdir, "cigarette.mp4")

    subprocess.run([
        "ffmpeg",
        "-y",
        "-f", 
        "rawvideo",
        "-pix_fmt", 
        "rgb24",
        "-s", 
        f"{W}x{H}",
        "-r", 
        str(FPS),
        "-i", 
        raw_path,
        "-c:v", 
        "libx264",
        "-pix_fmt", 
        "yuv420p",
        mp4_path
    ])

    return


'''
1. build immutable objects (Nodes)
2. serialize frames
    - apply transformations, including gravity
3. raytracer to RENDER frames + viewing angle to a video
'''
start = time.time()
render()
print("time: ", str(time.time() - start)[:6], "s")