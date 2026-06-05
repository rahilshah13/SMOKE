#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

// Render Immutables
const int RESOLUTION[4] = {720, 720, 3, 4};
#define FPS 60
#define W 720
#define H 720

int SEC = 0;

// Scene Immutables
typedef struct {
    const char* label;
    double centroid[3];
    int start_tick;
} Object_Tuple;

Object_Tuple OBJECTS[] = {
    {"floor", {0, 0, 0}, 0},
    {"paper", {0, 0, 10}, 0},
    {"substance_1", {0, 0, 0}, 0},
    {"flame", {0, 0, 0}, 3}
};

// Node representations (Mirrored for parity with original structure)
typedef int (*Node_Update_Proc)(int frame);

typedef struct {
    Node_Update_Proc update;
} Node_Ticks;

typedef struct {
    double centroid[3];
    double mass;
    double (*density)(double p[3]);
    // dynamic maps/arrays omitted as they are unused placeholders in the render loop
    int ppm[3]; 
} Node;

// Helper to generate random double range [min, max]
double rand_float_range(double min, double max) {
    double scale = rand() / (double)RAND_MAX;
    return min + scale * (max - min);
}

// Helper to generate random integer range [min, max)
int rand_int_range(int min, int max) {
    return min + rand() % (max - min);
}

// -------------------------------------------------------------------
// CAMERA
// -------------------------------------------------------------------
void camera(double t, double *dist, double *yaw, double *pitch) {
    double yaw_val = -0.82 + 0.18 * sin(t * 0.12);
    double pitch_val = -0.11 + 0.03 * sin(t * 0.21);
    *dist = 82.0 - 140.0 * fmin(1.0, t / 6.0);

    double shake_yaw = sin(t * 2.3) * 0.003 + sin(t * 7.7) * 0.001;
    double shake_pitch = cos(t * 1.9) * 0.002 + sin(t * 5.1) * 0.001;

    *yaw = yaw_val + shake_yaw;
    *pitch = pitch_val + shake_pitch;
}

void rotate_y(double x, double z, double a, double *rx, double *rz) {
    double ca = cos(a), sa = sin(a);
    *rx = x * ca - z * sa;
    *rz = x * sa + z * ca;
}

void rotate_x(double y, double z, double a, double *ry, double *rz) {
    double ca = cos(a), sa = sin(a);
    *ry = y * ca - z * sa;
    *rz = y * sa + z * ca;
}

bool project(double x, double y, double z, double t, int *sx, int *sy, double *sz) {
    double dist, yaw, pitch;
    camera(t, &dist, &yaw, &pitch);

    double nx, nz;
    rotate_y(x, z, yaw, &nx, &nz);

    double ny, fz;
    rotate_x(y, nz, pitch, &ny, &fz);

    double final_z = fz + dist;
    if (final_z <= 0.1) {
        return false;
    }

    double f = 180.0 / final_z;
    *sx = (int)((double)W * 0.58 + nx * f);
    *sy = (int)((double)H * 0.45 - ny * f);
    *sz = final_z;

    return true;
}

// -------------------------------------------------------------------
// FRAMEBUFFER
// -------------------------------------------------------------------
unsigned char clamp_val(double v, int lo, int hi) {
    int val = (int)v;
    if (val < lo) return (unsigned char)lo;
    if (val > hi) return (unsigned char)hi;
    return (unsigned char)val;
}

void pixel(unsigned char *frame, double *depth, int x, int y, double z, double r, double g, double b) {
    if (x < 0 || y < 0 || x >= W || y >= H) {
        return;
    }

    int idx = y * W + x;
    if (z < depth[idx]) {
        depth[idx] = z;
        int i = idx * 3;
        frame[i + 0] = clamp_val(r, 0, 255);
        frame[i + 1] = clamp_val(g, 0, 255);
        frame[i + 2] = clamp_val(b, 0, 255);
    }
}

// -------------------------------------------------------------------
// POINT CLOUD DENSITY FIELDS
// -------------------------------------------------------------------
double floor_density(double p[3]) {
    if (fabs(p[1] + 8.0) < 0.5) {
        return 1.0;
    }
    return 0.0;
}

double cigarette_density(double p[3], double t) {
    double x = p[0], y = p[1], z = p[2];
    double r = sqrt(y * y + z * z);
    double burn = fmin(10.0, t * 0.7);

    if (-18.0 < x && x < (18.0 - burn) && r < 1.2) {
        return 1.0;
    }

    if ((18.0 - burn) < x && x < (20.0 - burn) && r < 1.5) {
        return 2.0;
    }

    return 0.0;
}

