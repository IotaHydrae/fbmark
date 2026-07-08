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
- JSON and CSV output support for visualization and automation (`-o` flag)
- Device auto-detection (model, vendor, CPU info) via `/sys` and `/proc`
- `visualize.py` script for generating charts from benchmark results
- CMake build support in addition to the Makefile

## Requirements

- Linux with framebuffer support (`/dev/fb0`)
- GCC or Clang
- Standard C library (`libc`, `libm`)

## Building

### Make

```bash
make                          # build fbmark.out
make clean                    # remove fbmark.out
make install PREFIX=/usr      # install to DESTDIR (default /usr/local)
```

Override compiler and flags:

```bash
make CC=clang CFLAGS="-O3 -march=native" LDFLAGS="-static"
```

### CMake

```bash
mkdir build && cd build
cmake ..
make                          # build fbmark.out
make install                  # install to prefix (default /usr/local)
```

## Usage

```
fbmark [OPTIONS]
```

### Options

| Option              | Description                                     |
|---------------------|-------------------------------------------------|
| `-h`, `--help`      | Show help message and exit                       |
| `-v`, `--version`   | Show version information and exit                |
| `-l`, `--list`      | List all available tests and exit                |
| `-t`, `--test LIST` | Run only specified tests (comma-separated names or numbers, e.g. `"mandelbrot,line"` or `"1,3,5"`) |
| `-o`, `--output FILE` | Write results to FILE (JSON or CSV, format auto-detected from extension) |
| `-f`, `--format FMT`  | Output format: `json` or `csv` (default: detect from `-o` file extension, fallback to json) |
| `-m`, `--model NAME`  | Device model name for output (default: auto-detect from `/sys/class/dmi/id/` or `/proc/device-tree/`) |

### Environment Variables

| Variable            | Default        | Description                            |
|---------------------|----------------|----------------------------------------|
| `FRAMEBUFFER`       | `/dev/fb0`     | Framebuffer device path                |
| `WIDTH`             | screen width   | Benchmark region width                 |
| `HEIGHT`            | screen height  | Benchmark region height                |
| `POSX`              | 0              | Benchmark region X offset              |
| `POSY`              | 0              | Benchmark region Y offset              |
| `SIERPINSKI_FPS`    | 4              | Minimum FPS for Sierpinski test        |

### Examples

```bash
# Run all tests
./fbmark.out

# Run specific tests only
./fbmark.out -t mandelbrot,rectangle,fill

# Run tests by number
./fbmark.out -t 1,2,8

# Custom framebuffer and region
FRAMEBUFFER=/dev/fb1 WIDTH=800 HEIGHT=600 ./fbmark.out

# Export results to JSON
./fbmark.out -o results.json

# Export results to CSV
./fbmark.out -o results.csv

# Force format regardless of file extension
./fbmark.out -o results.txt -f json

# Set device model name
./fbmark.out -o results.json -m "Raspberry Pi 4"
```

### Running Without a Real Framebuffer

On systems without a real framebuffer (e.g. CI), point `FRAMEBUFFER` at a file-backed buffer:

```bash
dd if=/dev/zero of=/tmp/fb bs=1K count=8192
FRAMEBUFFER=/tmp/fb ./fbmark.out
```

## Visualization

Use `visualize.py` to generate charts from benchmark results (requires `matplotlib`):

```bash
# Install dependency
pip install matplotlib

# Run benchmark and export results
./fbmark.out -o results.json

# Generate chart (supports both .json and .csv input)
python3 visualize.py results.json
```

This produces a `results.png` containing:
- **Raw values chart** — bar chart of raw benchmark values (log scale, grouped by test)
- **Normalized scores chart** — bar chart of per-test scores (0–100)
- **Summary panel** — device info, resolution, total time and overall score

## Sample Output

```
╔══════════════════════════════════════════════════════════════╗
║              fbmark - Linux Framebuffer Benchmark            ║
╠══════════════════════════════════════════════════════════════╣
║  Device  : /dev/fb0                                         ║
║  Res     : 1920x1080, 32 bpp                                ║
║  Region  : 1920x1080 at (0,0)                               ║
╚══════════════════════════════════════════════════════════════╝

  [ 1/13] Mandelbrot...
  [ 2/13] Rectangle fill...
  ...

╔══════════════════════════════════════════════════════════════════════╗
║                        RESULTS SCOREBOARD                           ║
╠════════════════╤══════════════╤═════════════════╤════════════════════╣
║ Test           │ Metric       │           Value │ Unit               ║
╟────────────────┼──────────────┼─────────────────┼────────────────────╢
║ Mandelbrot     │ time         │            2.45 │ s  (lower is better) ║
║ Rectangle fill │ MPixels/s   │          187.32 │ MPixels/s          ║
║ Sierpinski     │ FPS          │           45.20 │ FPS                ║
║ ...            │ ...          │             ... │ ...                ║
╠════════════════╧══════════════╧═════════════════╧════════════════════╣
║  Total:    8.32 s  │  Score:  72.4 / 100  (13 tests)               ║
╚══════════════════════════════════════════════════════════════════════╝
```

## The 13 Tests

| #  | Test             | Description                           | Metric         | Direction      |
|----|------------------|---------------------------------------|----------------|----------------|
|  1 | Mandelbrot       | Fractal rendering (raytracing)        | time (s)       | lower better   |
|  2 | Rectangle fill   | Random-colored rectangle fill         | MPixels/s      | higher better  |
|  3 | Sierpinski       | Recursive Sierpinski triangle         | max FPS        | higher better  |
|  4 | Gradient fill    | Horizontal color gradient             | MPixels/s      | higher better  |
|  5 | Blit copy        | Memory block copy (blit)              | MPixels/s      | higher better  |
|  6 | Line draw        | Random line drawing                   | lines/s        | higher better  |
|  7 | Circle draw      | Random circle drawing                 | circles/s      | higher better  |
|  8 | Fullscreen fill  | Solid color full-screen fill          | MPixels/s      | higher better  |
|  9 | Plasma effect    | Animated plasma sin/cos effect        | avg FPS        | higher better  |
| 10 | Scroll           | Vertical screen scroll                | MPixels/s      | higher better  |
| 11 | Text render      | Bitmap font text rendering            | chars/s        | higher better  |
| 12 | Triangle fill    | Random triangle fill                  | triangles/s    | higher better  |
| 13 | Julia set        | Fractal rendering (Julia set)         | time (s)       | lower better   |

### Scoring

Each test's raw value is normalized against a reference value from `score_meta`, clamped to [0, 100]. The total score is the **average** of all selected tests' normalized scores. Reference values:

| Test            | Ref Value | Direction      |
|-----------------|-----------|----------------|
| Mandelbrot      | 3.0       | lower better   |
| Rectangle fill  | 200.0     | higher better  |
| Sierpinski      | 60.0      | higher better  |
| Gradient fill   | 200.0     | higher better  |
| Blit copy       | 500.0     | higher better  |
| Line draw       | 100000.0  | higher better  |
| Circle draw     | 20000.0   | higher better  |
| Fullscreen fill | 1000.0    | higher better  |
| Plasma effect   | 30.0      | higher better  |
| Scroll          | 500.0     | higher better  |
| Text render     | 100000.0  | higher better  |
| Triangle fill   | 10000.0   | higher better  |
| Julia set       | 3.0       | lower better   |

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

## Contributing

Contributions are welcome! Feel free to open issues or submit pull requests.

## Authors

- Nicolas Caramelli
- Zheng Hua

## License

GPL v3+ — see source file headers for details.
