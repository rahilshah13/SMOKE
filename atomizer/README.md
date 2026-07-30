perfume dispersion point cloud renderer

simulates an animated gif of a perfume solution's dispersion, driven by physics models and molecular properties.

installation & compilation (macos)

```bash
brew install libcsv ffmpeg
gcc -O3 -pthread render.c -o perfume_render -lm -lcsv

```

example command

```bash
./perfume_render -s 20 -r 30 -t 298.15 -m 0.5 -v 25.0 -o summer_breeze.gif

```

options

* `-s, --seconds`: duration (default: 20)
* `-r, --fps`: frames per second (default: 60)
* `-w, --width`, `-h, --height`: resolution (default: 720x720)
* `-v, --velocity`: initial spray velocity m/s (default: 18.0)
* `-c, --cone`: cone spread factor (default: 0.42)
* `-t, --temp`: ambient temperature in Kelvin (default: 295.15)
* `-m, --humidity`: relative humidity 0.0–1.0 (default: 0.45)
* `-o, --output`: output filename (default: `perfume_dispersion.gif`)
* `-?, --help`: display usage information
