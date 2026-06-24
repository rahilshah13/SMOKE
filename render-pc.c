#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>

#define FPS 60
#define W 720
#define H 720
#define N_PATHS 1
int SEC = 0;

typedef struct {
    unsigned int thread_id;
    double base_dist;
    double dist_amp;
    double dist_freq;
    double base_yaw;
    double yaw_speed;
    double base_pitch;
    double pitch_amp;
    double pitch_freq;
    double shake_amp;
    double zoom_speed;
} Camera_Config;

void eval_generic_camera(double t, const Camera_Config *cam, double *dist, double *yaw, double *pitch, double *focal) {
    *dist = cam->base_dist + cam->dist_amp * cos(t * cam->dist_freq);    
    *yaw = cam->base_yaw + (t * cam->yaw_speed);
    *pitch = cam->base_pitch + cam->pitch_amp * sin(t * cam->pitch_freq);    
    double shake_y = sin(t * 4.2) * cam->shake_amp + sin(t * 9.3) * (cam->shake_amp * 0.2);
    double shake_p = cos(t * 3.8) * cam->shake_amp + sin(t * 8.1) * (cam->shake_amp * 0.2);
    *yaw += shake_y;
    *pitch += shake_p;
    *focal = 220.0 + (t * cam->zoom_speed); 
}


double rand_float_range(double min, double max, unsigned int *seed) { 
    return min + (rand_r(seed) / (double)RAND_MAX) * (max - min); 
}

int rand_int_range(int min, int max, unsigned int *seed) { 
    return min + rand_r(seed) % (max - min); 
}

unsigned char clamp_val(double v, int lo, int hi) { 
    int val = (int)v; 
    return (val < lo) ? (unsigned char)lo : ((val > hi) ? (unsigned char)hi : (unsigned char)val); 
}

void system_run(const char *cmd) { 
    int ret = system(cmd); (void)ret; 
}

#define ROTATE_Y(x, z, a, rx, rz) do { \
    double ca = cos(a), sa = sin(a); \
    *(rx) = (x) * ca - (z) * sa; *(rz) = (x) * sa + (z) * ca; \
} while(0)

#define ROTATE_X(y, z, a, ry, rz) do { \
    double ca = cos(a), sa = sin(a); \
    *(ry) = (y) * ca - (z) * sa; *(rz) = (y) * sa + (z) * ca; \
} while(0)


typedef struct { double x, y, z, d; } Sample_Point;
typedef struct { Sample_Point *data; int size; int capacity; } Point_Buffer;

void buf_init(Point_Buffer *b) { 
    b->size = 0; b->capacity = 1024; 
    b->data = (Sample_Point *)malloc(b->capacity * sizeof(Sample_Point)); 
}

void buf_append(Point_Buffer *b, Sample_Point p) { 
    if (b->size >= b->capacity) { 
        b->capacity *= 2; b->data = (Sample_Point *)realloc(b->data, b->capacity * sizeof(Sample_Point)); \
    } \
    b->data[b->size++] = p; 
}

void pixel(unsigned char *frame, double *depth, int x, int y, double z, double r, double g, double b) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    int idx = y * W + x;
    if (z < depth[idx]) { 
        depth[idx] = z; int i = idx * 3; 
        frame[i+0] = clamp_val(r,0,255); frame[i+1] = clamp_val(g,0,255); frame[i+2] = clamp_val(b,0,255); 
    }
}

double floor_density(double p[3]) { return (fabs(p[1] + 8.0) < 0.5) ? 1.0 : 0.0; }

double cigarette_density(double p[3], double t) {
    double x = p[0], y = p[1], z = p[2]; double r = sqrt(y * y + z * z); double burn = fmin(10.0, t * 0.7);
    if (-18.0 < x && x < (18.0 - burn) && r < 1.2) return 1.0;
    if ((18.0 - burn) < x && x < (20.0 - burn) && r < 1.5) return 2.0;
    return 0.0;
}

double smoke_density(double p[3], double t) {
    double x = p[0], y = p[1], z = p[2]; double source_x = 18.0 - fmin(10.0, t * 0.7); double density = 0.0;
    for (int i = 0; i < 32; i++) {
        double age = (double)i * 0.18; double py = age * 6.0 + t * 3.5; double spread = 0.3 + py * 0.04;
        double drift_x = sin(py * 0.15 + t * 0.8) * 1.2 + sin(py * 0.55 + t * 1.7) * 0.5;
        double drift_z = cos(py * 0.12 + t * 0.6) * 1.1 + cos(py * 0.41 + t * 1.4) * 0.6;
        double dx = x - (source_x + drift_x); double dy = y - py; double dz = z - drift_z;
        density += exp(-(dx*dx + dz*dz) / (spread * spread) - fabs(dy) * 0.35) * exp(-age * 0.05);
    }
    return density * 0.11;
}

