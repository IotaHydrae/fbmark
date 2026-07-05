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
  unsigned char *buffer;
  unsigned int frame, n, i, j;
  float seconds, fps[16];
  struct timeval last, now;

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

  n = 0;
  frame = 0;
  gettimeofday(&last, NULL);

  do {
    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        float v;

        v = sinf(j * 0.03f + frame * 0.1f) +
            sinf(i * 0.03f + frame * 0.07f) +
            sinf(sqrtf((i - height/2.f) * (i - height/2.f) +
                       (j - width/2.f)  * (j - width/2.f)) * 0.04f + frame * 0.05f);

        buffer[(posy + i) * info.xres * pixel_size + (posx + j) * pixel_size + 2] =
          (unsigned char)((sinf(v * 1.0f + 0) * 0.5f + 0.5f) * 255);
        buffer[(posy + i) * info.xres * pixel_size + (posx + j) * pixel_size + 1] =
          (unsigned char)((sinf(v * 1.0f + 2) * 0.5f + 0.5f) * 255);
        buffer[(posy + i) * info.xres * pixel_size + (posx + j) * pixel_size + 0] =
          (unsigned char)((sinf(v * 1.0f + 4) * 0.5f + 0.5f) * 255);
      }
    }

    frame++;
    gettimeofday(&now, NULL);
    seconds = (now.tv_sec - last.tv_sec) + (now.tv_usec - last.tv_usec) * 1e-6;
    if (seconds >= 1 && n < 16) {
      fps[n] = frame / seconds;
      last = now;
      frame = 0;
      n++;
    }
  } while (n < 8);

  printf("Plasma frame buffer test bench\n\n");
  for (i = 1; i <= n; i++)
    printf("Frame %2d: %8.2f Frames/second\n", i, fps[i - 1]);
  printf("\nAverage: %.2f FPS\n", n > 0 ?
         (fps[0] + fps[1] + fps[2] + fps[3] + fps[4] + fps[5] + fps[6] + fps[7]) / n : 0.f);

  fb_console_restore();
  munmap(buffer, len);
  close(fd);

  return 0;
}
