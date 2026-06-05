package main

import "core:c/libc"
import "core:fmt"
import "core:math"
import "core:math/rand"
import "core:os"
import "core:slice"
import "core:strconv"
import "core:strings"
import "core:time"

// Render Immutables
RESOLUTION: [4]int : {720, 720, 3, 4}
FPS        :: 60
W          :: RESOLUTION[0]
H          :: RESOLUTION[1]

SEC: int

// Scene Immutables
Object_Tuple :: struct {
	label:      string,
	centroid:   [3]f64,
	start_tick: int,
}

OBJECTS := []Object_Tuple{
	{"floor", {0, 0, 0}, 0},
	{"paper", {0, 0, 10}, 0},
	{"substance_1", {0, 0, 0}, 0},
	{"flame", {0, 0, 0}, 3},
}

// Node representations (Mirroring Python definitions)
Node_Update_Proc :: proc(frame: int) -> int

Node_Ticks :: struct {
	update: Node_Update_Proc,
}

Node :: struct {
	centroid: [3]f64,
	mass:     f64,
	density:  proc(p: [3]f64) -> f64,
	ticks:    map[int]Node_Ticks,
	ppm:      [3]int, // Simplified or dynamic as needed
}

_NODES: map[string]Node
_EDGES: [][2]string // Mock representation of edges

// -------------------------------------------------------------------
// CAMERA
// -------------------------------------------------------------------
camera :: proc(t: f64) -> (dist: f64, yaw: f64, pitch: f64) {
	yaw_val := -0.82 + 0.18 * math.sin(t * 0.12)
	pitch_val := -0.11 + 0.03 * math.sin(t * 0.21)
	distance := 82.0 - 140.0 * math.min(1.0, t / 6.0)

	shake_yaw := math.sin(t * 2.3) * 0.003 + math.sin(t * 7.7) * 0.001
	shake_pitch := math.cos(t * 1.9) * 0.002 + math.sin(t * 5.1) * 0.001

	return distance, yaw_val + shake_yaw, pitch_val + shake_pitch
}

rotate_y :: proc(x, z, a: f64) -> (rx: f64, rz: f64) {
	ca, sa := math.cos(a), math.sin(a)
	return x * ca - z * sa, x * sa + z * ca
}

rotate_x :: proc(y, z, a: f64) -> (ry: f64, rz: f64) {
	ca, sa := math.cos(a), math.sin(a)
	return y * ca - z * sa, y * sa + z * ca
}

project :: proc(x, y, z, t: f64) -> (sx: int, sy: int, sz: f64, ok: bool) {
	dist, yaw, pitch := camera(t)

	nx, nz := rotate_y(x, z, yaw)
	ny, fz := rotate_x(y, nz, pitch)

	final_z := fz + dist

	if final_z <= 0.1 {
		return 0, 0, 0.0, false
	}

	f := 180.0 / final_z

	sx_val := int(f64(W) * 0.58 + nx * f)
	sy_val := int(f64(H) * 0.45 - ny * f)

	return sx_val, sy_val, final_z, true
}

// -------------------------------------------------------------------
// FRAMEBUFFER
// -------------------------------------------------------------------
clamp_val :: proc(v: f64, lo: int = 0, hi: int = 255) -> u8 {
	val := int(v)
	return u8(math.max(lo, math.min(hi, val)))
}

pixel :: proc(frame: []u8, depth: []f64, x, y: int, z: f64, r, g, b: f64) {
	if x < 0 || y < 0 || x >= W || y >= H {
		return
	}

	idx := y * W + x

	if z < depth[idx] {
		depth[idx] = z
		i := idx * 3
		frame[i + 0] = clamp_val(r)
		frame[i + 1] = clamp_val(g)
		frame[i + 2] = clamp_val(b)
	}
}

// -------------------------------------------------------------------
// POINT CLOUD DENSITY FIELDS
// -------------------------------------------------------------------
floor_density :: proc(p: [3]f64) -> f64 {
	if math.abs(p[1] + 8.0) < 0.5 {
		return 1.0
	}
	return 0.0
}

