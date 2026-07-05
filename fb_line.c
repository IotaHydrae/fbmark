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

static void draw_line(unsigned char *buffer, int xres, int pixel_size,
                      int x0, int y0, int x1, int y1,
                      unsigned char r, unsigned char g, unsigned char b)
{
  int dx, dy, sx, sy, err, e2;

  dx = abs(x1 - x0);
  dy = abs(y1 - y0);
  sx = x0 < x1 ? 1 : -1;
  sy = y0 < y1 ? 1 : -1;
  err = dx - dy;

  for (;;) {
    if (x0 >= 0 && x0 < xres && y0 >= 0) {
      buffer[y0 * xres * pixel_size + x0 * pixel_size + 2] = r;
      buffer[y0 * xres * pixel_size + x0 * pixel_size + 1] = g;
      buffer[y0 * xres * pixel_size + x0 * pixel_size + 0] = b;
    }
    if (x0 == x1 && y0 == y1) break;
    e2 = 2 * err;
    if (e2 > -dy) { err -= dy; x0 += sx; }
    if (e2 <  dx) { err += dx; y0 += sy; }
  }
}

int main(int argc, char **argv)
{
  int fd, width, height, posx, posy;
  struct fb_var_screeninfo info;
  size_t len, pixel_size;
  unsigned char *buffer;
  unsigned int count, start, end;
  int x0, y0, x1, y1;
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
    x0 = rand() % width;
    y0 = rand() % height;
    x1 = rand() % width;
    y1 = rand() % height;

    draw_line(buffer + posy * info.xres * pixel_size + posx * pixel_size,
              info.xres, pixel_size,
              x0, y0, x1, y1,
              rand() % 256, rand() % 256, rand() % 256);

    count++;
    gettimeofday(&tv, NULL);
    end = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  } while (end < (start + 5000));

  printf("Line frame buffer test bench\n");
  printf("Benchmarking Bresenham lines: %d lines in %.2f seconds (%.2f lines/second)\n",
         count, (end - start) / 1000., count * 1000. / (end - start));

  fb_console_restore();
  munmap(buffer, len);
  close(fd);

  return 0;
}