double smoke_density(double p[3], double t) {
    double x = p[0], y = p[1], z = p[2];
    double source_x = 18.0 - fmin(10.0, t * 0.7);
    double density = 0.0;

    for (int i = 0; i < 32; i++) {
        double age = (double)i * 0.18;
        double py = age * 6.0 + t * 3.5;
        double spread = 0.3 + py * 0.04;

        double drift_x = sin(py * 0.15 + t * 0.8) * 1.2 + sin(py * 0.55 + t * 1.7) * 0.5;
        double drift_z = cos(py * 0.12 + t * 0.6) * 1.1 + cos(py * 0.41 + t * 1.4) * 0.6;

        double px = source_x + drift_x;
        double pz = drift_z;

        double dx = x - px;
        double dy = y - py;
        double dz = z - pz;

        double r2 = dx * dx + dz * dz;
        double core = exp(-(r2 / (spread * spread)) - fabs(dy) * 0.35);

        density += core * exp(-age * 0.05);
    }

    return density * 0.11;
}

// -------------------------------------------------------------------
// POINT CLOUD SAMPLING
// -------------------------------------------------------------------
typedef struct {
    double x, y, z, d;
} Sample_Point;

// Dynamic array setup for sample buffers
typedef struct {
    Sample_Point *data;
    int size;
    int capacity;
} Point_Buffer;

void buf_init(Point_Buffer *b) {
    b->size = 0;
    b->capacity = 1024;
    b->data = (Sample_Point *)malloc(b->capacity * sizeof(Sample_Point));
}

void buf_append(Point_Buffer *b, Sample_Point p) {
    if (b->size >= b->capacity) {
        b->capacity *= 2;
        b->data = (Sample_Point *)realloc(b->data, b->capacity * sizeof(Sample_Point));
    }
    b->data[b->size++] = p;
}

Point_Buffer sample_density_field_floor(double bounds[6], int samples) {
    Point_Buffer pts;
    buf_init(&pts);

    for (int i = 0; i < samples; i++) {
        double x = rand_float_range(bounds[0], bounds[1]);
        double y = rand_float_range(bounds[2], bounds[3]);
        double z = rand_float_range(bounds[4], bounds[5]);

        double p[3] = {x, y, z};
        double d = floor_density(p);

        if (d > 0.01) {
            double r = rand() / (double)RAND_MAX;
            if (r < fmin(1.0, d)) {
                Sample_Point sp = {x, y, z, d};
                buf_append(&pts, sp);
            }
        }
    }
    return pts;
}

Point_Buffer sample_density_field_cig(double bounds[6], int samples, double t) {
    Point_Buffer pts;
    buf_init(&pts);

    for (int i = 0; i < samples; i++) {
        double x = rand_float_range(bounds[0], bounds[1]);
        double y = rand_float_range(bounds[2], bounds[3]);
        double z = rand_float_range(bounds[4], bounds[5]);

        double p[3] = {x, y, z};
        double d = cigarette_density(p, t);

        if (d > 0.01) {
            double r = rand() / (double)RAND_MAX;
            if (r < fmin(1.0, d)) {
                Sample_Point sp = {x, y, z, d};
                buf_append(&pts, sp);
            }
        }
    }
    return pts;
}

Point_Buffer sample_density_field_smoke(double bounds[6], int samples, double t) {
    Point_Buffer pts;
    buf_init(&pts);

    for (int i = 0; i < samples; i++) {
        double x = rand_float_range(bounds[0], bounds[1]);
        double y = rand_float_range(bounds[2], bounds[3]);
        double z = rand_float_range(bounds[4], bounds[5]);

        double p[3] = {x, y, z};
        double d = smoke_density(p, t);

        if (d > 0.01) {
            double r = rand() / (double)RAND_MAX;
            if (r < fmin(1.0, d)) {
                Sample_Point sp = {x, y, z, d};
                buf_append(&pts, sp);
            }
        }
    }
    return pts;
}

// -------------------------------------------------------------------
// RENDERERS
// -------------------------------------------------------------------
void render_floor(unsigned char *frame, double *depth, double t) {
    double bounds[6] = {-80, 80, -9, -7, -80, 80};
    Point_Buffer pts = sample_density_field_floor(bounds, 40000);

    for (int i = 0; i < pts.size; i++) {
        Sample_Point pt = pts.data[i];
        int sx, sy;
        double sz;
        if (!project(pt.x, pt.y, pt.z, t, &sx, &sy, &sz)) continue;

        double shade = 35.0 + (sz + 80.0) * 0.3;
        pixel(frame, depth, sx, sy, sz, shade, shade, shade);
    }
    free(pts.data);
}

