#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include <getopt.h>
#include <libcsv.h>

#define MAX_MOLECULES 100
#define MAX_CAS_LEN 32
#define MAX_NAME_LEN 64
#define DflT_FPS 60
#define DflT_SEC 20
#define DflT_W 720
#define DflT_H 720

int FPS = DflT_FPS;
int SEC = DflT_SEC;
int W = DflT_W;
int H = DflT_H;
double AToMizer_V0 = 18.0;
double AToMizer_SPREAD = 0.42;
double AMBient_TEMP = 295.15; 
double AMBient_HUMIDITY = 0.45;
char OUT_FILENAME[256] = "perfume_dispersion.gif";

typedef struct {
    char cas[MAX_CAS_LEN];
    char name[MAX_NAME_LEN];
    double mw;
    double vap_pressure;
    double r, g, b;
} Molecule;

Molecule loaded_molecules[MAX_MOLECULES];
int loaded_count = 0;

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

typedef struct { double x, y, z, d; int mol_idx; } Sample_Point;
typedef struct { Sample_Point *data; int size; int capacity; } Point_Buffer;

typedef struct {
    int stage; 
    char target_cas[MAX_CAS_LEN];
    double *out_val;
    bool found;
} CSV_Lookup_Ctx;

static double rand_float_range(double min, double max, unsigned int *seed) { 
    return min + (rand_r(seed) / (double)RAND_MAX) * (max - min); 
}

static int rand_int_range(int min, int max, unsigned int *seed) { 
    return min + rand_r(seed) % (max - min); 
}

static unsigned char clamp_val(double v, int lo, int hi) { 
    int val = (int)v; 
    if (val < lo) return (unsigned char)lo; 
    if (val > hi) return (unsigned char)hi; 
    return (unsigned char)val; 
}

void buf_init(Point_Buffer *b) { 
    b->size = 0; b->capacity = 4096; 
    b->data = (Sample_Point *)malloc(b->capacity * sizeof(Sample_Point)); 
}

void buf_append(Point_Buffer *b, Sample_Point p) { 
    if (b->size >= b->capacity) { 
        b->capacity *= 2; 
        b->data = (Sample_Point *)realloc(b->data, b->capacity * sizeof(Sample_Point)); 
    } 
    b->data[b->size++] = p; 
}

void cb_load_molecules(void *s, size_t len, void *data) {
    static int col = 0;
    if (loaded_count >= MAX_MOLECULES) return;

    if (col == 0) {
        strncpy(loaded_molecules[loaded_count].cas, s, len);
        loaded_molecules[loaded_count].cas[len] = '\0';
        unsigned int color_seed = loaded_count + 1000; 
        loaded_molecules[loaded_count].r = rand_int_range(50, 255, &color_seed);
        loaded_molecules[loaded_count].g = rand_int_range(50, 255, &color_seed);
        loaded_molecules[loaded_count].b = rand_int_range(50, 255, &color_seed);
        loaded_molecules[loaded_count].mw = -1.0; 
        loaded_molecules[loaded_count].vap_pressure = -1.0;
    } else if (col == 1) {
        strncpy(loaded_molecules[loaded_count].name, s, len);
        loaded_molecules[loaded_count].name[len] = '\0';
        loaded_count++;
    }
    col++;
    if (col == 2) col = 0;
}

bool parse_csv_file(const char *filename, csv_callback cb, void *user_data) {
    FILE *fp = fopen(filename, "r");
    if (!fp) { fprintf(stderr, "Error opening %s\n", filename); return false; }

    struct csv_parser p;
    csv_init(&p, CSV_STRICT);
    char buf[1024];
    size_t bytes_read;

    while ((bytes_read = fread(buf, 1, 1024, fp)) > 0) {
        if (csv_parse(&p, buf, bytes_read, cb, user_data) != bytes_read) {
            fprintf(stderr, "Error parsing %s: %s\n", filename, csv_strerror(csv_error(&p)));
            csv_free(&p); fclose(fp); return false;
        }
    }
    csv_fini(&p, cb, user_data);
    csv_free(&p);
    fclose(fp);
    return true;
}

