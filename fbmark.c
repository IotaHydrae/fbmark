/*
  fbmark                   Linux Framebuffer Benchmark
  Copyright (C) 2014-2017  Nicolas Caramelli

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "fb_util.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define TEST_COUNT 13
#define FBMARK_VERSION "1.0.1"

/* ---- result type ---- */

typedef struct {
  const char *name;
  const char *metric;
  double    value;
  const char *unit;
} test_result_t;

/* ---- scoring metadata ---- */

typedef enum { METRIC_HIGHER_BETTER, METRIC_LOWER_BETTER } metric_direction_t;

typedef struct {
  double              ref_value;
  metric_direction_t  direction;
} score_meta_t;

static const score_meta_t score_meta[TEST_COUNT] = {
  {   3.0,     METRIC_LOWER_BETTER  },  /* Mandelbrot      */
  { 200.0,     METRIC_HIGHER_BETTER },  /* Rectangle fill  */
  {  60.0,     METRIC_HIGHER_BETTER },  /* Sierpinski      */
  { 200.0,     METRIC_HIGHER_BETTER },  /* Gradient fill   */
  { 500.0,     METRIC_HIGHER_BETTER },  /* Blit copy       */
  { 100000.0,  METRIC_HIGHER_BETTER },  /* Line draw       */
  { 20000.0,   METRIC_HIGHER_BETTER },  /* Circle draw     */
  { 1000.0,    METRIC_HIGHER_BETTER },  /* Fullscreen fill */
  {  30.0,     METRIC_HIGHER_BETTER },  /* Plasma effect   */
  { 500.0,     METRIC_HIGHER_BETTER },  /* Scroll          */
  { 100000.0,  METRIC_HIGHER_BETTER },  /* Text render     */
  { 10000.0,   METRIC_HIGHER_BETTER },  /* Triangle fill   */
  {   3.0,     METRIC_LOWER_BETTER  },  /* Julia set       */
};

/* ---- test function forward declarations ---- */

static test_result_t test_mandelbrot(void);
static test_result_t test_rectangle(void);
static test_result_t test_sierpinski(void);
static test_result_t test_gradient(void);
static test_result_t test_blit(void);
static test_result_t test_line(void);
static test_result_t test_circle(void);
static test_result_t test_fill(void);
static test_result_t test_plasma(void);
static test_result_t test_scroll(void);
static test_result_t test_text(void);
static test_result_t test_triangle(void);
static test_result_t test_julia(void);

/* ---- test function table ---- */

typedef test_result_t (*test_fn_t)(void);

typedef struct {
  const char *short_name;
  const char *display_name;
  int         index;
  test_fn_t   func;
} test_entry_t;

static const test_entry_t test_table[TEST_COUNT] = {
  {"mandelbrot",   "Mandelbrot",      0,  test_mandelbrot},
  {"rectangle",    "Rectangle fill",  1,  test_rectangle},
  {"sierpinski",   "Sierpinski",      2,  test_sierpinski},
  {"gradient",     "Gradient fill",   3,  test_gradient},
  {"blit",         "Blit copy",       4,  test_blit},
  {"line",         "Line draw",       5,  test_line},
  {"circle",       "Circle draw",     6,  test_circle},
  {"fill",         "Fullscreen fill", 7,  test_fill},
  {"plasma",       "Plasma effect",   8,  test_plasma},
  {"scroll",       "Scroll",          9,  test_scroll},
  {"text",         "Text render",    10,  test_text},
  {"triangle",     "Triangle fill",  11,  test_triangle},
  {"julia",        "Julia set",      12,  test_julia},
};

static double compute_total_score(const test_result_t results[TEST_COUNT],
                                  const int selected[TEST_COUNT])
{
  double total = 0.0;
  int i, count = 0;

  for (i = 0; i < TEST_COUNT; i++) {
    double score;
    if (!selected[i]) continue;
    if (score_meta[i].direction == METRIC_HIGHER_BETTER) {
      /* higher raw value → higher score */
      score = (results[i].value / score_meta[i].ref_value) * 100.0;
    } else {
      /* lower raw value → higher score */
      score = (score_meta[i].ref_value / results[i].value) * 100.0;
    }
    if (score < 0.0) score = 0.0;
    total += score;
    count++;
  }

  return count > 0 ? total / count : 0.0;
}

/* ---- globals (set once by main) ---- */

static int              fb_fd;
static struct fb_var_screeninfo fb_info;
static size_t           fb_len;
static size_t           fb_pixel_size;
static size_t           fb_row_size;
static unsigned char   *fb_buffer;

static int bench_width, bench_height, bench_posx, bench_posy;

/* ---- helpers ---- */

static unsigned char *pixel_at(int x, int y)
{
  return fb_buffer + (bench_posy + y) * fb_row_size + (bench_posx + x) * fb_pixel_size;
}

static void clear_screen(void)
{
  int i;
  for (i = 0; i < bench_height; i++)
    memset(pixel_at(0, i), 0, bench_width * fb_pixel_size);
}

/* ---- device / system info detection ---- */

static char *read_sysfs_line(const char *path)
{
  FILE *f = fopen(path, "r");
  static char buf[256];
  if (!f) return NULL;
  if (fgets(buf, sizeof(buf), f)) {
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    fclose(f);
    return buf;
  }
  fclose(f);
  return NULL;
}

static const char *detect_device_model(void)
{
  char *vendor, *product;
  static char model[512];

  /* x86 / DMI — copy strings out of the shared static buffer immediately */
  vendor  = read_sysfs_line("/sys/class/dmi/id/sys_vendor");
  if (vendor) {
    char vendor_copy[256];
    strncpy(vendor_copy, vendor, sizeof(vendor_copy) - 1);
    vendor_copy[sizeof(vendor_copy) - 1] = '\0';

    product = read_sysfs_line("/sys/class/dmi/id/product_name");
    if (product) {
      snprintf(model, sizeof(model), "%s %s", vendor_copy, product);
      return model;
    }
  }

  /* ARM / device-tree */
  product = read_sysfs_line("/proc/device-tree/model");
  if (product) {
    strncpy(model, product, sizeof(model) - 1);
    model[sizeof(model) - 1] = '\0';
    return model;
  }

  return "Unknown";
}

static const char *detect_cpu_model(void)
{
  FILE *f = fopen("/proc/cpuinfo", "r");
  static char buf[256];
  if (!f) return "Unknown";

  while (fgets(buf, sizeof(buf), f)) {
    char *colon = strchr(buf, ':');
    if (colon) {
      *colon = '\0';
      if (strstr(buf, "model name") || strstr(buf, "Model Name")) {
        char *val = colon + 1;
        while (*val == ' ' || *val == '\t') val++;
        char *nl = strchr(val, '\n');
        if (nl) *nl = '\0';
        fclose(f);
        return val;
      }
    }
  }
  fclose(f);
  return "Unknown";
}

static const char *detect_device_vendor(void)
{
  char *vendor;

  vendor = read_sysfs_line("/sys/class/dmi/id/sys_vendor");
  if (vendor) return vendor;

  /* ARM device-tree model: first word is often the vendor */
  vendor = read_sysfs_line("/proc/device-tree/model");
  if (vendor) {
    static char v[128];
    char *space = strchr(vendor, ' ');
    if (space) {
      size_t len = space - vendor;
      if (len >= sizeof(v)) len = sizeof(v) - 1;
      memcpy(v, vendor, len);
      v[len] = '\0';
      return v;
    }
  }

  return "Unknown";
}