void render_cigarette(unsigned char *frame, double *depth, double t) {
    double bounds[6] = {-22, 22, -2, 2, -2, 2};
    Point_Buffer pts = sample_density_field_cig(bounds, 50000, t);

    for (int i = 0; i < pts.size; i++) {
        Sample_Point pt = pts.data[i];
        int sx, sy;
        double sz;
        if (!project(pt.x, pt.y, pt.z, t, &sx, &sy, &sz)) continue;

        double burn = fmin(10.0, t * 0.7);
        double r, g, b;

        if (pt.x > (16.0 - burn)) {
            r = 255;
            g = (double)rand_int_range(60, 140);
            b = 10;
        } else if (pt.x < -12.0) {
            r = 210; g = 160; b = 90;
        } else {
            r = 240; g = 240; b = 220;
        }

        pixel(frame, depth, sx, sy, sz, r, g, b);
    }
    free(pts.data);
}

void render_smoke(unsigned char *frame, double *depth, double t) {
    double bounds[6] = {5, 30, -2, 40, -10, 10};
    Point_Buffer pts = sample_density_field_smoke(bounds, 200000, t);

    for (int i = 0; i < pts.size; i++) {
        Sample_Point pt = pts.data[i];
        int sx, sy;
        double sz;
        if (!project(pt.x, pt.y, pt.z, t, &sx, &sy, &sz)) continue;

        double c = 80.0 + fmin(175.0, pt.d * 220.0);
        pixel(frame, depth, sx, sy, sz, c, c, c);
    }
    free(pts.data);
}

void render_flame(unsigned char *frame, double *depth, double t) {
    if (t > 1.5) return;

    for (int i = 0; i < 12000; i++) {
        double x = 20.0 + rand_float_range(-1.0, 2.0);
        double y = rand_float_range(-1.5, 3.0);
        double z = rand_float_range(-1.5, 1.5);

        int sx, sy;
        double sz;
        if (!project(x, y, z, t, &sx, &sy, &sz)) continue;

        double heat = 0.77;
        double r = 255.0;
        double g = 180.0 * (1.0 - heat);
        double b = 40.0 * (1.0 - heat);

        pixel(frame, depth, sx, sy, sz, r, g, b);
    }
}

void system_run(const char *cmd) {
    int ret = system(cmd);
    (void)ret; // Suppress unused result compiler hints
}

void render() {
    int total_frames = FPS * SEC;
    long long frame_size = W * H * 3;

    unsigned char *FRAME = (unsigned char *)malloc(total_frames * frame_size);
    double *depth = (double *)malloc(W * H * sizeof(double));

    for (int tick = 0; tick < total_frames; tick++) {
        double t = (double)tick / (double)FPS;
        unsigned char *frame_slice = &FRAME[tick * frame_size];

        // Reset depth matrix values to 1e9
        for (int i = 0; i < W * H; i++) {
            depth[i] = 1e9;
        }

        // Generate background gradient
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int i = (y * W + x) * 3;
                unsigned char v = (unsigned char)(8 + ((double)y / (double)H) * 18);
                frame_slice[i + 0] = v;
                frame_slice[i + 1] = v;
                frame_slice[i + 2] = v;
            }
        }

        render_floor(frame_slice, depth, t);
        render_cigarette(frame_slice, depth, t);
        render_smoke(frame_slice, depth, t);
        render_flame(frame_slice, depth, t);
    }

    const char *outdir = "3D-ISO-C";
    mkdir(outdir, 0775);

    char raw_path[256];
    snprintf(raw_path, sizeof(raw_path), "%s/frames.rgb", outdir);

    FILE *f = fopen(raw_path, "wb");
    if (f != NULL) {
        fwrite(FRAME, 1, total_frames * frame_size, f);
        fclose(f);
    }

    char mp4_path[256];
    snprintf(mp4_path, sizeof(mp4_path), "%s/cigarette.mp4", outdir);

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), 
        "ffmpeg -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i %s -c:v libx264 -pix_fmt yuv420p %s",
        W, H, FPS, raw_path, mp4_path
    );

    system_run(cmd);

    free(FRAME);
    free(depth);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: render <seconds>\n");
        return 1;
    }

    SEC = atoi(argv[1]);
    if (SEC <= 0) {
        printf("Invalid seconds parameter.\n");
        return 1;
    }

    // Seed standard library RNG
    srand((unsigned int)time(NULL));

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    render();

    clock_gettime(CLOCK_MONOTONIC, &end);
    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("time: %.4fs\n", elapsed);
    return 0;
}