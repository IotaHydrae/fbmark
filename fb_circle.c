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
#include <unistd.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "fb_util.h"

static void draw_circle(unsigned char *buffer, int xres, int pixel_size,
                        int cx, int cy, int r,
                        unsigned char red, unsigned char green, unsigned char blue)
{
  int x, y, d, px, py;

  x = 0;
  y = r;
  d = 1 - r;

  while (x <= y) {
    /* Eight symmetric points per octant */
    px = cx + x; py = cy + y;
    if (px >= 0 && px < xres && py >= 0) {
      buffer[py * xres * pixel_size + px * pixel_size + 2] = red;
      buffer[py * xres * pixel_size + px * pixel_size + 1] = green;
      buffer[py * xres * pixel_size + px * pixel_size + 0] = blue;
    }
    px = cx + y; py = cy + x;
    if (px >= 0 && px < xres && py >= 0) {
      buffer[py * xres * pixel_size + px * pixel_size + 2] = red;
      buffer[py * xres * pixel_size + px * pixel_size + 1] = green;
      buffer[py * xres * pixel_size + px * pixel_size + 0] = blue;
    }
    px = cx - x; py = cy + y;
    if (px >= 0 && px < xres && py >= 0) {
      buffer[py * xres * pixel_size + px * pixel_size + 2] = red;
      buffer[py * xres * pixel_size + px * pixel_size + 1] = green;
      buffer[py * xres * pixel_size + px * pixel_size + 0] = blue;
    }
    px = cx - y; py = cy + x;
    if (px >= 0 && px < xres && py >= 0) {
      buffer[py * xres * pixel_size + px * pixel_size + 2] = red;
      buffer[py * xres * pixel_size + px * pixel_size + 1] = green;
      buffer[py * xres * pixel_size + px * pixel_size + 0] = blue;
    }
    px = cx + x; py = cy - y;
    if (px >= 0 && px < xres && py >= 0) {
      buffer[py * xres * pixel_size + px * pixel_size + 2] = red;
      buffer[py * xres * pixel_size + px * pixel_size + 1] = green;
      buffer[py * xres * pixel_size + px * pixel_size + 0] = blue;
    }
    px = cx + y; py = cy - x;
    if (px >= 0 && px < xres && py >= 0) {
      buffer[py * xres * pixel_size + px * pixel_size + 2] = red;
      buffer[py * xres * pixel_size + px * pixel_size + 1] = green;
      buffer[py * xres * pixel_size + px * pixel_size + 0] = blue;
    }
    px = cx - x; py = cy - y;
    if (px >= 0 && px < xres && py >= 0) {
      buffer[py * xres * pixel_size + px * pixel_size + 2] = red;
      buffer[py * xres * pixel_size + px * pixel_size + 1] = green;
      buffer[py * xres * pixel_size + px * pixel_size + 0] = blue;
    }
    px = cx - y; py = cy - x;
    if (px >= 0 && px < xres && py >= 0) {
      buffer[py * xres * pixel_size + px * pixel_size + 2] = red;
      buffer[py * xres * pixel_size + px * pixel_size + 1] = green;
      buffer[py * xres * pixel_size + px * pixel_size + 0] = blue;
    }

    if (d < 0) {
      d += 2 * x + 3;
    } else {
      d += 2 * (x - y) + 5;
      y--;
    }
    x++;
  }
}

int main(int argc, char **argv)
{
  int fd, width, height, posx, posy;
  struct fb_var_screeninfo info;
  size_t len, pixel_size;
  unsigned char *buffer;
  unsigned int count, start, end, r;
  int cx, cy;
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
    cx = rand() % width;
    cy = rand() % height;
    r = (rand() % 64) + 8;

    draw_circle(buffer + posy * info.xres * pixel_size + posx * pixel_size,
                info.xres, pixel_size, cx, cy, r,
                rand() % 256, rand() % 256, rand() % 256);

    count++;
    gettimeofday(&tv, NULL);
    end = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  } while (end < (start + 5000));

  printf("Circle frame buffer test bench\n");
  printf("Benchmarking midpoint circles: %d circles in %.2f seconds (%.2f circles/second)\n",
         count, (end - start) / 1000., count * 1000. / (end - start));

  fb_console_restore();
  munmap(buffer, len);
  close(fd);

  return 0;
}
