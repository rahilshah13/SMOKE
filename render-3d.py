import sys
import os
import math
import random
import time 

# Immutables
outdir = "render-frames"
RESOLUTION, FPS, SEC = [720, 720, 3, 4], 60, int(sys.argv[1]) if len(sys.argv) > 1 else 4
W, H = RESOLUTION[0], RESOLUTION[1]

# -------------------------------------------------------------------
# CAMERA & PROJECTION ENGINE
# -------------------------------------------------------------------
def camera_builder(distance_fn, yaw_fn, pitch_fn):
    def compute_camera(t):
        shake_yaw = math.sin(t * 2.3) * 0.003 + math.sin(t * 7.7) * 0.001
        shake_pitch = math.cos(t * 1.9) * 0.002 + math.sin(t * 5.1) * 0.001
        return (distance_fn(t), yaw_fn(t) + shake_yaw, pitch_fn(t) + shake_pitch)
    return compute_camera

distance_lambda = lambda t: 82 - 140 * min(1.0, t / 6.0)
yaw_lambda      = lambda t: -0.82 + 0.18 * math.sin(t * 0.12)
pitch_lambda    = lambda t: -0.11 + 0.03 * math.sin(t * 0.21)
camera = camera_builder(distance_lambda, yaw_lambda, pitch_lambda)

def project(x, y, z, t):
    """Computes a 3D perspective projection matrix, returning a flat tuple coordinate or None."""
    dist, yaw, pitch = camera(t)
    
    # Rotate Y (Yaw)
    x_r1 = x * math.cos(yaw) - z * math.sin(yaw)
    z_r1 = x * math.sin(yaw) + z * math.cos(yaw)
    
    # Rotate X (Pitch)
    y_r2 = y * math.cos(pitch) - z_r1 * math.sin(pitch)
    z_r2 = (y * math.sin(pitch) + z_r1 * math.cos(pitch)) + dist
    
    if z_r2 > 0.1:
        sx = int(W * 0.58 + x_r1 * (180.0 / z_r2))
        sy = int(H * 0.45 - y_r2 * (180.0 / z_r2))
        return (sx, sy, z_r2)
    return None

# -------------------------------------------------------------------
# FRAMEBUFFER OPERATORS & SAFETIES
# -------------------------------------------------------------------
clamp = lambda v, lo=0, hi=255: max(lo, min(hi, int(v)))

def draw_pixel(frame, depth, x, y, z, r, g, b):
    """Safely updates the frame coordinate and handles depth checking cleanly."""
    if 0 <= x < W and 0 <= y < H:
        idx = y * W + x
        if z < depth[idx]:
            depth[idx] = z
            offset = idx * 3
            frame[offset] = clamp(r)
            frame[offset + 1] = clamp(g)
            frame[offset + 2] = clamp(b)

def project_and_pixel(frame, depth, x, y, z, t, r, g, b):
    """Helper wrapper to cleanly decouple list comprehensions from assignment bugs."""
    p = project(x, y, z, t)
    if p is not None:
        draw_pixel(frame, depth, p[0], p[1], p[2], r, g, b)

# -------------------------------------------------------------------
# PROCEDURAL MODEL FIELDS
# -------------------------------------------------------------------
def floor_density(p):
    return 1.0 if abs(p[1] + 8) < 0.5 else 0.0

def cigarette_density(p, t):
    x, y, z = p
    r = math.sqrt(y * y + z * z)
    burn = min(10.0, t * 0.7)
    if -18 < x < (18 - burn) and r < 1.2: return 1.0
    if (18 - burn) < x < (20 - burn) and r < 1.5: return 2.0
    return 0.0

def smoke_density(p, t):
    x, y, z = p
    source_x = 18 - min(10.0, t * 0.7)
    density = 0.0
    for i in range(32):
        age = i * 0.18
        py = age * 6.0 + t * 3.5
        spread = 0.3 + py * 0.04
        drift_x = math.sin(py * 0.15 + t * 0.8) * 1.2 + math.sin(py * 0.55 + t * 1.7) * 0.5
        drift_z = math.cos(py * 0.12 + t * 0.6) * 1.1 + math.cos(py * 0.41 + t * 1.4) * 0.6
        dx, dy, dz = x - (source_x + drift_x), y - py, z - drift_z
        density += math.exp(-((dx*dx + dz*dz) / (spread * spread)) - abs(dy) * 0.35) * math.exp(-age * 0.05)
    return density * 0.11

def sample_density_field(fn, bounds, samples):
    points = []
    for _ in range(samples):
        x = random.uniform(bounds[0], bounds[1])
        y = random.uniform(bounds[2], bounds[3])
        z = random.uniform(bounds[4], bounds[5])
        d = fn((x, y, z))
        if d > 0.01 and random.random() < min(1.0, d):
            points.append((x, y, z, d))
    return points