Point_Buffer sample_density_field_floor(double bounds[6], int samples, unsigned int *seed) {
    Point_Buffer pts; buf_init(&pts);
    for (int i = 0; i < samples; i++) {
        double x = rand_float_range(bounds[0], bounds[1], seed), y = rand_float_range(bounds[2], bounds[3], seed), z = rand_float_range(bounds[4], bounds[5], seed);
        double p[3] = {x, y, z}; double d = floor_density(p);
        if (d > 0.01 && (rand_r(seed) / (double)RAND_MAX) < fmin(1.0, d)) { Sample_Point sp = {x, y, z, d}; buf_append(&pts, sp); }
    }
    return pts;
}

Point_Buffer sample_density_field_cig(double bounds[6], int samples, double t, unsigned int *seed) {
    Point_Buffer pts; buf_init(&pts);
    for (int i = 0; i < samples; i++) {
        double x = rand_float_range(bounds[0], bounds[1], seed), y = rand_float_range(bounds[2], bounds[3], seed), z = rand_float_range(bounds[4], bounds[5], seed);
        double p[3] = {x, y, z}; double d = cigarette_density(p, t);
        if (d > 0.01 && (rand_r(seed) / (double)RAND_MAX) < fmin(1.0, d)) { Sample_Point sp = {x, y, z, d}; buf_append(&pts, sp); }
    }
    return pts;
}

Point_Buffer sample_density_field_smoke(double bounds[6], int samples, double t, unsigned int *seed) {
    Point_Buffer pts; buf_init(&pts);
    for (int i = 0; i < samples; i++) {
        double x = rand_float_range(bounds[0], bounds[1], seed), y = rand_float_range(bounds[2], bounds[3], seed), z = rand_float_range(bounds[4], bounds[5], seed);
        double p[3] = {x, y, z}; double d = smoke_density(p, t);
        if (d > 0.01 && (rand_r(seed) / (double)RAND_MAX) < fmin(1.0, d)) { Sample_Point sp = {x, y, z, d}; buf_append(&pts, sp); }
    }
    return pts;
}

bool project_point(double x, double y, double z, double t, const Camera_Config *cam, int *sx, int *sy, double *sz) {
    double dist, yaw, pitch, focal;
    eval_generic_camera(t, cam, &dist, &yaw, &pitch, &focal);
    
    double nx, nz; ROTATE_Y(x, z, yaw, &nx, &nz);
    double ny, fz; ROTATE_X(y, nz, pitch, &ny, &fz);
    
    double final_z = fz + dist;
    if (final_z <= 0.1) return false;
    
    double f = focal / final_z;
    *sx = (int)((double)W * 0.50 + nx * f); // Centered horizon shift
    *sy = (int)((double)H * 0.48 - ny * f);
    *sz = final_z;
    return true;
}