/* =====================================================================
 *  Test 1: Mandelbrot
 * ===================================================================== */

static test_result_t test_mandelbrot(void)
{
  unsigned int iters, i, j, n;
  unsigned char c;
  float tmp, x, y, seconds;
  struct timeval start, stop;

  gettimeofday(&start, NULL);
  iters = 1;
  while (iters <= 48) {
    for (i = 0; i < bench_height; i++) {
      for (j = 0; j < bench_width; j++) {
        x = y = c = 0;
        float a = 3.f * j / bench_width - 2.f;
        float b = 1.f - 2.f * i / bench_height;
        do {
          tmp = x;
          x = x * x - y * y + a;
          y = 2.f * tmp * y + b;
          if (x * x + y * y > 4.f) break;
        } while (++c < iters);
        for (n = 0; n < fb_pixel_size && n < 3; n++)
          *(pixel_at(j, i) + n) = c * 255 / iters;
      }
    }
    iters++;
  }
  gettimeofday(&stop, NULL);
  seconds = (stop.tv_sec - start.tv_sec) + (stop.tv_usec - start.tv_usec) / 1000000.;

  test_result_t r = {"Mandelbrot", "time", seconds, "s  (lower is better)"};
  return r;
}

/* =====================================================================
 *  Test 2: Rectangle fill
 * ===================================================================== */

static test_result_t test_rectangle(void)
{
  unsigned int w, h, i, j, x, y, count, start_ms, end_ms;
  unsigned char cr, cg, cb;
  struct timeval tv;
  unsigned char *data;

  w = bench_width >> 2;
  h = bench_height >> 2;
  count = 0;
  gettimeofday(&tv, NULL);
  start_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

  do {
    cr = rand() % 256;
    cg = rand() % 256;
    cb = rand() % 256;
    x = rand() % (bench_width - w);
    y = rand() % (bench_height - h);
    data = pixel_at(x, y);

    for (i = 0; i < h; i++) {
      for (j = 0; j < w; j++) {
        data[fb_pixel_size * j + 2] = cr;
        data[fb_pixel_size * j + 1] = cg;
        data[fb_pixel_size * j + 0] = cb;
      }
      data += fb_row_size;
    }
    count++;
    gettimeofday(&tv, NULL);
    end_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  } while (end_ms < (start_ms + 5000));

  test_result_t res = {"Rectangle fill", "throughput",
                     count * (double)w * h / ((end_ms - start_ms) * 1000.),
                     "MPixels/s"};
  return res;
}

/* =====================================================================
 *  Test 3: Sierpinski
 * ===================================================================== */

typedef struct { int x, y; } pixel_t;

static test_result_t test_sierpinski(void)
{
  pixel_t p[3], v;
  unsigned int angle, radius, iters, frames, time, i, n, d[3];
  float seconds, fps[16], best;
  struct timeval last, now;
  unsigned char *data;

  data = malloc(bench_width * bench_height * fb_pixel_size);
  angle = 0;
  radius = MIN(bench_width, bench_height) / 2 - 8;
  iters = 1024;
  frames = time = 0;
  best = 0;
  gettimeofday(&last, NULL);

  printf("\n  Sierpinski details:\n");

  while (time < (int)(sizeof(fps) / sizeof(float))) {
    memset(data, 0, bench_width * bench_height * fb_pixel_size);
    for (n = 0; n < 3; n++) {
      p[n].x = bench_width / 2 + (int)(radius * cos((n * 120 + angle) * M_PI / 180));
      p[n].y = bench_height / 2 + (int)(radius * sin((n * 120 + angle) * M_PI / 180));
    }
    v.x = p[0].x;
    v.y = p[0].y;
    for (i = 0; i < iters; i++) {
      n = rand() % 3;
      v.x = (v.x + p[n].x) / 2. + .5;
      v.y = (v.y + p[n].y) / 2. + .5;
      for (n = 0; n < 3; n++) {
        d[n] = (v.x - p[n].x) * (v.x - p[n].x) + (v.y - p[n].y) * (v.y - p[n].y);
        if (v.y >= 0 && v.y < bench_height && v.x >= 0 && v.x < bench_width)
          data[v.y * bench_width * fb_pixel_size + v.x * fb_pixel_size + 2 - n] =
            (unsigned char)((1.f - d[n] / (3.f * radius * radius)) * 255);
      }
    }

    for (i = 0; i < bench_height; i++)
      memcpy(pixel_at(0, i), data + i * bench_width * fb_pixel_size,
             bench_width * fb_pixel_size);

    frames++;
    gettimeofday(&now, NULL);
    seconds = (now.tv_sec - last.tv_sec) + (now.tv_usec - last.tv_usec) * 1e-6;
    if (seconds >= 1.f) {
      fps[time] = frames / seconds;
      if (fps[time] > best) best = fps[time];
      printf("    %8d iterations: %8.2f FPS\n", iters, fps[time]);
      if (fps[time] < (getenv("SIERPINSKI_FPS") ? atoi(getenv("SIERPINSKI_FPS")) : 4.f))
        break;
      last = now;
      frames = 0;
      iters *= 2;
      time++;
    }
    angle = (angle + 1) % 360;
  }

  free(data);

  test_result_t r = {"Sierpinski", "max FPS", best, "FPS"};
  return r;
}

/* =====================================================================
 *  Test 4: Gradient
 * ===================================================================== */

static test_result_t test_gradient(void)
{
  unsigned int count, start_ms, end_ms, i, j, n;
  unsigned char c;
  struct timeval tv;

  count = 0;
  gettimeofday(&tv, NULL);
  start_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

  do {
    for (i = 0; i < bench_height; i++) {
      for (j = 0; j < bench_width; j++) {
        c = (unsigned char)(j * 255 / bench_width);
        for (n = 0; n < fb_pixel_size && n < 3; n++) {
          if (n == 0)
            *(pixel_at(j, i) + 2 - n) = (unsigned char)((i * j) % (bench_width * bench_height) * 255 / (bench_width * bench_height));
          else if (n == 1)
            *(pixel_at(j, i) + 2 - n) = (unsigned char)(i * 255 / bench_height);
          else
            *(pixel_at(j, i) + 2 - n) = c;
        }
        for (; n < fb_pixel_size; n++)
          *(pixel_at(j, i) + n) = 0;
      }
    }
    count++;
    gettimeofday(&tv, NULL);
    end_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  } while (end_ms < (start_ms + 5000));

  test_result_t r = {"Gradient fill", "throughput",
                     count * (double)bench_width * bench_height / ((end_ms - start_ms) * 1000.),
                     "MPixels/s"};
  return r;
}

/* =====================================================================
 *  Test 5: Blit
 * ===================================================================== */

static test_result_t test_blit(void)
{
  unsigned int bw, bh, sx, sy, dx, dy, i, count, start_ms, end_ms;
  struct timeval tv;

  /* pre-fill with pattern */
  for (i = 0; i < bench_height; i++)
    memset(pixel_at(0, i), (i / 16) % 2 ? 0xAA : 0x55, bench_width * fb_pixel_size);

  bw = bench_width / 4;
  bh = bench_height / 4;
  count = 0;
  gettimeofday(&tv, NULL);
  start_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

  do {
    sx = rand() % (bench_width - bw);
    sy = rand() % (bench_height - bh);
    dx = rand() % (bench_width - bw);
    dy = rand() % (bench_height - bh);

    for (i = 0; i < bh; i++)
      memcpy(pixel_at(dx, dy + i), pixel_at(sx, sy + i), bw * fb_pixel_size);

    count++;
    gettimeofday(&tv, NULL);
    end_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  } while (end_ms < (start_ms + 5000));

  test_result_t r = {"Blit copy", "throughput",
                     count * (double)bw * bh / ((end_ms - start_ms) * 1000.),
                     "MPixels/s"};
  return r;
}

