### Goal
pack watch <=> calculate lung cancer exposure


https://github.com/user-attachments/assets/db03dfc6-e726-4215-b802-3e9ac50c82df



### Demo
- `pypy3 render-2D.py <seconds>`
- `pypy3 render-3D.py <seconds>`
- `pypy3 render-iso.py <seconds>`
- `odin run . -- <seconds>`
- `gcc -O3 render-iso.c -o render-iso -lm ; ./render-iso 5`

### Misc
- `pypy3` was 10x faster than `python3` on one test

---

### `pypy3 render-iso.py 5` vs `odin run . -- 5` 720x720p@60FPS (single-node, concurrent run)
- pypy3: 126.54s (2.1 MB .mp4 generated)
- odin: 97.25s (3.4 MB .mp4 generated)
- gcc C: 59.05s (3.4 MB .mp4 generated)