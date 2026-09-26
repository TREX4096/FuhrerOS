#!/usr/bin/env python3
"""Analyse a FuhrerOS experiment directory (produced by scripts/benchmark.sh).

Reads raw/fuhrer-exp/<config>/<workload>/rep<N>.json (+ .timeline.csv) and
writes result.json, summary.md and SVG graphs. Only measured values are
reported; missing combinations are shown as NOT RUN.

Usage: analyze.py EXPERIMENT_DIR
"""
import csv
import glob
import json
import math
import os
import statistics
import sys

# workload -> (label, extractor, unit, higher_is_better)
METRICS = {
    "randread": ("IOPS", lambda j: j["iops"], "IOPS", True),
    "randwrite": ("IOPS", lambda j: j["iops"], "IOPS", True),
    "seqread": ("MB/s", lambda j: j["mbps"], "MB/s", True),
    "seqwrite": ("MB/s", lambda j: j["mbps"], "MB/s", True),
    "cpu": ("Mops/s", lambda j: j["mops_per_s"]["p50"], "Mops/s", True),
    "memory": ("triad GB/s", lambda j: j["triad_gbps"]["p50"], "GB/s", True),
    "netlat": ("RTT p50", lambda j: j["rtt_us"]["p50"], "us", False),
    "netbw": ("MB/s", lambda j: j["mbps"], "MB/s", True),
}
# Tail latency (Graph 2)
TAIL = {
    "randread": lambda j: j["latency_us"]["p99"],
    "randwrite": lambda j: j["latency_us"]["p99"],
    "seqread": lambda j: j["latency_us"]["p99"],
    "seqwrite": lambda j: j["latency_us"]["p99"],
    "netlat": lambda j: j["rtt_us"]["p99"],
}
MAIN_CONFIGS = ["B0", "normal", "batched", "specialized", "adaptive"]
A6_CONFIGS = ["knobs-batched", "knobs-specialized", "lib-batched", "lib-specialized"]
LABEL = {"B0": "B0 Linux", "normal": "B1 static normal", "batched": "static batched",
         "specialized": "static specialized", "adaptive": "B2 adaptive",
         "knobs-batched": "knobs-only batched", "knobs-specialized": "knobs-only specialized",
         "lib-batched": "lib-only batched", "lib-specialized": "lib-only specialized"}
POLICY_NAMES = {0: "NORMAL", 1: "BATCHED", 2: "SPECIALIZED", -1: "none"}
# Policy the default map should reach in each transition phase.
# E-002 used the original map (cpu->BATCHED, seqread->BATCHED); later runs use
# the revised map (D-006 rev. 2). The config's "policy_map" field selects.
EXPECTED_V1 = {"idle": 0, "cpu": 1, "randread": 2, "seqread": 1}
EXPECTED_V2 = {"idle": 0, "cpu": 0, "randread": 2, "seqread": 2}
EXPECTED = EXPECTED_V1
PALETTE = ["#5B6C8F", "#8FA3BF", "#C9A66B", "#7FA37A", "#B5646B", "#6BA3A8", "#9C7FB5"]


def load(path):
    try:
        with open(path) as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def summarize(values):
    if not values:
        return None
    med = statistics.median(values)
    sd = statistics.stdev(values) if len(values) > 1 else 0.0
    return {"n": len(values), "median": med, "min": min(values), "max": max(values),
            "stdev": sd, "cv_pct": (100.0 * sd / med) if med else 0.0, "raw": values}


def collect_main(raw):
    res = {}
    for cfg in MAIN_CONFIGS + A6_CONFIGS:
        for wl in METRICS:
            vals, tails = [], []
            for p in sorted(glob.glob(os.path.join(raw, cfg, wl, "rep*.json"))):
                j = load(p)
                if not j:
                    continue
                try:
                    vals.append(float(METRICS[wl][1](j)))
                    if wl in TAIL:
                        tails.append(float(TAIL[wl](j)))
                except (KeyError, TypeError):
                    pass
            if vals:
                res.setdefault(wl, {})[cfg] = {"primary": summarize(vals),
                                               "p99": summarize(tails)}
    return res


def fmt(x):
    if x is None:
        return "NOT RUN"
    if abs(x) >= 1000:
        return f"{x:,.0f}"
    if abs(x) >= 10:
        return f"{x:.1f}"
    return f"{x:.3f}"


