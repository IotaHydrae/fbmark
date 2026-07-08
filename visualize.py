#!/usr/bin/env python3
"""
fbmark可视化工具 —— 从 fbmark JSON 或 CSV 输出生成图表。

用法：
    ./fbmark.out -o results.json
    python3 visualize.py results.json

    ./fbmark.out -o results.csv
    python3 visualize.py results.csv

输出：
    results.png — 一张包含三项内容的组合图：
      1. 柱状图：各测试的原始值（按测试分组）
      2. 柱状图：各测试的归一化得分（0—100）
      3. 汇总：带有元数据信息的文字面板
"""

import csv
import json
import sys
import os
from pathlib import Path

# 检查是否已安装 matplotlib
try:
    import matplotlib
    matplotlib.use("Agg")  # 非交互式后端，适用于无头环境
    import matplotlib.pyplot as plt
    import matplotlib.ticker as mticker
except ImportError:
    print("错误：此脚本需要 matplotlib。请执行以下命令安装：")
    print("  pip install matplotlib")
    sys.exit(1)


def _detect_format(path: str) -> str:
    """根据扩展名检测文件格式。"""
    ext = os.path.splitext(path)[1].lower()
    if ext == ".csv":
        return "csv"
    return "json"


def _load_json(path: str) -> dict:
    """从 JSON 文件加载 fbmark 结果。"""
    with open(path) as f:
        return json.load(f)