void load_simulation_data() {
    printf("Loading molecule database...\n");
    if (!parse_csv_file("molecules.csv", cb_load_molecules, NULL)) exit(1);
    printf("Loaded %d molecules.\n", loaded_count);

    printf("Loading physical properties...\n");
    for (int i = 0; i < loaded_count; i++) {
        // [PLACEHOLDER 1: Parse molecular weight via CAS using the provided CSV schema]

        // [PLACEHOLDER 2: Parse vapor pressure using Gibbs equation constants via CAS schema]

        if (loaded_molecules[i].mw < 0.0 || loaded_molecules[i].vap_pressure < 0.0) {
            fprintf(stderr, "Warning: Missing properties for CAS %s (%s)\n", loaded_molecules[i].cas, loaded_molecules[i].name);
        }
    }
}

double calculate_density(double p[3], double t, int mol_idx, double *drift_x, double *spread) {
    Molecule m = loaded_molecules[mol_idx];
    if (m.mw < 0.0 || m.vap_pressure < 0.0) return 0.0;

    double x = p[0], y = p[1], z = p[2];
    double effective_t = fmax(0.001, t);
    double k_temp = AMBient_TEMP / 298.15;

    double mw_factor = sqrt(150.0 / m.mw);
    *spread = fmax(0.05, (AToMizer_SPREAD + sqrt(effective_t) * 0.85) * mw_factor * sqrt(k_temp));

    *drift_x = AToMizer_V0 * (1.0 - exp(-effective_t * 2.5)) * (m.vap_pressure / 1.5);
    double drift_y = -0.4 * effective_t * (m.mw / 150.0);
    double drift_z = 0.0;

    double dx = x - (*drift_x);
    double dy = y - drift_y;
    double dz = z - drift_z;
    double dist_sq = dx * dx + dy * dy + dz * dz;

    double safe_hum = fmin(1.0, fmax(0.0, AMBient_HUMIDITY));
    double hum_damp = 1.0 - (safe_hum * 0.2);
    double evap_dec = exp(-effective_t * (m.vap_pressure * 0.12));

    double base_gauss = exp(-dist_sq / (*spread * *spread));
    double vol_norm = 1.0 / (*spread * *spread * *spread);

    return base_gauss * m.vap_pressure * hum_damp * evap_dec * vol_norm * 150.0;
}

Point_Buffer sample_plume(int samples, double t, unsigned int *seed) {
    Point_Buffer pts;
    buf_init(&pts);

    for (int i = 0; i < samples; i++) {
        int mol_idx = rand_int_range(0, loaded_count, seed);
        
        double drift_x, spread;
        double center_p[3] = {0,0,0};
        calculate_density(center_p, t, mol_idx, &drift_x, &spread);

        double domain_r = spread * 3.5;
        double x = rand_float_range(drift_x - domain_r, drift_x + domain_r, seed);
        double y = rand_float_range(-0.5 - domain_r, domain_r, seed);
        double z = rand_float_range(-domain_r, domain_r, seed);

        double p[3] = {x, y, z};
        double d = calculate_density(p, t, mol_idx, &drift_x, &spread);

        if (d > 0.005 && (rand_r(seed) / (double)RAND_MAX) < fmin(1.0, d * 0.1 * (spread*spread))) {
            Sample_Point sp = {x, y, z, d, mol_idx};
            buf_append(&pts, sp);
        }
    }
    return pts;
}