# ---------------------------------------------------------------- SVG helpers
def svg_bars(path, title, groups, series, values, ylabel, ref=None):
    """Grouped bar chart. values[(group, series)] = number or None."""
    W, H, L, B, T = 900, 420, 70, 90, 50
    gw = (W - L - 20) / max(1, len(groups))
    bw = gw * 0.8 / max(1, len(series))
    vmax = max([v for v in values.values() if v is not None] + [1e-9])
    if ref:
        vmax = max(vmax, ref)
    vmax *= 1.12
    y = lambda v: H - B - (H - B - T) * v / vmax
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
           f'font-family="sans-serif" font-size="12">',
           '<rect width="100%" height="100%" fill="#ffffff"/>',
           f'<text x="{W/2}" y="24" text-anchor="middle" font-size="15" font-weight="bold">{title}</text>',
           f'<text x="16" y="{(H-B+T)/2}" transform="rotate(-90 16 {(H-B+T)/2})" text-anchor="middle">{ylabel}</text>']
    for k in range(6):
        v = vmax * k / 5
        out.append(f'<line x1="{L}" x2="{W-20}" y1="{y(v):.1f}" y2="{y(v):.1f}" stroke="#e3e3e3"/>')
        out.append(f'<text x="{L-6}" y="{y(v)+4:.1f}" text-anchor="end" fill="#555">{fmt(v)}</text>')
    if ref:
        out.append(f'<line x1="{L}" x2="{W-20}" y1="{y(ref):.1f}" y2="{y(ref):.1f}" stroke="#333" stroke-dasharray="4 3"/>')
    for gi, g in enumerate(groups):
        x0 = L + gi * gw + gw * 0.1
        for si, s in enumerate(series):
            v = values.get((g, s))
            if v is None:
                continue
            x = x0 + si * bw
            out.append(f'<rect x="{x:.1f}" y="{y(v):.1f}" width="{bw-2:.1f}" height="{H-B-y(v):.1f}" '
                       f'fill="{PALETTE[si % len(PALETTE)]}"><title>{g} {s}: {fmt(v)}</title></rect>')
        out.append(f'<text x="{L + gi*gw + gw/2:.1f}" y="{H-B+16}" text-anchor="middle">{g}</text>')
    lx = L
    for si, s in enumerate(series):
        out.append(f'<rect x="{lx}" y="{H-40}" width="12" height="12" fill="{PALETTE[si % len(PALETTE)]}"/>')
        out.append(f'<text x="{lx+16}" y="{H-30}">{s}</text>')
        lx += 18 + 7.5 * len(s)
    out.append("</svg>")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out))


def svg_timeline(path, title, runs):
    """runs: list of (label, rows) where rows = [(t, phase, mb_or_ops, policy)]."""
    W, H, L, R, T = 900, 120 + 110 * len(runs), 70, 20, 40
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" '
           f'font-family="sans-serif" font-size="12">',
           '<rect width="100%" height="100%" fill="#ffffff"/>',
           f'<text x="{W/2}" y="22" text-anchor="middle" font-size="15" font-weight="bold">{title}</text>']
    pol_color = {0: "#d9d9d9", 1: "#C9A66B", 2: "#5B6C8F", -1: "#ffffff"}
    tmax = max([r[0] for _, rows in runs for r in rows] + [1])
    for i, (label, rows) in enumerate(runs):
        top = T + 10 + i * 110
        h = 70
        x = lambda t: L + (W - L - R) * t / tmax
        out.append(f'<text x="{L}" y="{top-4}" font-weight="bold">{label}</text>')
        # policy band
        for a, b in zip(rows, rows[1:] + [None]):
            t0 = a[0] - 1
            t1 = a[0]
            out.append(f'<rect x="{x(t0):.1f}" y="{top+h+4}" width="{max(0.5, x(t1)-x(t0)):.1f}" height="10" '
                       f'fill="{pol_color.get(a[3], "#fff")}"/>')
        # throughput (normalised per phase kind so all phases are visible)
        vmax = {}
        for r in rows:
            vmax[r[1]] = max(vmax.get(r[1], 0), r[2])
        pts = []
        for r in rows:
            v = r[2] / vmax[r[1]] if vmax.get(r[1]) else 0
            pts.append(f"{x(r[0]):.1f},{top + h - h * v:.1f}")
        out.append(f'<polyline points="{" ".join(pts)}" fill="none" stroke="#B5646B" stroke-width="1.6"/>')
        out.append(f'<line x1="{L}" x2="{W-R}" y1="{top+h}" y2="{top+h}" stroke="#999"/>')
        last = None
        for r in rows:
            if r[1] != last:
                out.append(f'<line x1="{x(r[0]-1):.1f}" x2="{x(r[0]-1):.1f}" y1="{top}" y2="{top+h+14}" stroke="#bbb" stroke-dasharray="3 3"/>')
                out.append(f'<text x="{x(r[0]-1)+3:.1f}" y="{top+10}" fill="#555">{r[1]}</text>')
                last = r[1]
    ly = H - 30
    for k, (name, c) in enumerate([("NORMAL", pol_color[0]), ("BATCHED", pol_color[1]),
                                   ("SPECIALIZED", pol_color[2])]):
        out.append(f'<rect x="{L + k*140}" y="{ly}" width="12" height="12" fill="{c}"/>')
        out.append(f'<text x="{L + k*140 + 16}" y="{ly+10}">{name}</text>')
    out.append(f'<text x="{L + 3*140}" y="{ly+10}" fill="#B5646B">— throughput (normalised per phase)</text>')
    out.append("</svg>")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(out))