def _load_csv(path: str) -> dict:
    """从 CSV 文件加载 fbmark 结果，并规范化为与 JSON 一致的结构。"""
    tests = []
    meta = {}

    with open(path, newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            tests.append({
                "name":       row["test_name"],
                "short_name": row["short_name"],
                "value":      float(row["value"]),
                "unit":       row["unit"],
                "metric":     row["metric"],
                "direction":  row["direction"],
                "score":      float(row["score"]),
                "ref_value":  float(row["ref_value"]),
            })

            # 元数据在每一行中重复，只取第一行即可
            if not meta:
                meta = {
                    "version":        "unknown",
                    "device_model":   row["device_model"],
                    "device_vendor":  row["device_vendor"],
                    "cpu_model":      row["cpu_model"],
                    "framebuffer":    row["framebuffer"],
                    "resolution": {
                        "width":  int(row["width"]),
                        "height": int(row["height"]),
                        "bpp":    int(row["bpp"]),
                    },
                    "region": {
                        "width": int(row["region_w"]),
                        "height": int(row["region_h"]),
                        "posx":  0,
                        "posy":  0,
                    },
                    "total_time_s": float(row["total_time_s"]),
                    "total_score":  float(row["total_score"]),
                }

    meta["tests"] = tests
    return meta


def load_results(path: str) -> dict:
    """加载 fbmark 结果文件（JSON 或 CSV，根据扩展名自动检测）。"""
    fmt = _detect_format(path)
    if fmt == "csv":
        return _load_csv(path)
    return _load_json(path)


def make_charts(data: dict, out_path: str):
    """从 fbmark 结果数据生成组合图表。"""

    tests = data["tests"]
    meta = {
        "version": data.get("version", "unknown"),
        "device_model": data.get("device_model", "Unknown"),
        "device_vendor": data.get("device_vendor", "Unknown"),
        "cpu_model": data.get("cpu_model", "Unknown"),
        "framebuffer": data.get("framebuffer", data.get("device", "Unknown")),
        "res": f"{data['resolution']['width']}x{data['resolution']['height']}",
        "bpp": data["resolution"]["bpp"],
        "region": (f"{data['region']['width']}x{data['region']['height']}"
                   f"+{data['region']['posx']}+{data['region']['posy']}"),
        "total_s": data["total_time_s"],
        "score": data["total_score"],
    }

    # ---- 布局：2 行，上排 2 张图，下排 1 个汇总面板 ----
    fig = plt.figure(figsize=(16, 10))
    fig.suptitle("fbmark — Linux Framebuffer Benchmark",
                 fontsize=16, fontweight="bold", y=0.97)

    gs = fig.add_gridspec(2, 2, height_ratios=[3, 1],
                          hspace=0.35, wspace=0.35,
                          left=0.06, right=0.98, top=0.92, bottom=0.06)

    ax_raw = fig.add_subplot(gs[0, 0])   # 原始数值
    ax_score = fig.add_subplot(gs[0, 1])  # 得分
    ax_meta = fig.add_subplot(gs[1, :])   # 汇总面板

    # ---- 配色 ----
    names = [t["short_name"] for t in tests]
    x = range(len(tests))

    # 通过方向进行着色：higher_better → 蓝色，lower_better → 橙红色
    raw_colors = [
        "#2196F3" if t["direction"] == "higher_better" else "#FF7043"
        for t in tests
    ]
    score_colors = raw_colors  # 与得分使用相同颜色方案

    # =================================================================
    # 图表 A：原始数值（分面柱状图，因为单位差异很大）
    # =================================================================
    raw_vals = [t["value"] for t in tests]
    bars = ax_raw.bar(x, raw_vals, color=raw_colors, edgecolor="white", linewidth=0.5)

    # 在每个柱子上方标注单位
    for i, t in enumerate(tests):
        unit_short = t["unit"].split()[0]  # 首个单词作为短单位
        ax_raw.text(i, raw_vals[i] * 1.01,
                    unit_short, ha="center", va="bottom",
                    fontsize=6.5, color="#555555")

    ax_raw.set_title("原始基准测试数值", fontsize=13, fontweight="bold")
    ax_raw.set_xticks(x)
    ax_raw.set_xticklabels(names, rotation=45, ha="right", fontsize=8)
    ax_raw.set_ylabel("数值（各测试单位不同）", fontsize=9)
    ax_raw.grid(axis="y", alpha=0.3, linestyle="--")
    ax_raw.set_axisbelow(True)

    # 对数刻度，方便比较跨数量级的数值
    if min(raw_vals) > 0:
        ax_raw.set_yscale("log")
        ax_raw.set_ylabel("数值（对数刻度，各测试单位不同）", fontsize=9)

    # 图例
    from matplotlib.patches import Patch
    legend_el = [
        Patch(facecolor="#2196F3", label="值越大越好"),
        Patch(facecolor="#FF7043", label="值越小越好"),
    ]
    ax_raw.legend(handles=legend_el, fontsize=8, loc="upper right")

    # =================================================================
    # 图表 B：归一化得分
    # =================================================================
    scores = [t["score"] for t in tests]
    bars2 = ax_score.bar(x, scores, color=score_colors,
                         edgecolor="white", linewidth=0.5)

    # 在每个柱子上方标注得分
    for i, s in enumerate(scores):
        ax_score.text(i, s + 0.8, f"{s:.0f}",
                      ha="center", va="bottom", fontsize=7.5, fontweight="bold")

    ax_score.set_title("归一化得分（0—100）", fontsize=13, fontweight="bold")
    ax_score.set_xticks(x)
    ax_score.set_xticklabels(names, rotation=45, ha="right", fontsize=8)
    ax_score.set_ylabel("得分", fontsize=9)
    ax_score.set_ylim(0, 110)
    ax_score.axhline(y=100, color="#888888", linewidth=0.8,
                     linestyle="--", alpha=0.6)
    ax_score.grid(axis="y", alpha=0.3, linestyle="--")
    ax_score.set_axisbelow(True)

    # =================================================================
    # 面板 C：汇总面板
    # =================================================================
    ax_meta.axis("off")

    lines = [
        f"设备型号：   {meta['device_model']}",
        f"设备厂商：   {meta['device_vendor']}",
        f"CPU：        {meta['cpu_model']}",
        f"帧缓冲：     {meta['framebuffer']}",
        f"分辨率：     {meta['res']} @ {meta['bpp']} bpp",
        f"基准区域：   {meta['region']}",
        f"fbmark 版本：{meta['version']}",
        "",
        f"总耗时：     {meta['total_s']:.2f} s",
        f"综合得分：   {meta['score']:.1f} / 100  （共 {len(tests)} 项测试）",
    ]

    y_pos = 0.95
    for line in lines:
        if line:
            ax_meta.text(0.05, y_pos, line, transform=ax_meta.transAxes,
                         fontsize=11, fontfamily="monospace",
                         verticalalignment="top")
        y_pos -= 0.10

    # ---- 保存 ----
    fig.savefig(out_path, dpi=150, facecolor="white")
    print(f"图表已保存至 {out_path}")


def main():
    if len(sys.argv) < 2:
        # 回退到默认文件名
        for default in ("results.json", "results.csv"):
            if Path(default).exists():
                json_path = default
                break
        else:
            print("用法：python3 visualize.py <results.json|results.csv>")
            print("")
            print("先运行 fbmark 并导出结果：")
            print("  ./fbmark.out -o results.json")
            print("  ./fbmark.out -o results.csv")
            print("  python3 visualize.py results.json")
            sys.exit(1)
    else:
        json_path = sys.argv[1]

    if not Path(json_path).exists():
        print(f"错误：文件不存在：{json_path}")
        sys.exit(1)

    if os.path.splitext(json_path)[1].lower() not in (".json", ".csv"):
        print(f"警告：无法识别的文件扩展名 '{json_path}'，"
              f"将尝试按 JSON 解析")

    data = load_results(json_path)

    # 输出文件名：将扩展名替换为 .png
    stem = os.path.splitext(json_path)[0]
    out_path = f"{stem}.png"

    make_charts(data, out_path)


if __name__ == "__main__":
    main()
