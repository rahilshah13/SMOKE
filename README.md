### `pypy3 render-2D.py 5; rm *.rgb`
![2D Render](2d_cig.mp4)

### `gcc -O3 render-point-cloud.c -o render -lm -pthread; ./render 5 ; rm render`  
![Camera 1](point_cloud_cig_0.mp4)
![Camera 2](point_cloud_cig_1.mp4)

### `gcc -O3 render-opengl.c -I/opt/homebrew/include -L/opt/homebrew/lib -lglfw -framework OpenGL -o render; ./render; rm render`
![OpenGl Render](opengl_cig.mp4)