import sys
import os
import math
import struct
import subprocess

# Render Immuteables
CAMERA, RESOLUTION, FPS, SEC = (0, 30, 30, -0.65, -0.65), [320, 180, 3, 4], 60, int(sys.argv[1])
W, H = RESOLUTION[0], RESOLUTION[1]

# Scene Immuteables
# (label, centroid, start_tick)
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
        "density": lambda x: x,
        "ticks": {
            0: {"update": lambda frame: frame}
        },
        "ppm": []
    }
    for obj in OBJECTS
}

_EDGES = []

def clamp(v, lo=0, hi=255):
    return max(lo, min(hi, int(v)))

def pixel(frame, x, y, r, g, b):
    if 0 <= x < W and 0 <= y < H:
        i = (y * W + x) * 3
        frame[i + 0], frame[i + 1], frame[i + 2] = clamp(r), clamp(g), clamp(b)

def circle(frame, cx, cy, r, color):
    r2 = r * r
    for y in range(int(cy - r), int(cy + r)):
        for x in range(int(cx - r), int(cx + r)):
            dx, dy = (x - cx), (y - cy)
            if dx * dx + dy * dy <= r2:
                pixel(frame, x, y, *color)

def line(frame, x0, y0, x1, y1, thickness, color):
    steps = int(max(abs(x1 - x0), abs(y1 - y0))) + 1
    for i in range(steps):
        t = i / steps
        x = x0 + (x1 - x0) * t
        y = y0 + (y1 - y0) * t
        circle(frame, x, y, thickness, color)

def smoke(frame, t):
    base_x = W // 2 + 80
    base_y = H // 2 - 5
    for i in range(20):
        drift, rise = math.sin(t * 0.7 + i * 0.3) * 10, (i * 6 + t * 12)
        x, y = int(base_x + drift), int(base_y - rise)
        alpha = max(0, 180 - i * 8)
        circle(frame, x, y, 8 + i // 2, (alpha, alpha, alpha))


def cigarette(frame, t):
    cx = W // 2
    cy = H // 2
    line(frame, cx - 80, cy, cx + 80, cy, 5, (235, 235, 220))
    line(frame, cx - 80, cy, cx - 40, cy, 5, (210, 160, 80))
    burn = min(70, int(t * 8))
    ash_x = cx + 80 - burn
    line(frame, ash_x - 5, cy, ash_x + 2, cy, 5, (120, 120, 120))
    ember = 180 + int(math.sin(t * 12) * 70)
    circle(frame, ash_x + 3, cy, 5, (ember, 60, 10))

    # flame during ignition
    if t < 1.5:
        fx, fy = ash_x + 12, cy

        for i in range(12):
            ox, oy = math.sin(t * 20 + i) * 4, math.cos(t * 18 + i) * 4
            circle(frame, fx + ox, fy + oy, 6 - i // 3, (255, 180 - i * 10, 40))

    smoke(frame, t)


def render():
    FRAME = [[0] * (W * H * 3) for _ in range(FPS * SEC)]

    for tick in range(FPS * SEC):
        t = tick / FPS
        frame = FRAME[tick]
        # background
        for y in range(H):
            shade = int(10 + (y / H) * 25)
            for x in range(W):
                pixel(frame, x, y, shade, shade, shade)
        # table
        for y in range(H // 2 + 20, H):
            for x in range(W):
                pixel(frame, x, y, 55, 38, 24)

        cigarette(frame, t)

    outdir = "./"
    os.makedirs(outdir, exist_ok=True)
    raw_path = os.path.join(outdir, "frames.rgb")

    with open(raw_path, "wb") as f:
        for frame in FRAME:
            f.write(bytearray(frame))

    mp4_path = os.path.join(outdir, "2d_cig.mp4")
    subprocess.run([
        "ffmpeg",
        "-y",
        "-f", "rawvideo",
        "-pix_fmt", "rgb24",
        "-s", f"{W}x{H}",
        "-r", str(FPS),
        "-i", raw_path,
        "-c:v", 
        "libx264",
        "-pix_fmt", 
        "yuv420p",
        mp4_path
    ])

    return

render()