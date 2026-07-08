# fbmark

[English](README.md) | [中文](README_zh.md)

**v1.0.1** — Linux Framebuffer 基准测试套件 — 包含 13 个图形基准测试，通过 `mmap` 直接渲染到 `/dev/fb0`，测量吞吐量、帧率或耗时。

## 功能特性

- 13 项基准测试，覆盖常见的 2D 图形操作
- 通过 `mmap` 直接操作 framebuffer — 无需 X11/Wayland/GPU
- 适用于任何支持 framebuffer 的 Linux 系统（`/dev/fb0`）
- 支持文件模拟 framebuffer，可在无头/CI 环境下运行
- 紧凑的双文件实现（`fbmark.c` + `fb_util.h`）— 易于移植和理解
- 结果归一化，逐项评分并输出总分
- 支持 JSON 和 CSV 输出，便于可视化和自动化（`-o` 参数）
- 设备自动检测（型号、厂商、CPU 信息），通过 `/sys` 和 `/proc` 获取
- `visualize.py` 脚本，用于从基准测试结果生成图表
- 除 Makefile 外，还支持 CMake 构建

## 环境要求

- 支持 framebuffer 的 Linux 系统（`/dev/fb0`）
- GCC 或 Clang
- 标准 C 库（`libc`、`libm`）

## 构建

### Make

```bash
make                          # 构建 fbmark.out
make clean                    # 删除 fbmark.out
make install PREFIX=/usr      # 安装到 DESTDIR（默认 /usr/local）
```

自定义编译器和编译选项：

```bash
make CC=clang CFLAGS="-O3 -march=native" LDFLAGS="-static"
```

### CMake

```bash
mkdir build && cd build
cmake ..
make                          # 构建 fbmark.out
make install                  # 安装到 prefix（默认 /usr/local）
```

## 使用方法

```
fbmark [OPTIONS]
```

### 命令行参数

| 参数                | 说明                                             |
|---------------------|--------------------------------------------------|
| `-h`, `--help`      | 显示帮助信息并退出                                |
| `-v`, `--version`   | 显示版本信息并退出                                |
| `-l`, `--list`      | 列出所有可用测试并退出                            |
| `-t`, `--test LIST` | 仅运行指定测试（逗号分隔的名称或编号，如 `"mandelbrot,line"` 或 `"1,3,5"`） |
| `-o`, `--output FILE` | 将结果写入 FILE（JSON 或 CSV，根据扩展名自动检测格式） |
| `-f`, `--format FMT`  | 输出格式：`json` 或 `csv`（默认：从 `-o` 文件扩展名检测，回退为 json） |
| `-m`, `--model NAME`  | 输出中的设备型号名称（默认：从 `/sys/class/dmi/id/` 或 `/proc/device-tree/` 自动检测） |

### 环境变量

| 变量                | 默认值          | 说明                                   |
|---------------------|----------------|----------------------------------------|
| `FRAMEBUFFER`       | `/dev/fb0`     | Framebuffer 设备路径                   |
| `WIDTH`             | 屏幕宽度       | 基准测试区域宽度                       |
| `HEIGHT`            | 屏幕高度       | 基准测试区域高度                       |
| `POSX`              | 0              | 基准测试区域 X 偏移                    |
| `POSY`              | 0              | 基准测试区域 Y 偏移                    |
| `SIERPINSKI_FPS`    | 4              | Sierpinski 测试的最小 FPS              |

### 示例

```bash
# 运行全部测试
./fbmark.out

# 仅运行指定测试
./fbmark.out -t mandelbrot,rectangle,fill

# 按编号运行测试
./fbmark.out -t 1,2,8

# 自定义 framebuffer 和区域
FRAMEBUFFER=/dev/fb1 WIDTH=800 HEIGHT=600 ./fbmark.out

# 导出结果为 JSON
./fbmark.out -o results.json

# 导出结果为 CSV
./fbmark.out -o results.csv

# 强制指定格式，忽略文件扩展名
./fbmark.out -o results.txt -f json

# 设置设备型号名称
./fbmark.out -o results.json -m "Raspberry Pi 4"
```

### 无真实 Framebuffer 运行

在没有真实 framebuffer 的系统上（如 CI），可以使用文件模拟 framebuffer：

```bash
dd if=/dev/zero of=/tmp/fb bs=1K count=8192
FRAMEBUFFER=/tmp/fb ./fbmark.out
```

## 可视化

使用 `visualize.py` 从基准测试结果生成图表（需要 `matplotlib`）：

```bash
# 安装依赖
pip install matplotlib

# 运行基准测试并导出结果
./fbmark.out -o results.json

# 生成图表（支持 .json 和 .csv 输入）
python3 visualize.py results.json
```

