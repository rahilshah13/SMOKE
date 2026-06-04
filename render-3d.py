import sys
import math
import subprocess


##############################
#        IMMUTABLES          #
##############################
class OBJ(dict):
    __getattr__ = dict.__getitem__
    __setattr__ = dict.__setitem__

WIDTH, HEIGHT, FPS, SEC = 720, 720, 75, int(sys.argv[1]) if len(sys.argv) > 1 else 5
TICKS = FPS * SEC
empty_frame = lambda: [[[15, 15, 20] for _ in range(WIDTH)] for _ in range(HEIGHT)]
clamp = lambda x, lo=0, hi=255 : max(lo, min(hi, int(x)))

_INIT_OBJECTS = [
    ("floor", (0, 0, 0), 0),
    ("paper", (0, -20, 10), 0),
    ("substance_1", (15, -15, 10), 0),
    ("flame", (5, -10, 10), 180),
]

##############################
#          MUTABLES          #
##############################
NODES = {
    label: OBJ({
        "label": label,
        "centroid": list(centroid),
        "mass": 100.0 if label in {"paper", "substance_1"} else 0.0,
        "ticks": {},
    })
    for label, centroid, _ in _INIT_OBJECTS
}

EDGES = []

###########################
#    DELTA FUNCTIONS      #
###########################
def apply_velocity(n, e):
    n.centroid[0] += e.dx
    n.centroid[1] += e.dy
    n.centroid[2] += e.dz
    n.mass = max(0.0, n.mass + e.dm)
    return n

def get_noise(x, y, t):
    """Pseudo-noise for organic textures (flames and rough textures)."""
    return (
        math.sin(x * 0.1 + t * 0.2) * math.cos(y * 0.1 - t * 0.15) + 
        math.sin(x * 0.05 - y * 0.05 + t * 0.3) * 0.5
    )