# -------------------------------------------------------------------
# STABLE SCENE RENDER EXECUTORS
# -------------------------------------------------------------------
def render_floor(frame, depth, t):
    for x, y, z, d in sample_density_field(floor_density, (-80, 80, -9, -7, -80, 80), 40000):
        shade = 35 + int((z + 80) * 0.3)
        project_and_pixel(frame, depth, x, y, z, t, shade, shade, shade)

def render_cigarette(frame, depth, t):
    burn = min(10.0, t * 0.7)
    for x, y, z, d in sample_density_field(lambda p: cigarette_density(p, t), (-22, 22, -2, 2, -2, 2), 50000):
        if x > (16 - burn):
            r, g, b = 255, random.randint(60, 140), 10
        elif x < -12:
            r, g, b = 210, 160, 90
        else:
            r, g, b = 240, 240, 220
        project_and_pixel(frame, depth, x, y, z, t, r, g, b)

def render_smoke(frame, depth, t):
    for x, y, z, d in sample_density_field(lambda p: smoke_density(p, t), (5, 30, -2, 40, -10, 10), 200000):
        c = int(80 + min(175, d * 220))
        project_and_pixel(frame, depth, x, y, z, t, c, c, c)

def render_flame(frame, depth, t):
    if t > 1.5:
        return
    for _ in range(12000):
        x = 20 + random.uniform(-1, 2)
        y = random.uniform(-1.5, 3)
        z = random.uniform(-1.5, 1.5)
        project_and_pixel(frame, depth, x, y, z, t, 255, 41, 9)

# -------------------------------------------------------------------
# PURE PYTHON RLE COMPRESSION GENERATOR
# -------------------------------------------------------------------
def save_rle_tga(filename, frame_data, width, height):
    header = bytearray([0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0, 0, width & 0xFF, (width >> 8) & 0xFF, height & 0xFF, (height >> 8) & 0xFF, 24, 0])
    body = bytearray()
    pixel_count = width * height
    idx = 0
    
    while idx < pixel_count:
        run_len = 1
        r, g, b = frame_data[idx*3], frame_data[idx*3+1], frame_data[idx*3+2]
        current_pixel = bytes([b, g, r])
        
        while idx + run_len < pixel_count and run_len < 128:
            nxt_idx = (idx + run_len) * 3
            if bytes([frame_data[nxt_idx+2], frame_data[nxt_idx+1], frame_data[nxt_idx]]) == current_pixel:
                run_len += 1
            else:
                break
                
        if run_len > 1:
            body.append(128 + (run_len - 1))
            body.extend(current_pixel)
            idx += run_len
        else:
            raw_pixels = [current_pixel]
            while idx + run_len < pixel_count and run_len < 128:
                nxt_idx = (idx + run_len) * 3
                nxt_pixel = bytes([frame_data[nxt_idx+2], frame_data[nxt_idx+1], frame_data[nxt_idx]])
                if idx + run_len + 1 < pixel_count:
                    third_idx = (idx + run_len + 1) * 3
                    if nxt_pixel == bytes([frame_data[third_idx+2], frame_data[third_idx+1], frame_data[third_idx]]):
                        break
                raw_pixels.append(nxt_pixel)
                run_len += 1
            body.append(run_len - 1)
            for p_bytes in raw_pixels:
                body.extend(p_bytes)
            idx += run_len

    with open(filename, "wb") as f:
        f.write(header)
        f.write(body)

# MAIN ENGINE
def render():
    num_frames = FPS * SEC
    os.makedirs(outdir, exist_ok=True)
    
    print(f"Beginning render sequence ({num_frames} frames)...")

    for tick in range(num_frames):
        t = tick / FPS
        frame = [0] * (W * H * 3)
        depth = [1e9] * (W * H)

        for y in range(H):
            for x in range(W):
                i = (y * W + x) * 3
                v = int(8 + (y / H) * 18)
                frame[i], frame[i + 1], frame[i + 2] = v, v, v

        render_floor(frame, depth, t)
        render_cigarette(frame, depth, t)
        render_smoke(frame, depth, t)
        render_flame(frame, depth, t)

        frame_path = os.path.join(outdir, f"frame_{tick:04d}.tga")
        save_rle_tga(frame_path, frame, W, H)
        
        if tick % 10 == 0 or tick == num_frames - 1:
            print(f"Processed Frame {tick + 1}/{num_frames}")

    print(f"\nRender completed successfully!")
    return

if __name__ == "__main__":
    start = time.time()
    render()
    ffmpeg_cmd = f'ffmpeg -y -framerate 60 -i {outdir}/frame_%04d.tga -vf "vflip" -c:v libx264 -pix_fmt yuv420p cigarette.mp4 > /dev/null 2>&1'
    exit_code = os.system(ffmpeg_cmd)
    
    if exit_code == 0 and os.path.exists(outdir):
        for file in os.listdir(outdir):
            os.remove(os.path.join(outdir, file))
        os.rmdir(outdir)

    print("Execution Processing time: ", str(time.time() - start)[:6], "s")