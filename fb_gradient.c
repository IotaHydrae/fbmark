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

int main(int argc, char **argv)
{
  int fd, width, height, posx, posy;
  struct fb_var_screeninfo info;
  size_t len, pixel_size;
  unsigned char *buffer, c;
  unsigned int i, j, n, count, start, end;
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
    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        /* RGB components: diagonal, vertical, horizontal gradients */
        c = (unsigned char)(j * 255 / width);
        for (n = 0; n < pixel_size && n < 3; n++) {
          if (n == 0)      /* R: diagonal pattern */
            buffer[(posy + i) * info.xres * pixel_size + (posx + j) * pixel_size + 2 - n] =
              (unsigned char)((i * j) % (width * height) * 255 / (width * height));
          else if (n == 1) /* G: vertical gradient */
            buffer[(posy + i) * info.xres * pixel_size + (posx + j) * pixel_size + 2 - n] =
              (unsigned char)(i * 255 / height);
          else             /* B: horizontal gradient */
            buffer[(posy + i) * info.xres * pixel_size + (posx + j) * pixel_size + 2 - n] = c;
        }
        for (; n < pixel_size; n++)
          buffer[(posy + i) * info.xres * pixel_size + (posx + j) * pixel_size + n] = 0;
      }
    }

    count++;
    gettimeofday(&tv, NULL);
    end = tv.tv_sec * 1000 + tv.tv_usec / 1000;
  } while (end < (start + 5000));

  printf("Gradient frame buffer test bench\n");
  printf("Benchmarking %dx%d gradient fill: %d frames in %.2f seconds\n",
         width, height, count, (end - start) / 1000.);
  printf("Throughput: %.2f MPixels/second\n",
         count * (double)width * height / ((end - start) * 1000.));

  fb_console_restore();
  munmap(buffer, len);
  close(fd);

  return 0;
}