cigarette_density :: proc(p: [3]f64, t: f64) -> f64 {
	x, y, z := p[0], p[1], p[2]
	r := math.sqrt(y * y + z * z)
	burn := math.min(10.0, t * 0.7)

	if -18.0 < x && x < (18.0 - burn) && r < 1.2 {
		return 1.0
	}

	// ember
	if (18.0 - burn) < x && x < (20.0 - burn) && r < 1.5 {
		return 2.0
	}

	return 0.0
}

smoke_density :: proc(p: [3]f64, t: f64) -> f64 {
	x, y, z := p[0], p[1], p[2]
	source_x := 18.0 - math.min(10.0, t * 0.7)
	density := 0.0

	for i in 0 ..< 32 {
		age := f64(i) * 0.18
		py := age * 6.0 + t * 3.5
		spread := 0.3 + py * 0.04

		drift_x := math.sin(py * 0.15 + t * 0.8) * 1.2 + math.sin(py * 0.55 + t * 1.7) * 0.5
		drift_z := math.cos(py * 0.12 + t * 0.6) * 1.1 + math.cos(py * 0.41 + t * 1.4) * 0.6

		px := source_x + drift_x
		pz := drift_z

		dx := x - px
		dy := y - py
		dz := z - pz

		r2 := dx * dx + dz * dz
		core := math.exp(-(r2 / (spread * spread)) - math.abs(dy) * 0.35)

		density += core * math.exp(-age * 0.05)
	}

	return density * 0.11
}

// -------------------------------------------------------------------
// POINT CLOUD SAMPLING
// -------------------------------------------------------------------
Sample_Point :: struct {
	x, y, z, d: f64,
}

sample_density_field_floor :: proc(bounds: [6]f64, samples: int) -> [dynamic]Sample_Point {
	pts: [dynamic]Sample_Point
	for _ in 0 ..< samples {
		x := rand.float64_range(bounds[0], bounds[1])
		y := rand.float64_range(bounds[2], bounds[3])
		z := rand.float64_range(bounds[4], bounds[5])

		d := floor_density({x, y, z})

		if d > 0.01 {
			if rand.float64() < math.min(1.0, d) {
				append(&pts, Sample_Point{x, y, z, d})
			}
		}
	}
	return pts
}

sample_density_field_cig :: proc(bounds: [6]f64, samples: int, t: f64) -> [dynamic]Sample_Point {
	pts: [dynamic]Sample_Point
	for _ in 0 ..< samples {
		x := rand.float64_range(bounds[0], bounds[1])
		y := rand.float64_range(bounds[2], bounds[3])
		z := rand.float64_range(bounds[4], bounds[5])

		d := cigarette_density({x, y, z}, t)

		if d > 0.01 {
			if rand.float64() < math.min(1.0, d) {
				append(&pts, Sample_Point{x, y, z, d})
			}
		}
	}
	return pts
}

sample_density_field_smoke :: proc(bounds: [6]f64, samples: int, t: f64) -> [dynamic]Sample_Point {
	pts: [dynamic]Sample_Point
	for _ in 0 ..< samples {
		x := rand.float64_range(bounds[0], bounds[1])
		y := rand.float64_range(bounds[2], bounds[3])
		z := rand.float64_range(bounds[4], bounds[5])

		d := smoke_density({x, y, z}, t)

		if d > 0.01 {
			if rand.float64() < math.min(1.0, d) {
				append(&pts, Sample_Point{x, y, z, d})
			}
		}
	}
	return pts
}

// -------------------------------------------------------------------
// RENDERERS
// -------------------------------------------------------------------
render_floor :: proc(frame: []u8, depth: []f64, t: f64) {
	pts := sample_density_field_floor({-80, 80, -9, -7, -80, 80}, 40000)
	defer delete(pts)

	for pt in pts {
		sx, sy, sz, ok := project(pt.x, pt.y, pt.z, t)
		if !ok do continue

		shade := 35.0 + (sz + 80.0) * 0.3
		pixel(frame, depth, sx, sy, sz, shade, shade, shade)
	}
}

