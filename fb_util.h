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

#ifndef FB_UTIL_H
#define FB_UTIL_H

#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/kd.h>
#include <sys/ioctl.h>

/* file-scope tty fd so the signal handler can reach it */
static int fb_util_tty_fd = -1;

static void fb_console_restore(void)
{
  if (fb_util_tty_fd != -1) {
    /* DECTCEM: show cursor */
    const char *show_cursor = "\033[?25h";
    write(fb_util_tty_fd, show_cursor, strlen(show_cursor));
    /* switch back to text mode */
    ioctl(fb_util_tty_fd, KDSETMODE, KD_TEXT);
    close(fb_util_tty_fd);
    fb_util_tty_fd = -1;
  }
}

static void fb_sig_handler(int sig)
{
  fb_console_restore();
  _exit(1);
}

/*
 * Initialise the console for framebuffer graphics:
 *   - open /dev/tty0 (the active virtual console)
 *   - switch to KD_GRAPHICS mode (disables kernel's hardware cursor)
 *   - send DECTCEM escape to hide the text cursor
 *   - install signal handlers so cleanup runs on Ctrl-C / kill
 *
 * Returns the tty fd on success, or -1 on failure (caller may ignore).
 */
static int fb_console_init(void)
{
  int fd = open("/dev/tty0", O_RDWR);
  if (fd == -1)
    return -1;

  fb_util_tty_fd = fd;

  /* switch to graphics mode (suppresses fbcon hardware cursor) */
  ioctl(fd, KDSETMODE, KD_GRAPHICS);

  /* VT100 DECTCEM: hide cursor */
  const char *hide_cursor = "\033[?25l";
  write(fd, hide_cursor, strlen(hide_cursor));

  /* install signal handlers */
  signal(SIGINT, fb_sig_handler);
  signal(SIGTERM, fb_sig_handler);

  return fd;
}

#endif /* FB_UTIL_H */