void draw_legend(unsigned char *frame) {
    if (loaded_count == 0) return;
    int panel_h = 75;
    int panel_y = H - panel_h;

    for (int y = panel_y; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int idx = (y * W + x) * 3;
            frame[idx+0] = (unsigned char)(frame[idx+0] * 0.3 + 10);
            frame[idx+1] = (unsigned char)(frame[idx+1] * 0.3 + 10);
            frame[idx+2] = (unsigned char)(frame[idx+2] * 0.3 + 15);
        }
    }

    int col_w = W / loaded_count;
    int swatch_w = fmin(20, col_w - 10);
    int swatch_h = 12;

    for (int i = 0; i < loaded_count; i++) {
        int start_x = i * col_w + (col_w - swatch_w) / 2;
        int start_y = panel_y + 15;

        for (int sy = 0; sy < swatch_h; sy++) {
            for (int sx = 0; sx < swatch_w; sx++) {
                int px = start_x + sx;
                int py = start_y + sy;
                if (px >= 0 && px < W && py >= 0 && py < H) {
                    int idx = (py * W + px) * 3;
                    frame[idx+0] = (unsigned char)loaded_molecules[i].r;
                    frame[idx+1] = (unsigned char)loaded_molecules[i].g;
                    frame[idx+2] = (unsigned char)loaded_molecules[i].b;
                }
            }
        }
    }
}

void eval_camera(double t, const Camera_Config *cam, double *dist, double *yaw, double *pitch, double *focal) {
    *dist = cam->base_dist + cam->dist_amp * cos(t * cam->dist_freq);    
    *yaw = cam->base_yaw + (t * cam->yaw_speed);
    *pitch = cam->base_pitch + cam->pitch_amp * sin(t * cam->pitch_freq);    
    *yaw += sin(t * 4.2) * cam->shake_amp;
    *pitch += cos(t * 3.8) * cam->shake_amp;
    *focal = 220.0 + (t * cam->zoom_speed); 
}

bool project_point(double x, double y, double z, double t, const Camera_Config *cam, int *sx, int *sy, double *sz) {
    double dist, yaw, pitch, focal;
    eval_camera(t, cam, &dist, &yaw, &pitch, &focal);
    
    double ca = cos(yaw), sa = sin(yaw);
    double nx = x * ca - z * sa; 
    double nz = x * sa + z * ca;
    
    double cp = cos(pitch), sp = sin(pitch);
    double ny = y * cp - nz * sp; 
    double fz = y * sp + nz * cp;
    
    double final_z = fz + dist;
    if (final_z <= 0.1) return false;
    
    double f = focal / final_z;
    *sx = (int)((double)W * 0.45 + nx * f);
    *sy = (int)((double)H * 0.42 - ny * f);
    *sz = final_z;
    return true;
}

void pixel(unsigned char *frame, double *depth, int x, int y, double z, double r, double g, double b) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    int idx = y * W + x;
    if (z < depth[idx]) { 
        depth[idx] = z; int i = idx * 3; 
        frame[i+0] = clamp_val(r,0,255); 
        frame[i+1] = clamp_val(g,0,255); 
        frame[i+2] = clamp_val(b,0,255); 
    }
}

void* render_runner(void* arg) {
    Camera_Config *cam = (Camera_Config*)arg;
    unsigned int seed = (unsigned int)time(NULL) + cam->thread_id;
    
    int total_frames = FPS * SEC;
    long long frame_size = W * H * 3;
    unsigned char *FRAME = (unsigned char *)malloc(total_frames * frame_size);
    double *depth = (double *)malloc(W * H * sizeof(double));

    for (int tick = 0; tick < total_frames; tick++) {
        double t = (double)tick / (double)FPS;
        unsigned char *frame_slice = &FRAME[tick * frame_size];
        
        for (int i = 0; i < W * H; i++) depth[i] = 1e9;
        for (int i = 0; i < W * H * 3; i++) frame_slice[i] = 10;

        Point_Buffer pts_plume = sample_plume(160000, t, &seed);
        for (int i = 0; i < pts_plume.size; i++) {
            Sample_Point pt = pts_plume.data[i]; 
            int sx, sy; 
            double sz;
            if (!project_point(pt.x, pt.y, pt.z, t, cam, &sx, &sy, &sz)) continue;
            
            Molecule m = loaded_molecules[pt.mol_idx];
            double intensity = fmin(1.0, pt.d * 0.45);
            pixel(frame_slice, depth, sx, sy, sz, m.r * intensity + 15, m.g * intensity + 15, m.b * intensity + 15);
        }
        free(pts_plume.data);

        draw_legend(frame_slice);
    }
    
    char raw_path[256], cmd[1024];
    snprintf(raw_path, sizeof(raw_path), "render_temp_%u.rgb", cam->thread_id);
    FILE *f = fopen(raw_path, "wb");
    if (f) { fwrite(FRAME, 1, total_frames * frame_size, f); fclose(f); }

    snprintf(cmd, sizeof(cmd), 
             "ffmpeg -y -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i %s "
             "-vf \"fps=%d,scale=%d:%d:flags=lanczos,split[s0][s1];[s0]palettegen[p];[s1][p]paletteuse\" %s > /dev/null 2>&1", 
             W, H, FPS, raw_path, FPS, W, H, OUT_FILENAME);
    int ret = system(cmd); 
    (void)ret;
    remove(raw_path);
    
    free(FRAME); 
    free(depth); 
    free(cam);
    return NULL;
}