# ---------------------------------------------------------------- transition
def read_timeline(path):
    rows = []
    try:
        with open(path) as f:
            for r in csv.DictReader(f):
                val = float(r["mb"]) if r["phase"] in ("randread", "seqread") and float(r["mb"]) > 0 \
                    else float(r["ops"])
                rows.append((float(r["t"]), r["phase"], val, int(r["daemon_policy"])))
    except (OSError, ValueError, KeyError):
        pass
    return rows


def expected_from_conf(path):
    """Expected policy per transition phase, from the fuhrer.conf actually used."""
    exp = dict(EXPECTED)
    ids = {"normal": 0, "batched": 1, "specialized": 2}
    phase = {"IO_RANDOM": "randread", "IO_SEQUENTIAL": "seqread", "CPU_BOUND": "cpu", "IDLE": "idle"}
    try:
        for line in open(path, encoding="utf-8"):
            k, _, v = line.partition("=")
            k, v = k.strip(), v.strip().lower()
            if k.startswith("map.") and k[4:] in phase and v in ids:
                exp[phase[k[4:]]] = ids[v]
    except OSError:
        pass
    return exp


def adaptation_stats(rows, expected=None):
    """Seconds from each phase start until the daemon reached the expected policy."""
    expected = expected or EXPECTED
    delays, phase_start, cur, reached = {}, None, None, False
    switches = sum(1 for a, b in zip(rows, rows[1:]) if a[3] != b[3])
    for t, ph, _, pol in rows:
        if ph != cur:
            cur, phase_start, reached = ph, t - 1, False
        key = ph if ph not in delays else ph + "#2"
        if not reached and pol == expected.get(ph, -9):
            delays[key] = t - phase_start
            reached = True
    per_phase = {}
    for t, ph, v, _ in rows:
        per_phase.setdefault(ph, []).append(v)
    return delays, switches, {k: statistics.mean(v) for k, v in per_phase.items()}


