### Goal
pack watch <=> calculate lung cancer exposure


https://github.com/user-attachments/assets/db03dfc6-e726-4215-b802-3e9ac50c82df


### Demo
- `pypy3 render-3D.py 5`
- `odin run render-3d.odin -file -- 5`
- `gcc -O3 render-3D.c -o render-3D -lm ; ./render-3D 5 ; rm render-3D`
- `clang -O3 -march=native render-3D.c -o render-3D -lm && ./render-3D 5 ; rm render-3D`

---
### 300 frames @ 720x720p .mp4 Generation on my laptop
- pypy3: 67s (2.1 MB)
- odin: 49s (2.7 MB)
- gcc C: 30s (2.7 MB)
- clang C: 30s (2.7 MB)