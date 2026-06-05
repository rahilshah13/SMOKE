### Goal
pack watch <=> calculate lung cancer exposure


https://github.com/user-attachments/assets/db03dfc6-e726-4215-b802-3e9ac50c82df



### Demo
- `pypy3 render-2D.py <seconds>`
- `pypy3 render-3D.py <seconds>`
- `pypy3 render-iso.py <seconds>`
- `odin run . -- <seconds>`
- `gcc -O3 render-iso.c -o render-iso -lm ; ./render-iso <seconds>`
- `clang -O3 -march=native render-iso.c -o render-iso -lm && ./render-iso <seconds>`

### Misc
- `pypy3` was 10x faster than `python3` on one test

---
### 5-Second 720x720p@60FPS .mp4 Generation
- pypy3: 127s (2.1 MB)
- odin: 97s (3.4 MB)
- gcc C: 59s (3.4 MB)
- clang C: 58s (2.7 MB)