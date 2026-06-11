package main

import "core:c/libc"
import "core:fmt"
import "core:math"
import "core:os"
import "core:strconv"
import "core:strings"
import "core:time"

// Active compilation configuration block hook
when #config(RENDER_3D, false) == false {

	// Render Immutables
	RESOLUTION :: [4]int{320, 180, 3, 4}
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

	// -------------------------------------------------------------------
	// PROCEDURAL DRAW ENGINE
	// -------------------------------------------------------------------

	clamp_val :: proc(v: f64, lo: int = 0, hi: int = 255) -> u8 {
		val := int(v)
		return u8(math.max(lo, math.min(hi, val)))
	}

	pixel :: proc(frame: []u8, x, y: int, r, g, b: f64) {
		if x < 0 || y < 0 || x >= W || y >= H {
			return
		}
		i := (y * W + x) * 3
		frame[i + 0] = clamp_val(r)
		frame[i + 1] = clamp_val(g)
		frame[i + 2] = clamp_val(b)
	}

	circle :: proc(frame: []u8, cx, cy, r: f64, color: [3]f64) {
		r2 := r * r
		start_y := int(cy - r)
		end_y   := int(cy + r)
		start_x := int(cx - r)
		end_x   := int(cx + r)

		for y in start_y..<end_y {
			for x in start_x..<end_x {
				dx := f64(x) - cx
				dy := f64(y) - cy
				if dx * dx + dy * dy <= r2 {
					pixel(frame, x, y, color[0], color[1], color[2])
				}
			}
		}
	}

	line :: proc(frame: []u8, x0, y0, x1, y1, thickness: f64, color: [3]f64) {
		steps := int(math.max(math.abs(x1 - x0), math.abs(y1 - y0))) + 1
		for i in 0..<steps {
			t := f64(i) / f64(steps)
			x := x0 + (x1 - x0) * t
			y := y0 + (y1 - y0) * t
			circle(frame, x, y, thickness, color)
		}
	}

	smoke :: proc(frame: []u8, t: f64) {
		base_x := f64(W / 2 + 80)
		base_y := f64(H / 2 - 5)

		for i in 0..<20 {
			fi := f64(i)
			drift := math.sin(t * 0.7 + fi * 0.3) * 10.0
			rise := fi * 6.0 + t * 12.0
			
			x := base_x + drift
			y := base_y - rise
			alpha := math.max(0.0, 180.0 - fi * 8.0)
			
			circle(frame, x, y, 8.0 + fi / 2.0, {alpha, alpha, alpha})
		}
	}

	cigarette :: proc(frame: []u8, t: f64) {
		cx := f64(W / 2)
		cy := f64(H / 2)

		line(frame, cx - 80, cy, cx + 80, cy, 5, {235, 235, 220})
		line(frame, cx - 80, cy, cx - 40, cy, 5, {210, 160, 80})

		burn := math.min(70.0, t * 8.0)
		ash_x := cx + 80.0 - burn

		line(frame, ash_x - 5.0, cy, ash_x + 2.0, cy, 5, {120, 120, 120})
		ember := 180.0 + math.sin(t * 12.0) * 70.0
		circle(frame, ash_x + 3.0, cy, 5, {ember, 60, 10})

		if t < 1.5 {
			fx := ash_x + 12.0
			fy := cy

			for i in 0..<12 {
				fi := f64(i)
				ox := math.sin(t * 20.0 + fi) * 4.0
				oy := math.cos(t * 18.0 + fi) * 4.0
				circle(frame, fx + ox, fy + oy, 6.0 - fi / 3.0, {255, 180.0 - fi * 10.0, 40})
			}
		}

		smoke(frame, t)
	}

	// -------------------------------------------------------------------
	// PIPELINE EXECUTION
	// -------------------------------------------------------------------

	render :: proc() {
		total_frames := FPS * SEC
		frame_size := W * H * 3
		
		FRAME := make([]u8, total_frames * frame_size)
		defer delete(FRAME)

		for tick in 0 ..< total_frames {
			t := f64(tick) / f64(FPS)
			frame_slice := FRAME[tick * frame_size : (tick + 1) * frame_size]

			// Background generation loop
			for y in 0 ..< H {
				shade := 10.0 + (f64(y) / f64(H)) * 25.0
				for x in 0 ..< W {
					pixel(frame_slice, x, y, shade, shade, shade)
				}
			}

			// Surface table rendering loop
			for y in (H / 2 + 20) ..< H {
				for x in 0 ..< W {
					pixel(frame_slice, x, y, 55, 38, 24)
				}
			}

			cigarette(frame_slice, t)
		}

		outdir := "render-2D"
		_ = os.make_directory_all(outdir)
		raw_path := strings.concatenate({outdir, "/frames.rgb"})
		defer delete(raw_path)
		_ = os.write_entire_file(raw_path, FRAME)

		mp4_path := strings.concatenate({outdir, "/cigarette.mp4"})
		defer delete(mp4_path)

		w_h_str := fmt.tprintf("%dx%d", W, H)
		fps_str := fmt.tprintf("%d", FPS)

		cmd := fmt.tprintf(
			"ffmpeg -y -f rawvideo -pix_fmt rgb24 -s %s -r %s -i %s -c:v libx264 -pix_fmt yuv420p %s > /dev/null 2>&1",
			w_h_str,
			fps_str,
			raw_path,
			mp4_path,
		)
		cmd_cstr := strings.clone_to_cstring(cmd)
		defer delete(cmd_cstr)

		libc.system(cmd_cstr)
	}

	main :: proc() {
		if len(os.args) < 2 {
			os.exit(1)
		}

		sec_arg, ok := strconv.parse_int(os.args[1])
		if !ok {
			os.exit(1)
		}
		SEC = sec_arg

		start := time.now()
		render()
		elapsed := time.since(start)

		fmt.printf("Execution Processing time: %.4f s\n", time.duration_seconds(elapsed))
	}
}