/* =====================================================================
 *  Test 6: Bresenham lines
 * ===================================================================== */

static test_result_t test_line(void)
{
  unsigned int count, start_ms, end_ms;
  struct timeval tv;

  count = 0;
  gettimeofday(&tv, NULL);
  start_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

  do {
    int x0 = rand() % bench_width, y0 = rand() % bench_height;
    int x1 = rand() % bench_width, y1 = rand() % bench_height;
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    unsigned char rr = rand() % 256, gg = rand() % 256, bb = rand() % 256;

    for (;;) {
      if (x0 >= 0 && x0 < bench_width && y0 >= 0 && y0 < bench_height) {
        *(pixel_at(x0, y0) + 2) = rr;
        *(pixel_at(x0, y0) + 1) = gg;
        *(pixel_at(x0, y0) + 0) = bb;
      }
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 > -dy) { err -= dy; x0 += sx; }
      if (e2 <  dx) { err += dx; y0 += sy; }
    }

    count++;
    gettimeofday(&tv, NULL);
    end_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  } while (end_ms < (start_ms + 5000));

  test_result_t r = {"Line draw", "rate",
                     count * 1000. / (end_ms - start_ms),
                     "lines/s"};
  return r;
}

/* =====================================================================
 *  Test 7: Midpoint circles
 * ===================================================================== */

static test_result_t test_circle(void)
{
  unsigned int count, start_ms, end_ms;
  struct timeval tv;

  count = 0;
  gettimeofday(&tv, NULL);
  start_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

  do {
    int cx = rand() % bench_width, cy = rand() % bench_height;
    int rad = (rand() % 64) + 8;
    unsigned char rr = rand() % 256, gg = rand() % 256, bb = rand() % 256;
    int x = 0, y = rad, d = 1 - rad;

    while (x <= y) {
      #define PLOT8(px, py) do { \
        if ((px) >= 0 && (px) < bench_width && (py) >= 0 && (py) < bench_height) { \
          *(pixel_at(px, py) + 2) = rr; \
          *(pixel_at(px, py) + 1) = gg; \
          *(pixel_at(px, py) + 0) = bb; \
        } } while(0)

      PLOT8(cx + x, cy + y); PLOT8(cx + y, cy + x);
      PLOT8(cx - x, cy + y); PLOT8(cx - y, cy + x);
      PLOT8(cx + x, cy - y); PLOT8(cx + y, cy - x);
      PLOT8(cx - x, cy - y); PLOT8(cx - y, cy - x);
      #undef PLOT8

      if (d < 0) { d += 2 * x + 3; }
      else       { d += 2 * (x - y) + 5; y--; }
      x++;
    }

    count++;
    gettimeofday(&tv, NULL);
    end_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  } while (end_ms < (start_ms + 5000));

  test_result_t r = {"Circle draw", "rate",
                     count * 1000. / (end_ms - start_ms),
                     "circles/s"};
  return r;
}

/* =====================================================================
 *  Test 8: Fullscreen fill
 * ===================================================================== */

static test_result_t test_fill(void)
{
  unsigned int count, start_ms, end_ms, i;
  struct timeval tv;

  count = 0;
  gettimeofday(&tv, NULL);
  start_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

  do {
    for (i = 0; i < bench_height; i++)
      memset(pixel_at(0, i), rand() % 256, bench_width * fb_pixel_size);
    count++;
    gettimeofday(&tv, NULL);
    end_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  } while (end_ms < (start_ms + 5000));

  test_result_t r = {"Fullscreen fill", "throughput",
                     count * (double)bench_width * bench_height / ((end_ms - start_ms) * 1000.),
                     "MPixels/s"};
  return r;
}

/* =====================================================================
 *  Test 9: Plasma
 * ===================================================================== */

static test_result_t test_plasma(void)
{
  unsigned int frame, n, i, j;
  float seconds, fps[16], avg;
  struct timeval last, now;

  n = 0;
  frame = 0;
  avg = 0;
  gettimeofday(&last, NULL);

  printf("\n  Plasma details:\n");

  do {
    for (i = 0; i < bench_height; i++) {
      for (j = 0; j < bench_width; j++) {
        float v = sinf(j * 0.03f + frame * 0.1f) +
                  sinf(i * 0.03f + frame * 0.07f) +
                  sinf(sqrtf((i - bench_height/2.f) * (i - bench_height/2.f) +
                             (j - bench_width/2.f)  * (j - bench_width/2.f)) * 0.04f + frame * 0.05f);
        *(pixel_at(j, i) + 2) = (unsigned char)((sinf(v * 1.f + 0) * 0.5f + 0.5f) * 255);
        *(pixel_at(j, i) + 1) = (unsigned char)((sinf(v * 1.f + 2) * 0.5f + 0.5f) * 255);
        *(pixel_at(j, i) + 0) = (unsigned char)((sinf(v * 1.f + 4) * 0.5f + 0.5f) * 255);
      }
    }

    frame++;
    gettimeofday(&now, NULL);
    seconds = (now.tv_sec - last.tv_sec) + (now.tv_usec - last.tv_usec) * 1e-6;
    if (seconds >= 1.f && n < 16) {
      fps[n] = frame / seconds;
      printf("    Frame %2d: %8.2f FPS\n", n + 1, fps[n]);
      last = now;
      frame = 0;
      n++;
    }
  } while (n < 8);

  for (i = 0; i < n; i++)
    avg += fps[i];
  if (n > 0) avg /= n;

  test_result_t r = {"Plasma effect", "avg FPS", avg, "FPS"};
  return r;
}

/* =====================================================================
 *  Test 10: Scroll
 * ===================================================================== */

static test_result_t test_scroll(void)
{
  unsigned int count, start_ms, end_ms, dy, i;
  struct timeval tv;

  /* pre-fill */
  for (i = 0; i < bench_height; i++)
    memset(pixel_at(0, i), (unsigned char)(i * 255 / bench_height), bench_width * fb_pixel_size);

  count = 0;
  gettimeofday(&tv, NULL);
  start_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

  do {
    dy = (rand() % 16) + 1;
    memmove(pixel_at(0, 0), pixel_at(0, dy), (bench_height - dy) * fb_row_size);
    for (i = bench_height - dy; i < bench_height; i++)
      memset(pixel_at(0, i), rand() % 256, bench_width * fb_pixel_size);
    count++;
    gettimeofday(&tv, NULL);
    end_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  } while (end_ms < (start_ms + 5000));

  test_result_t r = {"Scroll", "throughput",
                     count * (double)bench_width * bench_height / ((end_ms - start_ms) * 1000.),
                     "MPixels/s"};
  return r;
}

/* =====================================================================
 *  Test 11: Text rendering
 * ===================================================================== */

#define FONT_W    8
#define FONT_H   13
#define GLYPHS   95
#define FONT_BASE 0x20