def apply_density(n, f, tick):
    label, mass = n.label, max(0.0, n.mass)
    
    # Simple 3D to 2D Projection
    scale = 20.0
    cx = int(WIDTH // 2 + n.centroid[0] * scale)
    cy = int(HEIGHT // 2 - n.centroid[1] * scale)
    
    # 1. FLOOR RENDERER (Perspective Grid)
    if label == "floor":
        horizon = HEIGHT // 2 + 50
        for py in range(horizon, HEIGHT):
            # Calculate distance depth factor
            depth = 1.0 / max(0.001, (py - horizon) / 150.0)
            # Wood plank or tile shader
            for px in range(WIDTH):
                # Map screen coordinates to world floor coordinates
                world_x = (px - WIDTH // 2) * depth * 0.05
                world_y = depth
                
                # Checkered/Plank pattern
                pattern = (int(world_x) % 2) ^ (int(world_y) % 2)
                base_col = 35 if pattern else 25
                
                # Smooth darkening toward the horizon
                fog = min(1.0, (py - horizon) / 200.0)
                r = int(base_col * 1.2 * fog)
                g = int(base_col * 0.9 * fog)
                b = int(base_col * 0.7 * fog)
                f[py][px] = [r, g, b]
        return f

    # Early exit if the object has completely burned away
    if mass <= 0.1 and label != "flame":
        return f

    # Object-specific dimensions & physics behaviors
    radii = {"paper": 45, "substance_1": 25, "flame": 60}
    radius = int(radii.get(label, 30) * (mass / 100.0 if label != "flame" else 1.0))
    if radius <= 0: return f

    for dy in range(-radius, radius + 20 if label == "flame" else radius + 1):
        py = cy + dy
        if py < 0 or py >= HEIGHT: continue

        for dx in range(-radius, radius + 1):
            px = cx + dx
            if px < 0 or px >= WIDTH: continue

            # Core distance tracking
            dist = math.sqrt(dx * dx + dy * dy)
            
            # 2. FLAME (Organic plume shape with plasma noise)
            if label == "flame":
                # Reshape sphere into an upward teardrop/plume
                upward_taper = (dy + radius) / (2.0 * radius) if radius > 0 else 1.0
                modified_dx = dx / (0.3 + 0.7 * upward_taper)
                modified_dy = dy * 0.8
                dist = math.sqrt(modified_dx * modified_dx + modified_dy * modified_dy)
                
                noise = get_noise(px, py, tick)
                effective_radius = radius * (0.8 + 0.3 * noise)
                
                if dist < effective_radius:
                    falloff = 1.0 - (dist / effective_radius)
                    # Dynamic flame colors: White-hot center to deep orange tips
                    if falloff > 0.6:
                        rgb = (255, 235, 180)
                    elif falloff > 0.3:
                        rgb = (255, 140, 20)
                    else:
                        rgb = (200, 40, 5)
                        
                    # Additive alpha blending for realistic illumination
                    old = f[py][px]
                    alpha = falloff * 0.8
                    f[py][px] = [
                        clamp(old[0] + rgb[0] * alpha),
                        clamp(old[1] + rgb[1] * alpha),
                        clamp(old[2] + rgb[2] * alpha)
                    ]

            # 3. PAPER (Rectangular crumple with char edges)
            elif label == "paper":
                # Shear shape to make it look like a piece of paper resting at an angle
                rx = dx + dy * 0.2
                ry = dy - dx * 0.1
                
                if abs(rx) < radius * 1.2 and abs(ry) < radius * 0.8:
                    edge_dist = min(radius * 1.2 - abs(rx), radius * 0.8 - abs(ry))
                    
                    # Burning mechanics: Turns to ash from the inside out
                    is_charred = (mass < 70.0) and (edge_dist > radius * (mass / 100.0))
                    
                    if is_charred:
                        # Ash gray with bright glowing ember border
                        if edge_dist < radius * (mass / 100.0) + 4:
                            rgb = (255, 70, 10)  # Glowing ember edge
                        else:
                            ash_noise = int(10 * get_noise(px, py, 0))
                            rgb = (40 + ash_noise, 40 + ash_noise, 45 + ash_noise) # Dull Ash
                    else:
                        # Clean paper fiber texture
                        paper_noise = int(15 * get_noise(px, py, 0))
                        rgb = (240 - paper_noise, 235 - paper_noise, 220 - paper_noise)

                    f[py][px] = list(rgb)

            # 4. CHEMICAL SUBSTANCE (Textured, granular sphere)
            elif label == "substance_1":
                if dist < radius:
                    # Granular noise texture
                    grain = 1.0 + 0.25 * get_noise(px * 3.0, py * 3.0, 0)
                    falloff = 1.0 - (dist / radius)
                    
                    # If burning, it develops a bubbling, boiling look
                    if mass < 95.0:
                        bubble = get_noise(px * 0.5, py * 0.5, tick * 0.5)
                        if bubble > 0.3:
                            rgb = (255, 90, 0) # Molten hot core
                        else:
                            rgb = (30, 25, 25) # Hardened slag oxide
                    else:
                        # Raw copper/chemical substance
                        rgb = (int(180 * grain), int(50 * grain), int(150 * grain))
                        
                    # Basic 3D shading (Simulated light source from the flame)
                    shade = 0.6 + 0.4 * falloff
                    f[py][px] = [clamp(rgb[0] * shade), clamp(rgb[1] * shade), clamp(rgb[2] * shade)]
    return f


################################
#           RENDER             #
################################

# Initialize Key-frames and simulation states
for tick in range(TICKS):
    burning, burn_duration = (tick >= 120), max(1, TICKS - 120)
    dy = 0.25 if burning else 0.0 # Flame slowly flickers upward
    dm = -100.0 / burn_duration if burning else 0.0

    for label in ("paper", "substance_1", "flame"):
        if label == "flame" and not burning:
            continue

        EDGES.append(OBJ({
            "label": label,
            "tick": tick,
            "dm": dm if label != "flame" else 0.0,
            "dx": 0.0,
            "dy": dy if label == "flame" else 0.0,
            "dz": 0.0,
        }))

# Build updaters
for E in EDGES:
    def get_updater(e):
        def update(_frame):
            n = apply_velocity(NODES[e.label], e)
            return apply_density(n, _frame, e.tick)
        return update
    NODES[E.label].ticks[E.tick] = { "update": get_updater(E) }


def render():
    proc = subprocess.Popen([
        "ffmpeg", "-y", "-f", "rawvideo", "-vcodec", "rawvideo",
        "-pix_fmt", "rgb24", "-s", f"{WIDTH}x{HEIGHT}", "-r", str(FPS),
        "-i", "-", "-an", "-vcodec", "libx264", "-preset", "medium",
        "-crf", "16", "-pix_fmt", "yuv420p", "3D.mp4"
    ], stdin=subprocess.PIPE)

    print("Simulating and rendering frames with realistic shading...")

    for t in range(TICKS):
        # 1. Initialize with an empty canvas
        frame = empty_frame()
        
        # 2. Draw static elements (The floor grid)
        frame = apply_density(NODES["floor"], frame, t)
        
        # 3. Overlay the objects sorted by explicit visual layers 
        # (Background Paper -> Foreground Substance -> Translucent Flame)
        for label in ["paper", "substance_1", "flame"]:
            if t in NODES[label].ticks:
                updater = NODES[label].ticks[t]["update"]
                frame = updater(frame)
        
        # 4. Stream RGB bytes straight to FFmpeg pipeline
        rgb = bytearray()
        for row in frame:
            for px in row:
                rgb.extend(px)
        proc.stdin.write(rgb)

    proc.stdin.close()
    proc.wait()
    print(f"\nRender Complete: 3D.mp4 saved ({WIDTH}x{HEIGHT}p @ {FPS} FPS).")


if __name__ == "__main__":
    render()