将生成 `results.png`，包含以下内容：
- **原始数值图** — 各测试原始值的柱状图（对数刻度，按测试分组）
- **归一化得分图** — 各测试得分的柱状图（0—100）
- **汇总面板** — 设备信息、分辨率、总耗时和综合得分

## 输出示例

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
║ 测试           │ 指标         │           数值  │ 单位               ║
╟────────────────┼──────────────┼─────────────────┼────────────────────╢
║ Mandelbrot     │ time         │            2.45 │ s  (越低越好)      ║
║ 矩形填充       │ MPixels/s   │          187.32 │ MPixels/s          ║
║ Sierpinski     │ FPS          │           45.20 │ FPS                ║
║ ...            │ ...          │             ... │ ...                ║
╠════════════════╧══════════════╧═════════════════╧════════════════════╣
║  Total:    8.32 s  │  Score:   125.3  (13 tests)                    ║
╚══════════════════════════════════════════════════════════════════════╝
```

## 13 项测试

| #  | 测试名称         | 描述                                | 指标           | 优劣方向       |
|----|------------------|-------------------------------------|----------------|----------------|
|  1 | Mandelbrot       | 分形渲染（光线追踪）                 | 耗时 (s)       | 越低越好       |
|  2 | 矩形填充         | 随机颜色矩形填充                     | MPixels/s      | 越高越好       |
|  3 | Sierpinski       | 递归 Sierpinski 三角形               | 最大 FPS       | 越高越好       |
|  4 | 渐变填充         | 水平颜色渐变                         | MPixels/s      | 越高越好       |
|  5 | Blit 拷贝        | 内存块拷贝（Blit）                   | MPixels/s      | 越高越好       |
|  6 | 直线绘制         | 随机直线绘制                         | lines/s        | 越高越好       |
|  7 | 圆形绘制         | 随机圆形绘制                         | circles/s      | 越高越好       |
|  8 | 全屏填充         | 纯色全屏填充                         | MPixels/s      | 越高越好       |
|  9 | Plasma 效果      | 动态 Plasma 正弦/余弦效果            | 平均 FPS       | 越高越好       |
| 10 | 滚动             | 垂直屏幕滚动                         | MPixels/s      | 越高越好       |
| 11 | 文本渲染         | 位图字体文本渲染                     | chars/s        | 越高越好       |
| 12 | 三角形填充       | 随机三角形填充                       | triangles/s    | 越高越好       |
| 13 | Julia 集         | 分形渲染（Julia 集）                 | 耗时 (s)       | 越低越好       |

### 评分规则

每项测试的原始值相对于 `score_meta` 中的参考值进行归一化。总分为所有选中测试归一化分数的**算术平均值**。类似 CoreMark，设备性能越强，得分越高，没有上限。

参考值如下：

| 测试名称       | 参考值      | 优劣方向       |
|----------------|-------------|----------------|
| Mandelbrot     | 3.0         | 越低越好       |
| 矩形填充       | 200.0       | 越高越好       |
| Sierpinski     | 60.0        | 越高越好       |
| 渐变填充       | 200.0       | 越高越好       |
| Blit 拷贝      | 500.0       | 越高越好       |
| 直线绘制       | 100000.0    | 越高越好       |
| 圆形绘制       | 20000.0     | 越高越好       |
| 全屏填充       | 1000.0      | 越高越好       |
| Plasma 效果    | 30.0        | 越高越好       |
| 滚动           | 500.0       | 越高越好       |
| 文本渲染       | 100000.0    | 越高越好       |
| 三角形填充     | 10000.0     | 越高越好       |
| Julia 集       | 3.0         | 越低越好       |

## 架构

两个文件：

1. **共享工具头文件**（`fb_util.h`）— 控制台初始化与清理。打开 `/dev/tty0`，切换到 `KD_GRAPHICS` 模式，隐藏文本光标，安装信号处理函数。以 `static` 函数形式直接包含，无需单独编译。

2. **基准测试程序**（`fbmark.c`）：将 13 项测试作为 `static` 函数包含在内，由 `main()` 依次调用，最后输出格式化的计分板并计算总分。

程序生命周期：

1. 以 `O_RDWR` 方式打开 `$FRAMEBUFFER`（默认 `/dev/fb0`）
2. 调用 `ioctl(FBIOGET_VSCREENINFO)` 获取屏幕尺寸和像素位深
3. `mmap(NULL, len, PROT_WRITE, MAP_SHARED, fd, 0)` 映射 framebuffer
4. 调用 `fb_console_init()` 切换到图形模式
5. 执行渲染循环，使用 `gettimeofday` 计时
6. 调用 `fb_console_restore()`、`munmap`、`close(fd)` 清理

## 贡献

欢迎贡献！欢迎提交 Issue 或 Pull Request。

## 作者

- Nicolas Caramelli
- Zheng Hua

## 许可证

GPL v3+ — 详见源文件头部。