void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS]\n\n", prog_name);
    printf("Options:\n");
    printf("  -s, --seconds SECONDS     Duration of output GIF in seconds (default: 20)\n");
    printf("  -r, --fps FPS             Frames per second (default: 60)\n");
    printf("  -w, --width WIDTH         Width of output frame in pixels (default: 720)\n");
    printf("  -h, --height HEIGHT       Height of output frame in pixels (default: 720)\n");
    printf("  -v, --velocity VELOCITY   Atomizer initial velocity v0 m/s (default: 18.0)\n");
    printf("  -c, --cone SPREAD         Atomizer cone spread base factor (default: 0.42)\n");
    printf("  -t, --temp KELVIN         Ambient temperature in Kelvin (default: 295.15)\n");
    printf("  -m, --humidity RH         Ambient relative humidity 0.0-1.0 (default: 0.45)\n");
    printf("  -o, --output FILE         Output GIF filename (default: perfume_dispersion.gif)\n");
    printf("  -?, --help                Display this help screen\n");
}

int main(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"seconds",  required_argument, 0, 's'},
        {"fps",      required_argument, 0, 'r'},
        {"width",    required_argument, 0, 'w'},
        {"height",   required_argument, 0, 'h'},
        {"velocity", required_argument, 0, 'v'},
        {"cone",     required_argument, 0, 'c'},
        {"temp",     required_argument, 0, 't'},
        {"humidity", required_argument, 0, 'm'},
        {"output",   required_argument, 0, 'o'},
        {"help",     no_argument,       0, '?'},
        {0, 0, 0, 0}
    };

    int opt, option_index = 0;
    while ((opt = getopt_long(argc, argv, "s:r:w:h:v:c:t:m:o:?", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's': SEC = atoi(optarg); break;
            case 'r': FPS = atoi(optarg); break;
            case 'w': W = atoi(optarg); break;
            case 'h': H = atoi(optarg); break;
            case 'v': AToMizer_V0 = atof(optarg); break;
            case 'c': AToMizer_SPREAD = atof(optarg); break;
            case 't': AMBient_TEMP = atof(optarg); break;
            case 'm': AMBient_HUMIDITY = atof(optarg); break;
            case 'o': strncpy(OUT_FILENAME, optarg, sizeof(OUT_FILENAME) - 1); break;
            case '?': print_usage(argv[0]); return 0;
            default: break;
        }
    }

    load_simulation_data();

    pthread_t thread;
    Camera_Config *cam = (Camera_Config*)malloc(sizeof(Camera_Config));
    cam->thread_id = 0;
    cam->base_dist  = 34.0;
    cam->dist_amp   = 4.0;
    cam->dist_freq  = 0.12;
    cam->base_yaw   = -0.15;
    cam->yaw_speed  = 0.04;
    cam->base_pitch = -0.12;
    cam->pitch_amp  = 0.04;
    cam->pitch_freq = 0.18;
    cam->shake_amp  = 0.001;
    cam->zoom_speed = 6.0;

    pthread_create(&thread, NULL, render_runner, cam);
    pthread_join(thread, NULL);

    return 0;
}