void* render_pipeline_runner(void* arg) {
    Camera_Config *cam = (Camera_Config*)arg;
    unsigned int seed = (unsigned int)time(NULL) + cam->thread_id;
    
    int total_frames = FPS * SEC;
    long long frame_size = W * H * 3;
    unsigned char *FRAME = (unsigned char *)malloc(total_frames * frame_size);
    double *depth = (double *)malloc(W * H * sizeof(double));
    
    printf("[Thread %u] Base Distance: %.2f | Zoom Velocity: +%.1f f/sec\n", 
           cam->thread_id, cam->base_dist, cam->zoom_speed);
           
    for (int tick = 0; tick < total_frames; tick++) {
        double t = (double)tick / (double)FPS;
        unsigned char *frame_slice = &FRAME[tick * frame_size];
        
        for (int i = 0; i < W * H; i++) depth[i] = 1e9;
        for (int i = 0; i < W * H * 3; i++) frame_slice[i] = 10; // Dark studio backdrop

        double bounds_floor[6] = {-120, 120, -9, -7, -120, 120};
        Point_Buffer pts_floor = sample_density_field_floor(bounds_floor, 40000, &seed);
        for (int i = 0; i < pts_floor.size; i++) {
            Sample_Point pt = pts_floor.data[i]; int sx, sy; double sz;
            if (!project_point(pt.x, pt.y, pt.z, t, cam, &sx, &sy, &sz)) continue;
            double shade = 30.0 + (sz * 0.15); // Subtle distance-based surface lighting
            pixel(frame_slice, depth, sx, sy, sz, shade, shade, shade + 3.0);
        }
        free(pts_floor.data);
        
        double bounds_cig[6] = {-22, 22, -2, 2, -2, 2};
        Point_Buffer pts_cig = sample_density_field_cig(bounds_cig, 50000, t, &seed);
        for (int i = 0; i < pts_cig.size; i++) {
            Sample_Point pt = pts_cig.data[i]; int sx, sy; double sz;
            if (!project_point(pt.x, pt.y, pt.z, t, cam, &sx, &sy, &sz)) continue;
            double burn = fmin(10.0, t * 0.7); double r, g, b;
            if (pt.x > (16.0 - burn)) { r = 255; g = (double)rand_int_range(70, 150, &seed); b = 15; }
            else if (pt.x < -12.0) { r = 215; g = 155; b = 85; } // Structured filter tip
            else { r = 238; g = 238; b = 230; } // Wrapping paper core
            pixel(frame_slice, depth, sx, sy, sz, r, g, b);
        }
        free(pts_cig.data);
        
        double bounds_smoke[6] = {5, 35, -2, 45, -12, 12};
        Point_Buffer pts_smoke = sample_density_field_smoke(bounds_smoke, 160000, t, &seed);
        for (int i = 0; i < pts_smoke.size; i++) {
            Sample_Point pt = pts_smoke.data[i]; int sx, sy; double sz;
            if (!project_point(pt.x, pt.y, pt.z, t, cam, &sx, &sy, &sz)) continue;
            double c = 85.0 + fmin(170.0, pt.d * 240.0);
            pixel(frame_slice, depth, sx, sy, sz, c, c + 2.0, c + 6.0);
        }
        free(pts_smoke.data);
    }
    
    // Write out frames and pipe straight into x264 profiles
    const char *outdir = "./";
    mkdir(outdir, 0775);
    char raw_path[256], mp4_path[256], cmd[1024];
    snprintf(raw_path, sizeof(raw_path), "%s/frames_inst_%u.rgb", outdir, cam->thread_id);
    FILE *f = fopen(raw_path, "wb");
    if (f) { fwrite(FRAME, 1, total_frames * frame_size, f); fclose(f); }
    snprintf(mp4_path, sizeof(mp4_path), "%s/pc_cig_%u.mp4", outdir, cam->thread_id);
    snprintf(cmd, sizeof(cmd), "ffmpeg -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i %s -c:v libx264 -pix_fmt yuv420p %s > /dev/null 2>&1", W, H, FPS, raw_path, mp4_path);
    system_run(cmd);
    
    free(FRAME); free(depth); free(cam);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc < 2) { printf("Usage: render <seconds>\n"); return 1; }
    SEC = atoi(argv[1]);
    if (SEC <= 0) return 1;
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    pthread_t threads[N_PATHS];
    unsigned int global_seed = (unsigned int)time(NULL);

    printf("Spawning %d threads...\n", N_PATHS);
    for (int i = 0; i < N_PATHS; i++) {
        Camera_Config *cam = (Camera_Config*)malloc(sizeof(Camera_Config));
        cam->thread_id = i;
        cam->base_dist  = rand_float_range(20.0, 35.0, &global_seed);
        cam->dist_amp   = rand_float_range(2.0, 6.0, &global_seed);
        cam->dist_freq  = rand_float_range(0.2, 0.4, &global_seed);
        cam->base_yaw   = rand_float_range(-1.0, 1.0, &global_seed);
        cam->yaw_speed  = rand_float_range(-0.15, 0.15, &global_seed);
        cam->base_pitch = rand_float_range(-0.4, -0.2, &global_seed);
        cam->pitch_amp  = rand_float_range(0.04, 0.12, &global_seed);
        cam->pitch_freq = rand_float_range(0.1, 0.3, &global_seed);
        cam->shake_amp  = rand_float_range(0.002, 0.004, &global_seed);
        cam->zoom_speed = rand_float_range(15.0, 35.0, &global_seed); // Dynamic push factor
        pthread_create(&threads[i], NULL, render_pipeline_runner, cam);
    }

    for (int i = 0; i < N_PATHS; i++) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    printf("\nSuccess! Generated %d renders in %.4fs\n", N_PATHS, elapsed);
    return 0;
}