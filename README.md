# fbmark

[English](README.md) | [中文](README_zh.md)

**v1.0.1** — Linux Framebuffer Benchmark Suite — a collection of 13 graphics benchmarks that render directly to `/dev/fb0` via `mmap` and measure throughput, frame rate, or elapsed time.

## Features

- 13 benchmark tests covering common 2D graphics operations
- Direct framebuffer rendering via `mmap` — no X11/Wayland/GPU required
- Works on any Linux system with framebuffer support (`/dev/fb0`)
- File-backed buffer support for headless/CI environments
- Compact two-file implementation (`fbmark.c` + `fb_util.h`) — easy to port and understand
- Score normalization and per-test scoring with a total score summary
- JSON output support for visualization and automation (`-o` flag)

## Requirements

- Linux with framebuffer support (`/dev/fb0`)
- GCC or Clang
- Standard C library (`libc`, `libm`)

## Building

```bash
make                          # build fbmark.out
make clean                    # remove fbmark.out
make install PREFIX=/usr      # install to DESTDIR (default /usr/local)
```

Override compiler and flags:

```bash
make CC=clang CFLAGS="-O3 -march=native" LDFLAGS="-static"
```

## Running

```bash
./fbmark.out                  # run the full suite (13 tests)
```

### Environment variables

| Variable           | Default        | Description                            |
|--------------------|----------------|----------------------------------------|
| `FRAMEBUFFER`      | `/dev/fb0`     | Framebuffer device path                |
| `WIDTH`            | screen width   | Benchmark region width                 |
| `HEIGHT`           | screen height  | Benchmark region height                |
| `POSX`             | 0              | Benchmark region X offset              |
| `POSY`             | 0              | Benchmark region Y offset              |
| `SIERPINSKI_FPS`   | 4              | Minimum FPS for Sierpinski test        |

Example:

```bash
FRAMEBUFFER=/dev/fb1 WIDTH=800 HEIGHT=600 ./fbmark.out
```

### Running without a real framebuffer

On systems without a real framebuffer (e.g. CI), point `FRAMEBUFFER` at a file-backed buffer:

```bash
dd if=/dev/zero of=/tmp/fb bs=1K count=8192
FRAMEBUFFER=/tmp/fb ./fbmark.out
```

## The 13 tests

| #  | Test            | Metric      | Direction      |
|----|-----------------|-------------|----------------|
|  1 | Mandelbrot      | time (s)    | lower better   |
|  2 | Rectangle fill  | MPixels/s   | higher better  |
|  3 | Sierpinski      | max FPS     | higher better  |
|  4 | Gradient fill   | MPixels/s   | higher better  |
|  5 | Blit copy       | MPixels/s   | higher better  |
|  6 | Line draw       | lines/s     | higher better  |
|  7 | Circle draw     | circles/s   | higher better  |
|  8 | Fullscreen fill | MPixels/s   | higher better  |
|  9 | Plasma effect   | avg FPS     | higher better  |
| 10 | Scroll          | MPixels/s   | higher better  |
| 11 | Text render     | chars/s     | higher better  |
| 12 | Triangle fill   | triangles/s | higher better  |
| 13 | Julia set       | time (s)    | lower better   |

Scoring: each test's raw value is normalized against a reference value, clamped to [0, 100], and averaged across all 13 tests to produce a total score.

## Architecture

Two files:

1. **Shared utility header** (`fb_util.h`) — console initialization and cleanup. Opens `/dev/tty0`, switches to `KD_GRAPHICS` mode, hides the text cursor, installs signal handlers. Included directly (`static` functions, no separate compilation).

2. **Benchmark program** (`fbmark.c`): includes all 13 tests as `static` functions called sequentially from `main()`, then prints a formatted scoreboard and computes a total score.

Program lifecycle:

1. Open `$FRAMEBUFFER` (default `/dev/fb0`) with `O_RDWR`
2. `ioctl(FBIOGET_VSCREENINFO)` to get screen dimensions and bpp
3. `mmap(NULL, len, PROT_WRITE, MAP_SHARED, fd, 0)` to map the framebuffer
4. `fb_console_init()` to switch to graphics mode
5. Run the rendering loop, measuring with `gettimeofday`
6. `fb_console_restore()`, `munmap`, `close(fd)`

## License

GPL v3+ — see source file headers for details.