render_cigarette :: proc(frame: []u8, depth: []f64, t: f64) {
	pts := sample_density_field_cig({-22, 22, -2, 2, -2, 2}, 50000, t)
	defer delete(pts)

	for pt in pts {
		sx, sy, sz, ok := project(pt.x, pt.y, pt.z, t)
		if !ok do continue

		burn := math.min(10.0, t * 0.7)
		r, g, b: f64

		if pt.x > (16.0 - burn) {
			r = 255
			g = f64(rand.int_range(60, 140))
			b = 10
		} else if pt.x < -12.0 {
			r, g, b = 210, 160, 90
		} else {
			r, g, b = 240, 240, 220
		}

		pixel(frame, depth, sx, sy, sz, r, g, b)
	}
}

render_smoke :: proc(frame: []u8, depth: []f64, t: f64) {
	pts := sample_density_field_smoke({5, 30, -2, 40, -10, 10}, 200000, t)
	defer delete(pts)

	for pt in pts {
		sx, sy, sz, ok := project(pt.x, pt.y, pt.z, t)
		if !ok do continue

		c := 80.0 + math.min(175.0, pt.d * 220.0)
		pixel(frame, depth, sx, sy, sz, c, c, c)
	}
}

render_flame :: proc(frame: []u8, depth: []f64, t: f64) {
	if t > 1.5 do return

	for _ in 0 ..< 12000 {
		x := 20.0 + rand.float64_range(-1.0, 2.0)
		y := rand.float64_range(-1.5, 3.0)
		z := rand.float64_range(-1.5, 1.5)

		sx, sy, sz, ok := project(x, y, z, t)
		if !ok do continue

		heat := 0.77
		r := 255.0
		g := 180.0 * (1.0 - heat)
		b := 40.0 * (1.0 - heat)

		pixel(frame, depth, sx, sy, sz, r, g, b)
	}
}

render :: proc() {
	total_frames := FPS * SEC
	frame_size := W * H * 3
	FRAME := make([]u8, total_frames * frame_size)
	defer delete(FRAME)

	depth := make([]f64, W * H)
	defer delete(depth)

	for tick in 0 ..< total_frames {
		t := f64(tick) / f64(FPS)
		frame_slice := FRAME[tick * frame_size : (tick + 1) * frame_size]

		// Reset depth frame-by-frame
		slice.fill(depth, 1e9)

		// Set background gradient
		for y in 0 ..< H {
			for x in 0 ..< W {
				i := (y * W + x) * 3
				v := u8(8 + (f64(y) / f64(H)) * 18)
				frame_slice[i + 0] = v
				frame_slice[i + 1] = v
				frame_slice[i + 2] = v
			}
		}

		render_floor(frame_slice, depth, t)
		render_cigarette(frame_slice, depth, t)
		render_smoke(frame_slice, depth, t)
		render_flame(frame_slice, depth, t)
	}

	outdir := "render-odin"
    _ = os.make_directory_all(outdir)
    raw_path := strings.concatenate({outdir, "/frames.rgb"})
    defer delete(raw_path)
    _ = os.write_entire_file(raw_path, FRAME)

	mp4_path := strings.concatenate({outdir, "/cigarette.mp4"})
	defer delete(mp4_path)

	w_h_str := fmt.tprintf("%dx%d", W, H)
	fps_str := fmt.tprintf("%d", FPS)

	cmd := fmt.tprintf(
		"ffmpeg -y -f rawvideo -pix_fmt rgb24 -s %s -r %s -i %s -c:v libx264 -pix_fmt yuv420p %s",
		w_h_str,
		fps_str,
		raw_path,
		mp4_path,
	)
	cmd_cstr := strings.clone_to_cstring(cmd)
	defer delete(cmd_cstr)

	// Run system subprocess synchronously
	system_run(cmd_cstr)
}

system_run :: proc(cmd: cstring) { libc.system(cmd) }

main :: proc() {
	if len(os.args) < 2 {
		fmt.println("Usage: render <seconds>")
		os.exit(1)
	}

	sec_arg, ok := strconv.parse_int(os.args[1])
	if !ok {
		fmt.println("Invalid seconds parameter.")
		os.exit(1)
	}
	SEC = sec_arg

	start := time.now()
	render()
	elapsed := time.since(start)

    fmt.printf("time: %.4fs\n", time.duration_seconds(elapsed))
}