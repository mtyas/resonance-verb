#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Turns the CSVs written by `rv_render --export-plots docs/tools/data` into the SVG
figures used in the README and docs. Standard library only.

    python3 docs/tools/make_plots.py
"""
import csv, math, pathlib

HERE = pathlib.Path(__file__).resolve().parent
DATA, OUT = HERE / "data", HERE.parent / "images"
BG, PANEL, EDGE, GRID = "#15120f", "#201b16", "#3a3128", "#2c2620"
IVORY, DIM, BRASS, ICE, GLOW = "#efe6d4", "#9c8f7a", "#c8a15a", "#8fd3f0", "#ffb347"
FONT = "font-family='-apple-system,Segoe UI,Helvetica,Arial,sans-serif'"


def read(name):
    with open(DATA / name) as f:
        rows = list(csv.DictReader(f))
    return {k: [float(r[k]) for r in rows] for k in rows[0]}


def chart(path, title, subtitle, xs, series, xlabel, ylabel, ylim, xlog=False, xticks=(), yticks=(), notes=(), stop_at_floor=False, legend="tr"):
    W, H, L, R, T, B = 900, 420, 70, 24, 70, 56
    pw, ph = W - L - R, H - T - B
    x0, x1 = xs[0], xs[-1]
    fx = (lambda x: (math.log(x) - math.log(x0)) / (math.log(x1) - math.log(x0))) if xlog else (lambda x: (x - x0) / (x1 - x0))
    sx = lambda x: L + pw * fx(x)
    sy = lambda y: T + ph * (1 - (min(max(y, ylim[0]), ylim[1]) - ylim[0]) / (ylim[1] - ylim[0]))
    o = [f"<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 {W} {H}' width='{W}' height='{H}' {FONT}>",
         f"<rect width='{W}' height='{H}' rx='12' fill='{BG}'/>",
         f"<rect x='{L}' y='{T}' width='{pw}' height='{ph}' fill='{PANEL}' stroke='{EDGE}'/>",
         f"<text x='{L}' y='32' fill='{IVORY}' font-size='20' font-weight='700'>{title}</text>",
         f"<text x='{L}' y='54' fill='{DIM}' font-size='13'>{subtitle}</text>"]
    for v in yticks:
        y = sy(v)
        o += [f"<line x1='{L}' x2='{L+pw}' y1='{y:.1f}' y2='{y:.1f}' stroke='{GRID}'/>",
              f"<text x='{L-8}' y='{y+4:.1f}' fill='{DIM}' font-size='12' text-anchor='end'>{v:g}</text>"]
    for v, lab in xticks:
        x = sx(v)
        o += [f"<line x1='{x:.1f}' x2='{x:.1f}' y1='{T}' y2='{T+ph}' stroke='{GRID}'/>",
              f"<text x='{x:.1f}' y='{T+ph+18}' fill='{DIM}' font-size='12' text-anchor='middle'>{lab}</text>"]
    for x, lab in notes:
        o += [f"<line x1='{sx(x):.1f}' x2='{sx(x):.1f}' y1='{T}' y2='{T+ph}' stroke='{BRASS}' stroke-dasharray='3 4' opacity='0.5'/>",
              f"<text x='{sx(x)+4:.1f}' y='{T+14}' fill='{BRASS}' font-size='11'>{lab}</text>"]
    for i, (label, ys, colour, width, dash) in enumerate(series):
        pairs = []
        for x, y in zip(xs, ys):
            if stop_at_floor and y < ylim[0] + 1:   # the voice has been freed: end the curve
                break
            pairs.append(f"{sx(x):.1f},{sy(y):.1f}")
        pts = " ".join(pairs)
        o.append(f"<polyline points='{pts}' fill='none' stroke='{colour}' stroke-width='{width}' stroke-linejoin='round'"
                 + (f" stroke-dasharray='{dash}'" if dash else "") + "/>")
        if legend is None:
            continue
        lx = L + pw - 250 if legend in ("tr", "br") else L + 16
        ly = (T + 20 + 20 * i) if legend in ("tr", "tl") else (T + ph - 14 - 20 * (len(series) - 1 - i))
        o += [f"<rect x='{lx-8}' y='{ly-13}' width='238' height='20' fill='{PANEL}' opacity='0.85'/>",
              f"<line x1='{lx}' x2='{lx+26}' y1='{ly-4}' y2='{ly-4}' stroke='{colour}' stroke-width='{width}'" + (f" stroke-dasharray='{dash}'" if dash else "") + "/>",
              f"<text x='{lx+34}' y='{ly}' fill='{IVORY}' font-size='12.5'>{label}</text>"]
    o += [f"<text x='{L+pw/2}' y='{H-14}' fill='{DIM}' font-size='13' text-anchor='middle'>{xlabel}</text>",
          f"<text x='18' y='{T+ph/2}' fill='{DIM}' font-size='13' text-anchor='middle' transform='rotate(-90 18 {T+ph/2})'>{ylabel}</text>",
          "</svg>"]
    (OUT / path).write_text("\n".join(o))
    print("wrote", OUT / path)


def smooth(ys, n=3):
    return [sum(ys[max(0, i - n):i + n + 1]) / len(ys[max(0, i - n):i + n + 1]) for i in range(len(ys))]


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    s = read("spectrum.csv")
    peak = max(s["wet_db"])
    marks = []
    for name, f0, n in (("C3", 130.81, 8), ("E3", 164.81, 6), ("G3", 196.0, 5)):
        marks.append((f0, name))
    chart("spectrum.svg", "Only the open strings ring",
          "White noise in, C major triad (C3 E3 G3) held: every peak is a partial of one of those strings",
          s["hz"], [("String matrix output (Welch average)", [v - peak for v in s["wet_db"]], GLOW, 1.5, "")],
          "Frequency (Hz)", "Level (dB re. strongest partial)", (-60, 3), xlog=True,
          xticks=[(100, "100"), (200, "200"), (300, "300"), (500, "500"), (700, "700"), (1000, "1k")],
          yticks=range(-60, 1, 10), notes=marks, legend=None)

    d = read("double_decay.csv")
    chart("double_decay.svg", "Two-stage piano decay",
          "A4 trichord struck by an impulse: unisons split into a fast “prompt” and slow “aftersound” mode",
          d["t"], [("Double Decay 0 (single slope)", [v - max(d["off_db"]) for v in d["off_db"]], DIM, 1.6, "6 4"),
                   ("Double Decay 1 (Weinreich)", [v - max(d["on_db"]) for v in d["on_db"]], GLOW, 2.0, "")],
          "Time (s)", "Level (dB)", (-70, 3), xticks=[(x, f"{x:g}") for x in (0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5)],
          yticks=range(-70, 1, 10), legend="br")

    p = read("pedal.csv")
    ref = max(p["lifted_db"])
    chart("half_pedal.svg", "Half-pedalling",
          "C4 rings, key released at 40 ms: the pedal sets how hard the dampers grip",
          p["t"], [("Pedal up (dampers down)", [v - ref for v in p["down_db"]], DIM, 1.6, "6 4"),
                   ("Half pedal", [v - ref for v in p["half_db"]], BRASS, 2.0, ""),
                   ("Pedal down (dampers lifted)", [v - ref for v in p["lifted_db"]], GLOW, 2.0, "")],
          "Time (s)", "Level (dB)", (-90, 3), xticks=[(x, f"{x:g}") for x in (0, 0.5, 1, 1.5, 2, 2.5)],
          yticks=range(-90, 1, 15), stop_at_floor=True, legend="br")


if __name__ == "__main__":
    main()