static const unsigned char font_data[GLYPHS][FONT_H] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*   */
  {0x00,0x00,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, /* ! */
  {0x00,0x00,0x66,0x66,0x66,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* " */
  {0x00,0x00,0x24,0x24,0x7E,0x24,0x24,0x7E,0x24,0x24,0x00,0x00,0x00}, /* # */
  {0x00,0x00,0x08,0x3E,0x49,0x48,0x3E,0x09,0x49,0x3E,0x08,0x00,0x00}, /* $ */
  {0x00,0x00,0x61,0x92,0x64,0x08,0x10,0x26,0x49,0x86,0x00,0x00,0x00}, /* % */
  {0x00,0x00,0x1C,0x22,0x22,0x1C,0x29,0x46,0x42,0x3D,0x00,0x00,0x00}, /* & */
  {0x00,0x00,0x18,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ' */
  {0x00,0x00,0x04,0x08,0x10,0x10,0x10,0x10,0x08,0x04,0x00,0x00,0x00}, /* ( */
  {0x00,0x00,0x20,0x10,0x08,0x08,0x08,0x08,0x10,0x20,0x00,0x00,0x00}, /* ) */
  {0x00,0x00,0x00,0x08,0x49,0x2A,0x1C,0x2A,0x49,0x08,0x00,0x00,0x00}, /* * */
  {0x00,0x00,0x00,0x08,0x08,0x08,0x7F,0x08,0x08,0x08,0x00,0x00,0x00}, /* + */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x08,0x10,0x00}, /* , */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00,0x00,0x00}, /* - */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00}, /* . */
  {0x00,0x00,0x02,0x04,0x08,0x08,0x10,0x20,0x40,0x80,0x00,0x00,0x00}, /* / */
  {0x00,0x00,0x3C,0x42,0x46,0x4A,0x52,0x62,0x42,0x3C,0x00,0x00,0x00}, /* 0 */
  {0x00,0x00,0x08,0x18,0x28,0x08,0x08,0x08,0x08,0x3E,0x00,0x00,0x00}, /* 1 */
  {0x00,0x00,0x3C,0x42,0x02,0x04,0x08,0x10,0x20,0x7E,0x00,0x00,0x00}, /* 2 */
  {0x00,0x00,0x3C,0x42,0x02,0x1C,0x02,0x02,0x42,0x3C,0x00,0x00,0x00}, /* 3 */
  {0x00,0x00,0x04,0x0C,0x14,0x24,0x44,0x7E,0x04,0x04,0x00,0x00,0x00}, /* 4 */
  {0x00,0x00,0x7E,0x40,0x40,0x7C,0x02,0x02,0x42,0x3C,0x00,0x00,0x00}, /* 5 */
  {0x00,0x00,0x1C,0x20,0x40,0x7C,0x42,0x42,0x42,0x3C,0x00,0x00,0x00}, /* 6 */
  {0x00,0x00,0x7E,0x02,0x04,0x08,0x10,0x10,0x10,0x10,0x00,0x00,0x00}, /* 7 */
  {0x00,0x00,0x3C,0x42,0x42,0x3C,0x42,0x42,0x42,0x3C,0x00,0x00,0x00}, /* 8 */
  {0x00,0x00,0x3C,0x42,0x42,0x42,0x3E,0x02,0x04,0x38,0x00,0x00,0x00}, /* 9 */
  {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00,0x00,0x00}, /* : */
  {0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x08,0x10,0x00}, /* ; */
  {0x00,0x00,0x02,0x04,0x08,0x10,0x20,0x10,0x08,0x04,0x02,0x00,0x00}, /* < */
  {0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00,0x00,0x00}, /* = */
  {0x00,0x00,0x40,0x20,0x10,0x08,0x04,0x08,0x10,0x20,0x40,0x00,0x00}, /* > */
  {0x00,0x00,0x3C,0x42,0x02,0x04,0x08,0x10,0x00,0x10,0x10,0x00,0x00}, /* ? */
  {0x00,0x00,0x3C,0x42,0x42,0x4E,0x52,0x4E,0x40,0x3C,0x00,0x00,0x00}, /* @ */
  {0x00,0x00,0x18,0x24,0x42,0x42,0x7E,0x42,0x42,0x42,0x00,0x00,0x00}, /* A */
  {0x00,0x00,0x7C,0x42,0x42,0x7C,0x42,0x42,0x42,0x7C,0x00,0x00,0x00}, /* B */
  {0x00,0x00,0x3C,0x42,0x40,0x40,0x40,0x40,0x42,0x3C,0x00,0x00,0x00}, /* C */
  {0x00,0x00,0x78,0x44,0x42,0x42,0x42,0x42,0x44,0x78,0x00,0x00,0x00}, /* D */
  {0x00,0x00,0x7E,0x40,0x40,0x78,0x40,0x40,0x40,0x7E,0x00,0x00,0x00}, /* E */
  {0x00,0x00,0x7E,0x40,0x40,0x78,0x40,0x40,0x40,0x40,0x00,0x00,0x00}, /* F */
  {0x00,0x00,0x3C,0x42,0x40,0x40,0x4E,0x42,0x42,0x3C,0x00,0x00,0x00}, /* G */
  {0x00,0x00,0x42,0x42,0x42,0x7E,0x42,0x42,0x42,0x42,0x00,0x00,0x00}, /* H */
  {0x00,0x00,0x3E,0x08,0x08,0x08,0x08,0x08,0x08,0x3E,0x00,0x00,0x00}, /* I */
  {0x00,0x00,0x1F,0x04,0x04,0x04,0x04,0x04,0x44,0x38,0x00,0x00,0x00}, /* J */
  {0x00,0x00,0x42,0x44,0x48,0x50,0x70,0x48,0x44,0x42,0x00,0x00,0x00}, /* K */
  {0x00,0x00,0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x7E,0x00,0x00,0x00}, /* L */
  {0x00,0x00,0x42,0x66,0x5A,0x5A,0x42,0x42,0x42,0x42,0x00,0x00,0x00}, /* M */
  {0x00,0x00,0x42,0x62,0x52,0x4A,0x46,0x42,0x42,0x42,0x00,0x00,0x00}, /* N */
  {0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00}, /* O */
  {0x00,0x00,0x7C,0x42,0x42,0x42,0x7C,0x40,0x40,0x40,0x00,0x00,0x00}, /* P */
  {0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x4A,0x44,0x3A,0x00,0x00,0x00}, /* Q */
  {0x00,0x00,0x7C,0x42,0x42,0x7C,0x48,0x44,0x42,0x42,0x00,0x00,0x00}, /* R */
  {0x00,0x00,0x3C,0x42,0x40,0x3C,0x02,0x02,0x42,0x3C,0x00,0x00,0x00}, /* S */
  {0x00,0x00,0x7F,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x00}, /* T */
  {0x00,0x00,0x42,0x42,0x42,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00}, /* U */
  {0x00,0x00,0x42,0x42,0x42,0x24,0x24,0x18,0x18,0x18,0x00,0x00,0x00}, /* V */
  {0x00,0x00,0x42,0x42,0x42,0x42,0x5A,0x5A,0x66,0x42,0x00,0x00,0x00}, /* W */
  {0x00,0x00,0x42,0x42,0x24,0x18,0x18,0x24,0x42,0x42,0x00,0x00,0x00}, /* X */
  {0x00,0x00,0x41,0x22,0x14,0x08,0x08,0x08,0x08,0x08,0x00,0x00,0x00}, /* Y */
  {0x00,0x00,0x7E,0x02,0x04,0x08,0x10,0x20,0x40,0x7E,0x00,0x00,0x00}, /* Z */
  {0x00,0x00,0x1E,0x10,0x10,0x10,0x10,0x10,0x10,0x1E,0x00,0x00,0x00}, /* [ */
  {0x00,0x00,0x80,0x40,0x20,0x10,0x10,0x08,0x04,0x02,0x00,0x00,0x00}, /* \ */
  {0x00,0x00,0x78,0x08,0x08,0x08,0x08,0x08,0x08,0x78,0x00,0x00,0x00}, /* ] */
  {0x00,0x00,0x08,0x14,0x22,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ^ */
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x7E,0x00,0x00}, /* _ */
  {0x00,0x00,0x10,0x08,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ` */
  {0x00,0x00,0x00,0x00,0x3C,0x02,0x3E,0x42,0x46,0x3A,0x00,0x00,0x00}, /* a */
  {0x00,0x00,0x40,0x40,0x5C,0x62,0x42,0x42,0x62,0x5C,0x00,0x00,0x00}, /* b */
  {0x00,0x00,0x00,0x00,0x3C,0x42,0x40,0x40,0x42,0x3C,0x00,0x00,0x00}, /* c */
  {0x00,0x00,0x02,0x02,0x3A,0x46,0x42,0x42,0x46,0x3A,0x00,0x00,0x00}, /* d */
  {0x00,0x00,0x00,0x00,0x3C,0x42,0x7E,0x40,0x42,0x3C,0x00,0x00,0x00}, /* e */
  {0x00,0x00,0x0C,0x12,0x10,0x7C,0x10,0x10,0x10,0x10,0x00,0x00,0x00}, /* f */
  {0x00,0x00,0x00,0x00,0x3A,0x46,0x42,0x46,0x3A,0x02,0x42,0x3C,0x00}, /* g */
  {0x00,0x00,0x40,0x40,0x5C,0x62,0x42,0x42,0x42,0x42,0x00,0x00,0x00}, /* h */
  {0x00,0x00,0x08,0x00,0x18,0x08,0x08,0x08,0x08,0x3E,0x00,0x00,0x00}, /* i */
  {0x00,0x00,0x04,0x00,0x0C,0x04,0x04,0x04,0x04,0x44,0x44,0x38,0x00}, /* j */
  {0x00,0x00,0x40,0x40,0x44,0x48,0x50,0x70,0x48,0x44,0x00,0x00,0x00}, /* k */
  {0x00,0x00,0x18,0x08,0x08,0x08,0x08,0x08,0x08,0x3E,0x00,0x00,0x00}, /* l */
  {0x00,0x00,0x00,0x00,0x76,0x49,0x49,0x49,0x49,0x49,0x00,0x00,0x00}, /* m */
  {0x00,0x00,0x00,0x00,0x5C,0x62,0x42,0x42,0x42,0x42,0x00,0x00,0x00}, /* n */
  {0x00,0x00,0x00,0x00,0x3C,0x42,0x42,0x42,0x42,0x3C,0x00,0x00,0x00}, /* o */
  {0x00,0x00,0x00,0x00,0x5C,0x62,0x42,0x42,0x62,0x5C,0x40,0x40,0x00}, /* p */
  {0x00,0x00,0x00,0x00,0x3A,0x46,0x42,0x42,0x46,0x3A,0x02,0x02,0x00}, /* q */
  {0x00,0x00,0x00,0x00,0x5C,0x62,0x40,0x40,0x40,0x40,0x00,0x00,0x00}, /* r */
  {0x00,0x00,0x00,0x00,0x3E,0x40,0x3C,0x02,0x02,0x7C,0x00,0x00,0x00}, /* s */
  {0x00,0x00,0x10,0x10,0x7C,0x10,0x10,0x10,0x12,0x0C,0x00,0x00,0x00}, /* t */
  {0x00,0x00,0x00,0x00,0x42,0x42,0x42,0x42,0x46,0x3A,0x00,0x00,0x00}, /* u */
  {0x00,0x00,0x00,0x00,0x42,0x42,0x24,0x24,0x18,0x18,0x00,0x00,0x00}, /* v */
  {0x00,0x00,0x00,0x00,0x41,0x49,0x49,0x49,0x49,0x36,0x00,0x00,0x00}, /* w */
  {0x00,0x00,0x00,0x00,0x42,0x24,0x18,0x18,0x24,0x42,0x00,0x00,0x00}, /* x */
  {0x00,0x00,0x00,0x00,0x42,0x42,0x42,0x46,0x3A,0x02,0x42,0x3C,0x00}, /* y */
  {0x00,0x00,0x00,0x00,0x7E,0x04,0x08,0x10,0x20,0x7E,0x00,0x00,0x00}, /* z */
  {0x00,0x00,0x06,0x08,0x08,0x08,0x30,0x08,0x08,0x08,0x06,0x00,0x00}, /* { */
  {0x00,0x00,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x00,0x00}, /* | */
  {0x00,0x00,0x60,0x10,0x10,0x10,0x0C,0x10,0x10,0x10,0x60,0x00,0x00}, /* } */
  {0x00,0x00,0x00,0x00,0x00,0x32,0x4C,0x00,0x00,0x00,0x00,0x00,0x00}, /* ~ */
};

/* ---- framebuffer text rendering ---- */

static void fb_draw_char(int cx, int cy, char ch, unsigned char r, unsigned char g, unsigned char b)
{
  int row, col;
  unsigned char glyph;

  if (ch < FONT_BASE || ch >= FONT_BASE + GLYPHS)
    return;

  for (row = 0; row < FONT_H; row++) {
    glyph = font_data[ch - FONT_BASE][row];
    for (col = 0; col < FONT_W; col++) {
      if (glyph & (0x80 >> col)) {
        int px = cx + col;
        int py = cy + row;
        if (px >= 0 && px < bench_width && py >= 0 && py < bench_height) {
          *(pixel_at(px, py) + 2) = r;
          *(pixel_at(px, py) + 1) = g;
          *(pixel_at(px, py) + 0) = b;
        }
      }
    }
  }
}

static void fb_draw_string(int x, int y, const char *s, unsigned char r, unsigned char g, unsigned char b)
{
  while (*s) {
    fb_draw_char(x, y, *s, r, g, b);
    x += FONT_W;
    s++;
  }
}

/* Render the scoreboard on the framebuffer if resolution is sufficient.
 * The layout is roughly 50-56 characters wide (~400-448 px at 8px font),
 * so we need at least that much horizontal space plus some margin.
 * We also need ~20 lines vertically (~260 px at 13px line height). */
static void fb_show_scoreboard(const test_result_t results[TEST_COUNT],
                                const int selected[TEST_COUNT],
                                double total_score, float total_sec)
{
  int y, i;
  char buf[128];
  int content_w_chars = 52;  /* conservative estimate of the widest line in characters */
  int content_w_px    = content_w_chars * FONT_W;
  int margin_x, margin_y;
  int sel_count = 0;

  for (i = 0; i < TEST_COUNT; i++)
    if (selected[i]) sel_count++;

  /* Need at least enough pixels to fit the content */
  if (bench_width < content_w_px + 16 || bench_height < 260)
    return;

  clear_screen();

  /* Centre the scoreboard horizontally; vertically start at 10 % from the top */
  margin_x = (bench_width - content_w_px) / 2;
  if (margin_x < 8) margin_x = 8;
  margin_y = bench_height / 10;
  if (margin_y < 8) margin_y = 8;

  /* ---- title (white) ---- */
  y = margin_y;
  fb_draw_string(margin_x, y, "fbmark - Linux Framebuffer Benchmark", 255, 255, 255);
  y += FONT_H + 6;

  /* ---- column header (cyan) ---- */
  fb_draw_string(margin_x, y, "Test                 Metric         Value  Unit", 0, 220, 220);
  y += FONT_H + 2;
  fb_draw_string(margin_x, y, "------------------------------------------------", 100, 100, 100);
  y += FONT_H + 2;

  /* ---- each selected test result (light gray) ---- */
  for (i = 0; i < TEST_COUNT; i++) {
    if (!selected[i]) continue;
    snprintf(buf, sizeof(buf), "%-20s %-14s %8.2f  %s",
             results[i].name, results[i].metric, results[i].value, results[i].unit);
    fb_draw_string(margin_x, y, buf, 200, 200, 200);
    y += FONT_H + 1;
  }

  y += 4;
  fb_draw_string(margin_x, y, "------------------------------------------------", 100, 100, 100);
  y += FONT_H + 4;

  /* ---- total line (yellow) ---- */
  snprintf(buf, sizeof(buf), "Total: %8.2f s     Score: %8.1f", total_sec, total_score);
  fb_draw_string(margin_x, y, buf, 255, 220, 50);

  /* ---- "exit" hint at the bottom of the screen (dark gray) ---- */
  y = bench_height - FONT_H - 12;
  if (y > margin_y + (sel_count + 6) * (FONT_H + 1)) {  /* only if there is room */
    fb_draw_string(margin_x, y, "Press Enter to exit...", 120, 120, 120);
  }
}

static test_result_t test_text(void)
{
  unsigned int count, start_ms, end_ms, cols, rows, cx, cy;
  struct timeval tv;
  const char *msg = "The quick brown fox jumps over the lazy dog 0123456789";
  int msg_len = strlen(msg);

  cols = bench_width / FONT_W;
  rows = bench_height / FONT_H;
  count = 0;
  gettimeofday(&tv, NULL);
  start_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

  do {
    for (cy = 0; cy < rows; cy++) {
      for (cx = 0; cx < cols; cx++) {
        char ch = msg[rand() % msg_len];
        if (ch >= FONT_BASE && ch < FONT_BASE + GLYPHS) {
          int row, col;
          for (row = 0; row < FONT_H; row++) {
            unsigned char glyph = font_data[ch - FONT_BASE][row];
            for (col = 0; col < FONT_W; col++) {
              if (glyph & (0x80 >> col)) {
                int px = cx * FONT_W + col;
                int py = cy * FONT_H + row;
                *(pixel_at(px, py) + 2) = rand() % 256;
                *(pixel_at(px, py) + 1) = rand() % 256;
                *(pixel_at(px, py) + 0) = rand() % 256;
              }
            }
          }
        }
      }
    }
    count++;
    gettimeofday(&tv, NULL);
    end_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  } while (end_ms < (start_ms + 5000));

  test_result_t r = {"Text render", "rate",
                     count * (double)cols * rows * 1000. / (end_ms - start_ms),
                     "chars/s"};
  return r;
}

/* =====================================================================
 *  Test 12: Triangle raster
 * ===================================================================== */

typedef struct { int x, y; } point_t;

static test_result_t test_triangle(void)
{
  unsigned int count, start_ms, end_ms = 0;
  struct timeval tv;

  count = 0;
  gettimeofday(&tv, NULL);
  start_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;

  do {
    point_t v0, v1, v2, tmp;
    unsigned char rr = rand() % 256, gg = rand() % 256, bb = rand() % 256;
    int y, x;

    v0.x = rand() % bench_width;  v0.y = rand() % bench_height;
    v1.x = rand() % bench_width;  v1.y = rand() % bench_height;
    v2.x = rand() % bench_width;  v2.y = rand() % bench_height;

    /* sort by y */
    if (v0.y > v1.y) { tmp = v0; v0 = v1; v1 = tmp; }
    if (v0.y > v2.y) { tmp = v0; v0 = v2; v2 = tmp; }
    if (v1.y > v2.y) { tmp = v1; v1 = v2; v2 = tmp; }

    if (v2.y == v0.y) continue;

    for (y = v0.y; y <= v2.y; y++) {
      if (y < 0 || y >= bench_height) continue;

      float t = (float)(y - v0.y) / (v2.y - v0.y);
      int x1 = v0.x + (int)((v2.x - v0.x) * t);
      int x2;

      if (y < v1.y)
        x2 = v0.x + (int)((v1.x - v0.x) * (float)(y - v0.y) / (v1.y - v0.y + 1));
      else if (v2.y > v1.y)
        x2 = v1.x + (int)((v2.x - v1.x) * (float)(y - v1.y) / (v2.y - v1.y + 1));
      else
        x2 = v1.x;

      if (x1 > x2) { int swp = x1; x1 = x2; x2 = swp; }
      if (x2 < 0 || x1 >= bench_width) continue;
      if (x1 < 0) x1 = 0;
      if (x2 >= bench_width) x2 = bench_width - 1;

      for (x = x1; x <= x2; x++) {
        *(pixel_at(x, y) + 2) = rr;
        *(pixel_at(x, y) + 1) = gg;
        *(pixel_at(x, y) + 0) = bb;
      }
    }

    count++;
    gettimeofday(&tv, NULL);
    end_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  } while (end_ms < (start_ms + 5000));

  test_result_t r = {"Triangle fill", "rate",
                     count * 1000. / (end_ms - start_ms),
                     "triangles/s"};
  return r;
}

/* =====================================================================
 *  Test 13: Julia set
 * ===================================================================== */

static test_result_t test_julia(void)
{
  unsigned int iters, i, j, n;
  unsigned char c;
  float tmp, x, y, seconds;
  float cr = -0.7f, ci = 0.27f;
  struct timeval start, stop;

  gettimeofday(&start, NULL);
  iters = 1;
  while (iters <= 48) {
    for (i = 0; i < bench_height; i++) {
      for (j = 0; j < bench_width; j++) {
        x = 3.f * j / bench_width - 1.5f;
        y = 1.5f - 3.f * i / bench_height;
        c = 0;
        do {
          tmp = x;
          x = x * x - y * y + cr;
          y = 2.f * tmp * y + ci;
          if (x * x + y * y > 4.f) break;
        } while (++c < iters);
        for (n = 0; n < fb_pixel_size && n < 3; n++)
          *(pixel_at(j, i) + n) = c * 255 / iters;
      }
    }
    iters++;
  }
  gettimeofday(&stop, NULL);
  seconds = (stop.tv_sec - start.tv_sec) + (stop.tv_usec - start.tv_usec) / 1000000.;

  test_result_t r = {"Julia set", "time", seconds, "s  (lower is better)"};
  return r;
}

/* =====================================================================
 *  CSV output writer
 * ===================================================================== */

static void write_results_csv(const char *filename,
                              const test_result_t results[TEST_COUNT],
                              const int selected[TEST_COUNT],
                              double total_score, float total_sec,
                              const char *fbdev,
                              const char *model, const char *vendor,
                              const char *cpu)
{
  FILE *f = fopen(filename, "w");
  int i;

  if (!f) {
    fprintf(stderr, "Warning: could not open output file '%s': %m\n", filename);
    return;
  }

  /* CSV header */
  fprintf(f, "device_model,device_vendor,cpu_model,framebuffer,"
          "width,height,bpp,region_w,region_h,total_time_s,total_score,"
          "test_name,short_name,value,unit,metric,direction,score,ref_value\n");

  /* one row per selected test */
  for (i = 0; i < TEST_COUNT; i++) {
    double score;
    if (!selected[i]) continue;

    if (score_meta[i].direction == METRIC_HIGHER_BETTER)
      score = (results[i].value / score_meta[i].ref_value) * 100.0;
    else
      score = (score_meta[i].ref_value / results[i].value) * 100.0;
    if (score > 100.0) score = 100.0;
    if (score < 0.0)   score = 0.0;

    /* CSV-escape: if a field contains commas or quotes, wrap with quotes */
    fprintf(f, "\"%s\",\"%s\",\"%s\",\"%s\","
            "%d,%d,%d,%d,%d,%.2f,%.1f,"
            "\"%s\",\"%s\",%.2f,\"%s\",\"%s\",\"%s\",%.1f,%.1f\n",
            model ? model : "Unknown",
            vendor ? vendor : "Unknown",
            cpu ? cpu : "Unknown",
            fbdev,
            fb_info.xres, fb_info.yres, fb_info.bits_per_pixel,
            bench_width, bench_height,
            total_sec, total_score,
            results[i].name,
            test_table[i].short_name,
            results[i].value,
            results[i].unit,
            results[i].metric,
            score_meta[i].direction == METRIC_HIGHER_BETTER
                ? "higher_better" : "lower_better",
            score,
            score_meta[i].ref_value);
  }

  fclose(f);
  printf("  Results written to %s\n", filename);
}

/* =====================================================================
 *  JSON output writer
 * ===================================================================== */

static void write_results_json(const char *filename,
                               const test_result_t results[TEST_COUNT],
                               const int selected[TEST_COUNT],
                               double total_score, float total_sec,
                               const char *fbdev,
                               const char *model, const char *vendor,
                               const char *cpu)
{
  FILE *f = fopen(filename, "w");
  int i, first;

  if (!f) {
    fprintf(stderr, "Warning: could not open output file '%s': %m\n", filename);
    return;
  }

  fprintf(f, "{\n");
  fprintf(f, "  \"version\": \"%s\",\n", FBMARK_VERSION);
  fprintf(f, "  \"device_model\": \"%s\",\n",
          model ? model : "Unknown");
  fprintf(f, "  \"device_vendor\": \"%s\",\n",
          vendor ? vendor : "Unknown");
  fprintf(f, "  \"cpu_model\": \"%s\",\n",
          cpu ? cpu : "Unknown");
  fprintf(f, "  \"framebuffer\": \"%s\",\n", fbdev);
  fprintf(f, "  \"resolution\": { \"width\": %d, \"height\": %d, \"bpp\": %d },\n",
          fb_info.xres, fb_info.yres, fb_info.bits_per_pixel);
  fprintf(f, "  \"region\": { \"width\": %d, \"height\": %d, \"posx\": %d, \"posy\": %d },\n",
          bench_width, bench_height, bench_posx, bench_posy);
  fprintf(f, "  \"total_time_s\": %.2f,\n", total_sec);
  fprintf(f, "  \"total_score\": %.1f,\n", total_score);

  fprintf(f, "  \"tests\": [\n");
  first = 1;
  for (i = 0; i < TEST_COUNT; i++) {
    double score;
    if (!selected[i]) continue;

    if (score_meta[i].direction == METRIC_HIGHER_BETTER)
      score = (results[i].value / score_meta[i].ref_value) * 100.0;
    else
      score = (score_meta[i].ref_value / results[i].value) * 100.0;
    if (score < 0.0) score = 0.0;

    if (!first) fprintf(f, ",\n");
    first = 0;

    fprintf(f, "    { "
            "\"name\": \"%s\", "
            "\"short_name\": \"%s\", "
            "\"value\": %.2f, "
            "\"unit\": \"%s\", "
            "\"metric\": \"%s\", "
            "\"direction\": \"%s\", "
            "\"score\": %.1f, "
            "\"ref_value\": %.1f "
            "}",
            results[i].name,
            test_table[i].short_name,
            results[i].value,
            results[i].unit,
            results[i].metric,
            score_meta[i].direction == METRIC_HIGHER_BETTER
                ? "higher_better" : "lower_better",
            score,
            score_meta[i].ref_value);
  }
  fprintf(f, "\n  ]\n");
  fprintf(f, "}\n");

  fclose(f);
  printf("  Results written to %s\n", filename);
}

/* =====================================================================
 *  Main
 * ===================================================================== */

int main(int argc, char **argv)
{
  test_result_t results[TEST_COUNT];
  int selected[TEST_COUNT];
  int i, run_count, max_name_len = 0, max_metric_len = 0;
  struct timeval total_start, total_end;
  float total_sec;
  const char *fbdev;
  const char *output_file = NULL;
  const char *output_format = NULL;   /* "json" or "csv", NULL = auto-detect */
  const char *device_model = NULL;    /* NULL = auto-detect */

  /* default: run all tests */
  for (i = 0; i < TEST_COUNT; i++) selected[i] = 1;

  /* ---- parse command-line arguments ---- */
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      printf("Usage: fbmark [OPTIONS]\n");
      printf("Linux Framebuffer Benchmark\n");
      printf("\n");
      printf("Options:\n");
      printf("  -h, --help       Show this help message and exit\n");
      printf("  -v, --version    Show version information and exit\n");
      printf("  -l, --list       List all available tests and exit\n");
      printf("  -t, --test TEST  Run only specified tests (comma-separated list of\n");
      printf("                   test names or numbers, e.g. \"mandelbrot,line\" or\n");
      printf("                   \"1,3,5\"); default: run all tests\n");
      printf("  -o, --output FILE Write results to FILE (JSON or CSV, format\n");
      printf("                   auto-detected from extension; use --format to override)\n");
      printf("  -f, --format FMT  Output format: json or csv (default: detect from\n");
      printf("                    -o file extension, fallback to json)\n");
      printf("  -m, --model NAME  Device model name for output (default: auto-detect\n");
      printf("                    from /sys/class/dmi/id/ or /proc/device-tree/)\n");
      printf("\n");
      printf("Environment variables:\n");
      printf("  FRAMEBUFFER       Framebuffer device path (default: /dev/fb0)\n");
      printf("  WIDTH             Benchmark region width (default: screen width)\n");
      printf("  HEIGHT            Benchmark region height (default: screen height)\n");
      printf("  POSX              Benchmark region X offset (default: 0)\n");
      printf("  POSY              Benchmark region Y offset (default: 0)\n");
      printf("  SIERPINSKI_FPS    Minimum FPS for Sierpinski test (default: 4)\n");
      printf("\n");
      printf("Authors:\n");
      printf("  Nicolas Caramelli\n");
      printf("  Zheng Hua <hua.zheng@embeddedboys.com>\n");
      return 0;
    }
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
      printf("fbmark version " FBMARK_VERSION "\n");
      return 0;
    }
    if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
      int j;
      printf("Available tests:\n");
      for (j = 0; j < TEST_COUNT; j++) {
        printf("  %2d  %-14s  %s\n", j + 1, test_table[j].short_name,
               test_table[j].display_name);
      }
      return 0;
    }
    if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--test") == 0) {
      char *token;
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: -t/--test requires an argument\n");
        return 1;
      }
      /* clear selection — user specified explicit list */
      memset(selected, 0, sizeof(selected));
      token = strtok(argv[++i], ",");
      while (token) {
        char *endptr;
        long idx;
        /* trim leading whitespace */
        while (*token == ' ' || *token == '\t') token++;
        /* try numeric index first (1-based) */
        idx = strtol(token, &endptr, 10);
        if (*endptr == '\0' && idx >= 1 && idx <= TEST_COUNT) {
          selected[idx - 1] = 1;
        } else {
          /* match by short name (case-insensitive) */
          int j, found = 0;
          for (j = 0; j < TEST_COUNT; j++) {
            if (strcasecmp(token, test_table[j].short_name) == 0) {
              selected[j] = 1;
              found = 1;
              break;
            }
          }
          if (!found) {
            fprintf(stderr, "Warning: unknown test '%s' — ignored\n", token);
          }
        }
        token = strtok(NULL, ",");
      }
    }
    if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: -o/--output requires a filename argument\n");
        return 1;
      }
      output_file = argv[++i];
      continue;
    }
    if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--format") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: -f/--format requires an argument (json or csv)\n");
        return 1;
      }
      output_format = argv[++i];
      if (strcmp(output_format, "json") != 0 && strcmp(output_format, "csv") != 0) {
        fprintf(stderr, "Error: unknown format '%s' — use 'json' or 'csv'\n",
                output_format);
        return 1;
      }
      continue;
    }
    if (strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--model") == 0) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: -m/--model requires a device model name\n");
        return 1;
      }
      device_model = argv[++i];
      continue;
    }
  }

  /* ---- open framebuffer ---- */
  fbdev = getenv("FRAMEBUFFER") ? getenv("FRAMEBUFFER") : "/dev/fb0";
  fb_fd = open(fbdev, O_RDWR);
  if (fb_fd == -1) {
    printf("Failed to open Framebuffer device: %m\n");
    return 1;
  }

  ioctl(fb_fd, FBIOGET_VSCREENINFO, &fb_info);

  bench_width  = getenv("WIDTH")  ? atoi(getenv("WIDTH"))  : fb_info.xres;
  bench_height = getenv("HEIGHT") ? atoi(getenv("HEIGHT")) : fb_info.yres;
  bench_posx   = getenv("POSX")   ? atoi(getenv("POSX"))   : 0;
  bench_posy   = getenv("POSY")   ? atoi(getenv("POSY"))   : 0;

  fb_len        = fb_info.xres * fb_info.yres * fb_info.bits_per_pixel / 8;
  fb_pixel_size = fb_info.bits_per_pixel / 8;
  fb_row_size   = fb_info.xres * fb_pixel_size;
  fb_buffer     = mmap(NULL, fb_len, PROT_WRITE, MAP_SHARED, fb_fd, 0);

  /* ---- header ---- */
  printf("\n");
  printf("╔══════════════════════════════════════════════════════════════╗\n");
  printf("║              fbmark - Linux Framebuffer Benchmark            ║\n");
  printf("╠══════════════════════════════════════════════════════════════╣\n");
  printf("║  Device  : %-49s ║\n", fbdev);
  printf("║  Res     : %dx%d, %d bpp                                  ║\n",
         fb_info.xres, fb_info.yres, fb_info.bits_per_pixel);
  printf("║  Region  : %dx%d at (%d,%d)                              ║\n",
         bench_width, bench_height, bench_posx, bench_posy);
  printf("╚══════════════════════════════════════════════════════════════╝\n");
  printf("\n");

  gettimeofday(&total_start, NULL);

  /* ---- disable framebuffer cursor blink ---- */
  fb_console_init();

  /* ---- count selected tests ---- */
  run_count = 0;
  for (i = 0; i < TEST_COUNT; i++)
    if (selected[i]) run_count++;

  /* ---- run selected tests ---- */
  {
    int run_idx = 1;
    for (i = 0; i < TEST_COUNT; i++) {
      if (!selected[i]) continue;
      printf("  [%2d/%2d] %s...\n", run_idx, run_count,
             test_table[i].display_name);
      clear_screen();
      results[i] = test_table[i].func();
      run_idx++;
    }
  }

  gettimeofday(&total_end, NULL);
  total_sec = (total_end.tv_sec - total_start.tv_sec) +
              (total_end.tv_usec - total_start.tv_usec) / 1000000.;

  /* ---- compute overall score ---- */
  double total_score = compute_total_score(results, selected);

  /* ---- find max column widths ---- */
  for (i = 0; i < TEST_COUNT; i++) {
    int nl, ml;
    if (!selected[i]) continue;
    nl = strlen(results[i].name);
    ml = strlen(results[i].metric);
    if (nl > max_name_len) max_name_len = nl;
    if (ml > max_metric_len) max_metric_len = ml;
  }

  /* ---- scoreboard ---- */
  printf("\n");
  printf("╔══════════════════════════════════════════════════════════════════════╗\n");
  printf("║                        RESULTS SCOREBOARD                           ║\n");
  printf("╠════════════════╤══════════════╤═════════════════╤════════════════════╣\n");
  printf("║ %-*s │ %-*s │ %15s │ %-18s ║\n",
         max_name_len, "Test",
         max_metric_len, "Metric",
         "Value", "Unit");
  printf("╟────────────────┼──────────────┼─────────────────┼────────────────────╢\n");

  for (i = 0; i < TEST_COUNT; i++) {
    if (!selected[i]) continue;
    printf("║ %-*s │ %-*s │ %15.2f │ %-18s ║\n",
           max_name_len, results[i].name,
           max_metric_len, results[i].metric,
           results[i].value,
           results[i].unit);
  }

  printf("╠════════════════╧══════════════╧═════════════════╧════════════════════╣\n");
  printf("║  Total: %8.2f s  │  Score: %8.1f  (%d tests)%11s║\n",
         total_sec, total_score, run_count, "");
  printf("╚══════════════════════════════════════════════════════════════════════╝\n");
  printf("\n");

  /* ---- render scoreboard on framebuffer ---- */
  fb_show_scoreboard(results, selected, total_score, total_sec);

  /* ---- write output file if requested ---- */
  if (output_file) {
    const char *fmt = output_format;
    const char *model, *vendor, *cpu;

    /* auto-detect format from file extension */
    if (!fmt) {
      const char *dot = strrchr(output_file, '.');
      if (dot && strcasecmp(dot, ".csv") == 0)
        fmt = "csv";
      else
        fmt = "json";   /* default */
    }

    /* device info */
    model  = device_model ? device_model : detect_device_model();
    vendor = detect_device_vendor();
    cpu    = detect_cpu_model();

    if (strcmp(fmt, "csv") == 0) {
      write_results_csv(output_file, results, selected,
                        total_score, total_sec, fbdev,
                        model, vendor, cpu);
    } else {
      write_results_json(output_file, results, selected,
                         total_score, total_sec, fbdev,
                         model, vendor, cpu);
    }
  }

  /* Wait for the user to press Enter so they can read the scoreboard
   * on the framebuffer before we restore the text console. */
  printf("\n  Press Enter to exit...\n");
  getchar();

  /* ---- cleanup ---- */
  fb_console_restore();
  munmap(fb_buffer, fb_len);
  close(fb_fd);

  return 0;
}