def main():
    exp = sys.argv[1]
    raw = os.path.join(exp, "raw", "fuhrer-exp")
    cfg = load(os.path.join(exp, "config.json")) or {}
    global EXPECTED
    EXPECTED = EXPECTED_V2 if cfg.get("policy_map") == "v2" else EXPECTED_V1
    result = {"experiment": cfg.get("experiment"), "config": cfg}
    md = [f"# {cfg.get('experiment', 'Experiment')} — suite `{cfg.get('suite')}`", "",
          f"- Date: {cfg.get('date')}  commit `{cfg.get('commit')}`",
          f"- Host: {cfg.get('host')} / {cfg.get('host_cpu')} ({cfg.get('host_note', '')})",
          f"- QEMU: {cfg.get('qemu')}  accel **{cfg.get('accel')}**",
          f"- VM `{cfg.get('vm_config')}`: {cfg.get('vcpus')} vCPU, {cfg.get('memory_mb')} MB, "
          f"{cfg.get('disk_gb')} GB {cfg.get('disk')}",
          f"- Kernel: {(cfg.get('image') or {}).get('kernel')}; reps={cfg.get('reps')}, "
          f"time={cfg.get('time_s')}s, warmup={cfg.get('warmup_s')}s", ""]

    # ---- main suite
    main_res = collect_main(raw)
    if main_res:
        result["main"] = main_res
        md += ["## Static vs adaptive (median of reps; CV% in parentheses)", "",
               "| workload | metric | " + " | ".join(LABEL[c] for c in MAIN_CONFIGS) +
               " | B3 best static | adaptive vs B1 | adaptive vs B3 |",
               "|---" * (len(MAIN_CONFIGS) + 5) + "|"]
        g1, g2 = {}, {}
        for wl, (label, _, unit, hib) in METRICS.items():
            if wl not in main_res:
                continue
            row = main_res[wl]
            cells = []
            for c in MAIN_CONFIGS:
                s = row.get(c, {}).get("primary")
                cells.append(f"{fmt(s['median'])} ({s['cv_pct']:.0f}%)" if s else "NOT RUN")
            statics = {c: row[c]["primary"]["median"] for c in ("normal", "batched", "specialized")
                       if c in row}
            b3c = (max if hib else min)(statics, key=statics.get) if statics else None
            b3 = statics.get(b3c)
            ad = row.get("adaptive", {}).get("primary")
            b1 = row.get("normal", {}).get("primary")

            def rel(a, b):
                if not a or b is None or not b:
                    return "NOT RUN"
                r = a["median"] / b if hib else b / a["median"]
                return f"{(r - 1) * 100:+.1f}%"
            md.append(f"| {wl} | {label} ({unit}) | " + " | ".join(cells) +
                      f" | {fmt(b3)} ({b3c}) | {rel(ad, b1['median'] if b1 else None)} | {rel(ad, b3)} |")
            row["B3"] = {"config": b3c, "median": b3}
            base = row.get("B0", {}).get("primary")
            for c in MAIN_CONFIGS:
                s = row.get(c, {}).get("primary")
                if s and base and base["median"]:
                    r = s["median"] / base["median"] if hib else base["median"] / s["median"]
                    g1[(wl, LABEL[c])] = r
                t = row.get(c, {}).get("p99")
                if t:
                    g2[(wl, LABEL[c])] = t["median"]
        md += ["", "Relative columns: positive = adaptive better. Latency workloads compare inversely.", ""]
        if any(c in main_res[w] for w in main_res for c in A6_CONFIGS):
            md += ["### A6 — where does the gain come from? (median; relative to B1 static normal)", "",
                   "| workload | knobs-only batched | knobs-only specialized | lib-only batched | "
                   "lib-only specialized | full batched | full specialized |", "|---" * 7 + "|"]
            for wl, (label, _, unit, hib) in METRICS.items():
                row = main_res.get(wl)
                if not row or "normal" not in row:
                    continue
                b1 = row["normal"]["primary"]["median"]
                cells = []
                for c in A6_CONFIGS + ["batched", "specialized"]:
                    s_ = row.get(c, {}).get("primary")
                    if not s_ or not b1:
                        cells.append("NOT RUN")
                        continue
                    r = s_["median"] / b1 if hib else b1 / s_["median"]
                    cells.append(f"{fmt(s_['median'])} ({(r - 1) * 100:+.0f}%)")
                md.append(f"| {wl} | " + " | ".join(cells) + " |")
            md += [""]
        wls = [w for w in METRICS if w in main_res]
        series = [LABEL[c] for c in MAIN_CONFIGS]
        svg_bars(os.path.join(exp, "graph1-throughput.svg"),
                 "Graph 1 — performance normalised to B0 (native Linux), higher is better",
                 wls, series, g1, "relative to B0", ref=1.0)
        tw = [w for w in wls if w in TAIL]
        if tw:
            svg_bars(os.path.join(exp, "graph2-p99.svg"),
                     "Graph 2 — P99 latency (us), lower is better", tw, series, g2, "microseconds")
        md += ["![Graph 1](graph1-throughput.svg)", "", "![Graph 2](graph2-p99.svg)", ""]

    # ---- transition suite (Graph 3)
    runs = []
    trans = {}
    for c in ["normal", "batched", "specialized", "adaptive"]:
        for p in sorted(glob.glob(os.path.join(raw, c, "transition", "rep*.timeline.csv"))):
            rows = read_timeline(p)
            if not rows:
                continue
            d, sw, per = adaptation_stats(rows)
            trans.setdefault(c, []).append({"delays_s": d, "switches": sw, "phase_mean": per})
            if p.endswith("rep1.timeline.csv"):
                runs.append((LABEL.get(c, c), rows))
    if trans:
        result["transition"] = trans
        md += ["## Workload transitions (Graph 3)", "",
               "| config | rep | switches | time-to-policy randread (s) | seqread (s) | cpu (s) | "
               "randread MB/s | seqread MB/s | cpu ops/s |", "|---" * 9 + "|"]
        for c, reps in trans.items():
            for i, r in enumerate(reps, 1):
                d, pm = r["delays_s"], r["phase_mean"]
                md.append(f"| {LABEL.get(c, c)} | {i} | {r['switches']} | {fmt(d.get('randread'))} | "
                          f"{fmt(d.get('seqread'))} | {fmt(d.get('cpu'))} | {fmt(pm.get('randread'))} | "
                          f"{fmt(pm.get('seqread'))} | {fmt(pm.get('cpu'))} |")
        svg_timeline(os.path.join(exp, "graph3-adaptation.svg"),
                     "Graph 3 — throughput and active policy across workload phases", runs)
        md += ["", "Time-to-policy counts from the phase start until fuhrerd's published policy equals "
               "the policy the default map assigns to that phase; NOT RUN = never reached "
               "(expected for static configs).", "", "![Graph 3](graph3-adaptation.svg)", ""]

    # ---- ablation suite (Graph 4 + A4/A5/A7)
    over = {}
    for d in sorted(glob.glob(os.path.join(raw, "overhead-*"))):
        v = os.path.basename(d)[len("overhead-"):]
        vals, dover = [], []
        for p in sorted(glob.glob(os.path.join(d, "cpu", "rep*.json"))):
            j = load(p)
            if j:
                vals.append(j["mops_per_s"]["p50"])
            st = p.replace(".json", ".daemon-status")
            try:
                for line in open(st):
                    if line.startswith("overhead_pct="):
                        dover.append(float(line.split("=")[1]))
            except OSError:
                pass
        over[v] = {"cpu_mops": summarize(vals), "daemon_overhead_pct": summarize(dover)}
    if over:
        result["overhead"] = over
        base = (over.get("off", {}).get("cpu_mops") or {}).get("median")
        md += ["## Monitoring overhead (Graph 4; A1 off vs A2 observe at several intervals)", "",
               "| variant | cpu Mops/s median | vs off | fuhrerd self CPU % (of all vCPUs) |",
               "|---|---|---|---|"]
        g4 = {}
        for v, r in over.items():
            m = (r["cpu_mops"] or {}).get("median")
            o = (r["daemon_overhead_pct"] or {}).get("median")
            md.append(f"| {v} | {fmt(m)} | "
                      f"{'%+.2f%%' % ((m / base - 1) * 100) if m and base else 'n/a'} | {fmt(o)} |")
            if m:
                g4[("cpu", v)] = m
        svg_bars(os.path.join(exp, "graph4-overhead.svg"),
                 "Graph 4 — CPU benchmark under profiler configurations (Mops/s)",
                 ["cpu"], list(over), g4, "Mops/s", ref=base)
        md += ["", "![Graph 4](graph4-overhead.svg)", ""]
    abl = {}
    for d in sorted(glob.glob(os.path.join(raw, "abl-*"))):
        v = os.path.basename(d)[4:]
        # The map in force is read from the config file the run actually used.
        expv = expected_from_conf(os.path.join(exp, "raw", f"ablation-{v}.conf"))
        for p in sorted(glob.glob(os.path.join(d, "transition", "rep*.timeline.csv"))):
            rows = read_timeline(p)
            if rows:
                dl, sw, pm = adaptation_stats(rows, expv)
                abl.setdefault(v, []).append({"delays_s": dl, "switches": sw, "phase_mean": pm})
    if abl:
        result["ablation"] = abl
        md += ["## Adaptation ablations (A4 default, A5 sampling interval, A7 hysteresis)", "",
               "| variant | reps | switches (median) | time-to-policy randread (s) | "
               "time-to-policy seqread (s) | randread MB/s | seqread MB/s |", "|---" * 7 + "|"]
        for v, reps in abl.items():
            med = lambda xs: statistics.median(xs) if xs else None
            md.append(f"| {v} | {len(reps)} | {fmt(med([r['switches'] for r in reps]))} | "
                      f"{fmt(med([r['delays_s']['randread'] for r in reps if 'randread' in r['delays_s']]))} | "
                      f"{fmt(med([r['delays_s']['seqread'] for r in reps if 'seqread' in r['delays_s']]))} | "
                      f"{fmt(med([r['phase_mean'].get('randread', 0) for r in reps]))} | "
                      f"{fmt(med([r['phase_mean'].get('seqread', 0) for r in reps]))} |")
        md.append("")

    md += ["## Threats to validity (this run)", "",
           "- Nested virtualisation (Windows Hyper-V → WSL2 → KVM): absolute numbers are not "
           "representative of bare metal; only relative comparisons inside one run are meaningful.",
           "- Disk is a qcow2 overlay on a WSL2 virtual disk; host page cache bypassed (cache=none) "
           "but the WSL2 vhdx layer is not.",
           "- Network workloads use guest loopback; they exercise the guest stack, not virtio-net.",
           f"- Small sample size (reps={cfg.get('reps')}); see CV% for run-to-run variance.", ""]
    with open(os.path.join(exp, "result.json"), "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)
    with open(os.path.join(exp, "summary.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(md))
    print("\n".join(md))


if __name__ == "__main__":
    main()
