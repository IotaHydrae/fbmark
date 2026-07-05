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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "fb_util.h"

typedef struct {
  int x, y;
} point_t;

static void fill_triangle(unsigned char *buffer, int xres, int pixel_size,
                          point_t v0, point_t v1, point_t v2,
                          unsigned char r, unsigned char g, unsigned char b)
{
  point_t tmp;
  int x, y, x1, x2;

  /* Sort vertices by y: v0.y <= v1.y <= v2.y */
  if (v0.y > v1.y) { tmp = v0; v0 = v1; v1 = tmp; }
  if (v0.y > v2.y) { tmp = v0; v0 = v2; v2 = tmp; }
  if (v1.y > v2.y) { tmp = v1; v1 = v2; v2 = tmp; }

  if (v2.y == v0.y) return; /* degenerate */

  for (y = v0.y; y <= v2.y; y++) {
    if (y < 0) continue;

    /* Interpolate x along edges v0-v2 and either v0-v1 (top) or v1-v2 (bottom) */
    float t = (float)(y - v0.y) / (v2.y - v0.y);
    x1 = v0.x + (int)((v2.x - v0.x) * t);

    if (y < v1.y) {
      float s = (float)(y - v0.y) / (v1.y - v0.y + 1);
      x2 = v0.x + (int)((v1.x - v0.x) * s);
    } else if (v2.y > v1.y) {
      float s = (float)(y - v1.y) / (v2.y - v1.y + 1);
      x2 = v1.x + (int)((v2.x - v1.x) * s);
    } else {
      x2 = v1.x;
    }

    if (x1 > x2) { int swp = x1; x1 = x2; x2 = swp; }

    if (x2 < 0 || x1 >= xres) continue;
    if (x1 < 0) x1 = 0;
    if (x2 >= xres) x2 = xres - 1;

    for (x = x1; x <= x2; x++) {
      buffer[y * xres * pixel_size + x * pixel_size + 2] = r;
      buffer[y * xres * pixel_size + x * pixel_size + 1] = g;
      buffer[y * xres * pixel_size + x * pixel_size + 0] = b;
    }
  }
}

int main(int argc, char **argv)
{
  int fd, width, height, posx, posy;
  struct fb_var_screeninfo info;
  size_t len, pixel_size;
  unsigned char *buffer;
  unsigned int count, start, end;
  point_t v0, v1, v2;
  struct timeval tv;

  fd = getenv("FRAMEBUFFER") ? open(getenv("FRAMEBUFFER"), O_RDWR) : open("/dev/fb0", O_RDWR);
  if (fd == -1) {
    printf("Failed to open Framebuffer device: %m\n");
    return 1;
  }

  ioctl(fd, FBIOGET_VSCREENINFO, &info);

  if (getenv("WIDTH")) width = atoi(getenv("WIDTH"));
  else width = info.xres;
  if (getenv("HEIGHT")) height = atoi(getenv("HEIGHT"));
  else height = info.yres;
  if (getenv("POSX")) posx = atoi(getenv("POSX"));
  else posx = 0;
  if (getenv("POSY")) posy = atoi(getenv("POSY"));
  else posy = 0;

  len = info.xres * info.yres * info.bits_per_pixel / 8;
  pixel_size = info.bits_per_pixel / 8;
  buffer = mmap(NULL, len, PROT_WRITE, MAP_SHARED, fd, 0);
  fb_console_init();

  count = 0;
  gettimeofday(&tv, NULL);
  start = tv.tv_sec * 1000 + tv.tv_usec / 1000;

  do {
    v0.x = rand() % width;  v0.y = rand() % height;
    v1.x = rand() % width;  v1.y = rand() % height;
    v2.x = rand() % width;  v2.y = rand() % height;

    fill_triangle(buffer + posy * info.xres * pixel_size + posx * pixel_size,
                  info.xres, pixel_size, v0, v1, v2,
                  rand() % 256, rand() % 256, rand() % 256);

    count++;
    gettimeofday(&tv, NULL);
    end = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  } while (end < (start + 5000));

  printf("Triangle frame buffer test bench\n");
  printf("Benchmarking raster triangles: %d triangles in %.2f seconds (%.2f triangles/second)\n",
         count, (end - start) / 1000., count * 1000. / (end - start));

  fb_console_restore();
  munmap(buffer, len);
  close(fd);

  return 0;
}
