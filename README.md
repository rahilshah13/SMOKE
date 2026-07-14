### `pypy3 render-2D.py 5; rm *.rgb`
![2D Render](2d_cig.gif)

### `gcc -O3 render-pc.c -o render -lm -pthread; ./render 5 ; rm render; rm *.rgb`  
![Point Cloud Render](pc_cig_0.gif)

### `gcc -O3 render-opengl.c -I/opt/homebrew/include -L/opt/homebrew/lib -lglfw -framework OpenGL -o render; ./render; rm render`

![OpenGl Render](opengl_cig.gif)

### `for f in *.mp4; do ffmpeg -i "$f" "${f%.mp4}.gif"; done`

--- 
### `diamond.c`
- sponsored by Milestones Fine Jewelery
- `gcc -O3 diamond.c -o diamond -I/opt/homebrew/include -L/opt/homebrew/lib -lglfw -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo -lm`

- `./diamond -cut 0 -carat 2 -clarity 0.01 -g .9 -b .15 -o brilliant_flawless_pissy_yellow_2ct.gif`![](brilliant_flawless_pissy_yellow_2ct.gif)
