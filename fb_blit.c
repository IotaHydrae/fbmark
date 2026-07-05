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

int main(int argc, char **argv)
{
  int fd, width, height, posx, posy;
  struct fb_var_screeninfo info;
  size_t len, pixel_size, row_size;
  unsigned char *buffer, *src, *dst;
  unsigned int bw, bh, sx, sy, dx, dy, i, count, start, end;
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
  row_size = info.xres * pixel_size;
  buffer = mmap(NULL, len, PROT_WRITE, MAP_SHARED, fd, 0);
  fb_console_init();

  /* Fill a checkerboard pattern first, then blit blocks around */
  for (i = 0; i < height; i++) {
    memset(buffer + (posy + i) * row_size + posx * pixel_size,
           (i / 16) % 2 ? 0xAA : 0x55, width * pixel_size);
  }

  bw = width / 4;
  bh = height / 4;
  count = 0;
  gettimeofday(&tv, NULL);
  start = tv.tv_sec * 1000 + tv.tv_usec / 1000;

  do {
    sx = rand() % (width - bw);
    sy = rand() % (height - bh);
    dx = rand() % (width - bw);
    dy = rand() % (height - bh);

    src = buffer + (posy + sy) * row_size + (posx + sx) * pixel_size;
    dst = buffer + (posy + dy) * row_size + (posx + dx) * pixel_size;

    for (i = 0; i < bh; i++)
      memcpy(dst + i * row_size, src + i * row_size, bw * pixel_size);

    count++;
    gettimeofday(&tv, NULL);
    end = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  } while (end < (start + 5000));

  printf("Blit frame buffer test bench\n");
  printf("Benchmarking %dx%d block copy: %.2f MPixels/second\n", bw, bh,
         count * (double)bw * bh / ((end - start) * 1000.));

  fb_console_restore();
  munmap(buffer, len);
  close(fd);

  return 0;
}
