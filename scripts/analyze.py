#!/usr/bin/env python3
"""Analyse a FuhrerOS experiment directory (scripts/experiment.sh).

Reads results.jsonl and writes summary.md, result.json and SVG graphs. Only
measured values are reported; anything absent is shown as NOT RUN.
"""
import json
import os
import statistics
import sys

PALETTE = ["#5B6C8F", "#C9A66B", "#7FA37A", "#B5646B", "#6BA3A8", "#9C7FB5", "#8FA3BF"]
POLICIES = ["round_robin", "priority", "low_latency", "adaptive"]
LABEL = {"round_robin": "B0 round-robin", "priority": "B1 priority",
         "low_latency": "B2 low-latency", "adaptive": "B3 adaptive"}


def med(xs):
    return statistics.median(xs) if xs else None


def cv(xs):
    if len(xs) < 2:
        return 0.0
    m = statistics.mean(xs)
    return 100.0 * statistics.stdev(xs) / m if m else 0.0


def fmt(x, nd=1):
    if x is None:
        return "NOT RUN"
    if isinstance(x, float) and not x.is_integer():
        return f"{x:,.{nd}f}"
    return f"{int(x):,}"


def svg_bars(path, title, groups, series, values, ylabel, lower_better=False):
    W, H, L, B, T = 900, 420, 80, 90, 50
    gw = (W - L - 20) / max(1, len(groups))
    bw = gw * 0.8 / max(1, len(series))
    vals = [v for v in values.values() if v is not None]
    vmax = (max(vals) if vals else 1) * 1.15 or 1
    y = lambda v: H - B - (H - B - T) * v / vmax
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" font-family="sans-serif" font-size="12">',
         '<rect width="100%" height="100%" fill="#ffffff"/>',
         f'<text x="{W/2}" y="24" text-anchor="middle" font-size="15" font-weight="bold">{title}</text>',
         f'<text x="18" y="{(H-B+T)/2}" transform="rotate(-90 18 {(H-B+T)/2})" text-anchor="middle">{ylabel}{" (lower is better)" if lower_better else ""}</text>']
    for k in range(6):
        v = vmax * k / 5
        o.append(f'<line x1="{L}" x2="{W-20}" y1="{y(v):.1f}" y2="{y(v):.1f}" stroke="#e5e5e5"/>')
        o.append(f'<text x="{L-6}" y="{y(v)+4:.1f}" text-anchor="end" fill="#555">{v:,.0f}</text>')
    for gi, g in enumerate(groups):
        x0 = L + gi * gw + gw * 0.1
        for si, s in enumerate(series):
            v = values.get((g, s))
            if v is None:
                continue
            x = x0 + si * bw
            o.append(f'<rect x="{x:.1f}" y="{y(v):.1f}" width="{bw-2:.1f}" height="{H-B-y(v):.1f}" fill="{PALETTE[si % len(PALETTE)]}"><title>{g} / {s}: {v:,.1f}</title></rect>')
        o.append(f'<text x="{L + gi*gw + gw/2:.1f}" y="{H-B+16}" text-anchor="middle">{g}</text>')
    lx = L
    for si, s in enumerate(series):
        o.append(f'<rect x="{lx}" y="{H-40}" width="12" height="12" fill="{PALETTE[si % len(PALETTE)]}"/>')
        o.append(f'<text x="{lx+16}" y="{H-30}">{s}</text>')
        lx += 22 + 7.5 * len(s)
    o.append("</svg>")
    open(path, "w", encoding="utf-8").write("\n".join(o))


def svg_timeline(path, title, runs):
    """runs: [(label, [(t_ms, phase, class, ops)])]"""
    W, L, R = 1000, 90, 20
    H = 70 + 150 * len(runs)
    cls_color = {"CPU_BOUND": "#C9A66B", "IO_BOUND": "#5B6C8F", "INTERACTIVE": "#7FA37A",
                 "MIXED": "#9C7FB5", "IDLE": "#d9d9d9", "UNKNOWN": "#eeeeee"}
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" font-family="sans-serif" font-size="12">',
         '<rect width="100%" height="100%" fill="#ffffff"/>',
         f'<text x="{W/2}" y="22" text-anchor="middle" font-size="15" font-weight="bold">{title}</text>']
    for i, (label, rows) in enumerate(runs):
        top = 50 + i * 150
        if not rows:
            continue
        tmax = rows[-1][0] or 1
        x = lambda t: L + (W - L - R) * t / tmax
        o.append(f'<text x="10" y="{top+12}" font-weight="bold">{label}</text>')
        o.append(f'<text x="10" y="{top+48}" fill="#555">workload</text>')
        o.append(f'<text x="10" y="{top+78}" fill="#555">detected</text>')
        o.append(f'<text x="10" y="{top+118}" fill="#555">throughput</text>')
        prev_t, prev_phase = 0, None
        vmax = {}
        for t, ph, c, ops in rows:
            vmax[ph] = max(vmax.get(ph, 0), ops)
        pts = []
        for t, ph, c, ops in rows:
            o.append(f'<rect x="{x(prev_t):.1f}" y="{top+62}" width="{max(0.5, x(t)-x(prev_t)):.1f}" height="22" fill="{cls_color.get(c, "#eee")}"><title>{c}</title></rect>')
            if ph != prev_phase:
                o.append(f'<line x1="{x(prev_t):.1f}" x2="{x(prev_t):.1f}" y1="{top+30}" y2="{top+135}" stroke="#999" stroke-dasharray="3 3"/>')
                o.append(f'<text x="{x(prev_t)+4:.1f}" y="{top+48}">{ph}</text>')
                prev_phase = ph
            v = ops / vmax[ph] if vmax.get(ph) else 0
            pts.append(f"{x(t):.1f},{top + 135 - 35 * v:.1f}")
            prev_t = t
        o.append(f'<polyline points="{" ".join(pts)}" fill="none" stroke="#B5646B" stroke-width="1.5"/>')
    lx = L
    for name, c in cls_color.items():
        o.append(f'<rect x="{lx}" y="{H-24}" width="12" height="12" fill="{c}"/><text x="{lx+16}" y="{H-14}">{name}</text>')
        lx += 30 + 8 * len(name)
    o.append("</svg>")
    open(path, "w", encoding="utf-8").write("\n".join(o))


def main():
    exp = sys.argv[1]
    cfg = json.load(open(os.path.join(exp, "config.json"), encoding="utf-8"))
    recs = []
    p = os.path.join(exp, "results.jsonl")
    if os.path.exists(p):
        for line in open(p, encoding="utf-8", errors="replace"):
            try:
                recs.append(json.loads(line))
            except ValueError:
                pass
    md = [f"# {cfg['experiment']} — suite `{cfg['suite']}`", "",
          f"- Date {cfg['date']}, commit `{cfg['commit']}`, kernel {cfg['kernel']}",
          f"- Host {cfg['host']} / {cfg['host_cpu']} ({cfg['host_note']})",
          f"- {cfg['qemu']}, accel **{cfg['accel']}**, {cfg['vcpus']} vCPU offered "
          f"(FuhrerOS uses {cfg['vcpus_used_by_kernel']}), {cfg['memory_mb']} MB",
          f"- {cfg['disk']}; {cfg['network']}", ""]
    result = {"config": cfg}

    sb = [r["data"] for r in recs if r["kind"] == "SCHEDBENCH"]
    if sb:
        groups = {}
        for d in sb:
            scen = d["name"].rsplit("-r", 1)[0]
            groups.setdefault((scen, d["policy"]), []).append(d)
        result["schedbench"] = {f"{k[0]}|{k[1]}": v for k, v in groups.items()}
        scenarios = sorted({k[0] for k in groups})
        metrics = [("dispatch_p50_us", "dispatch p50 (us)"), ("dispatch_p99_us", "dispatch p99 (us)"),
                   ("wake_p50_us", "wake p50 (us)"), ("wake_p99_us", "wake p99 (us)"),
                   ("resp_p99_us", "response p99 (us)"), ("cpu_jobs_per_s_x100", "batch jobs/s (x100)"),
                   ("io_ops_per_s", "I/O ops/s"), ("jain_x1000", "fairness (Jain x1000)"),
                   ("ctx_switches_per_s", "ctx switches/s"), ("sched_overhead_ns_per_s", "sched overhead ns/s")]
        for scen in scenarios:
            pols = [p for p in POLICIES if (scen, p) in groups] + sorted(
                {k[1] for k in groups if k[0] == scen and k[1] not in POLICIES})
            md += [f"## Scheduler — scenario `{scen}` (median of {len(groups[(scen, pols[0])])} runs; CV%)", "",
                   "| metric | " + " | ".join(LABEL.get(p, p) for p in pols) + " |",
                   "|---" * (len(pols) + 1) + "|"]
            for key, label in metrics:
                cells = []
                for pol in pols:
                    xs = [d[key] for d in groups[(scen, pol)] if key in d]
                    cells.append(f"{fmt(med(xs), 0)} ({cv(xs):.0f}%)" if xs else "NOT RUN")
                md.append(f"| {label} | " + " | ".join(cells) + " |")
            classes = {pol: "/".join(sorted({d.get("system_class", "?") for d in groups[(scen, pol)]})) for pol in pols}
            md.append("| system class (mid-run) | " + " | ".join(classes[p] for p in pols) + " |")
            tc = {pol: sorted({d.get("task_classes", "") for d in groups[(scen, pol)]}) for pol in pols}
            if any(any(v) for v in tc.values()):
                md.append("")
                md.append("Per-worker classes assigned by the kernel at mid-run:")
                md.append("")
                for pol in pols:
                    md.append(f"- {LABEL.get(pol, pol)}: " + " | ".join(f"`{v}`" for v in tc[pol] if v))
            md.append("")
        if any(s.startswith("A") for s in scenarios):
            abl = [s for s in scenarios if s.startswith("A")]
            vals = {}
            for s in abl:
                pol = [k[1] for k in groups if k[0] == s][0]
                vals[(s, "wake p99 (us)")] = med([d["wake_p99_us"] for d in groups[(s, pol)]])
            svg_bars(os.path.join(exp, "ablation-wake-p99.svg"), "Ablations — interactive wake-up latency P99",
                     abl, ["wake p99 (us)"], vals, "microseconds", lower_better=True)
            md += ["![ablation](ablation-wake-p99.svg)", ""]
        else:
            for key, label, lb in [("wake_p99_us", "Interactive wake-up latency P99", True),
                                   ("cpu_jobs_per_s_x100", "Batch throughput (jobs/s x100)", False)]:
                vals = {}
                for scen in scenarios:
                    for pol in POLICIES:
                        if (scen, pol) in groups:
                            vals[(scen, LABEL[pol])] = med([d[key] for d in groups[(scen, pol)]])
                fn = f"sched-{key}.svg"
                svg_bars(os.path.join(exp, fn), label, scenarios, [LABEL[p] for p in POLICIES], vals,
                         "value", lower_better=lb)
                md += [f"![{label}]({fn})", ""]

    io = [r["data"] for r in recs if r["kind"] == "IOBENCH" and r["data"].get("seconds", 0) > 1]
    if io:
        g = {}
        for d in io:
            g.setdefault((d["workload"], d["policy"]), []).append(d)
        result["iobench"] = {f"{k[0]}|{k[1]}": v for k, v in g.items()}
        wls = [w for w in ["seq", "random", "hotset", "scan"] if any(k[0] == w for k in g)]
        pols = [p for p in ["lru", "fifo", "clock", "readahead", "adaptive"] if any(k[1] == p for k in g)]
        for key, label in [("mb_per_s_x100", "throughput MB/s x100"), ("hit_rate_pm", "hit rate (per mille)"),
                           ("lat_p99_us", "read latency p99 (us)"), ("cache_cpu_ns_per_read", "cache CPU per read (ns)")]:
            md += [f"## Buffer cache — {label} (median)", "", "| workload | " + " | ".join(pols) + " |",
                   "|---" * (len(pols) + 1) + "|"]
            vals = {}
            for w in wls:
                cells = []
                for pol in pols:
                    xs = [d[key] for d in g.get((w, pol), [])]
                    m = med(xs)
                    vals[(w, pol)] = m
                    cells.append(fmt(m, 0))
                md.append(f"| {w} | " + " | ".join(cells) + " |")
            md.append("")
            if key in ("mb_per_s_x100", "hit_rate_pm"):
                fn = f"cache-{key}.svg"
                svg_bars(os.path.join(exp, fn), f"Buffer cache: {label}", wls, pols, vals, label)
                md += [f"![{label}]({fn})", ""]

    tl = [r["data"] for r in recs if r["kind"] == "TL"]
    tr = [r["data"] for r in recs if r["kind"] == "TRANSITION"]
    if tl:
        runs, cur, last_t = [], [], -1
        for d in tl:
            if d["t_ms"] < last_t and cur:
                runs.append(cur)
                cur = []
            cur.append(d)
            last_t = d["t_ms"]
        if cur:
            runs.append(cur)
        labelled = []
        for i, run in enumerate(runs):
            lab = f"{run[0]['sched']} scheduler / {run[0]['cache']} cache"
            labelled.append((lab, [(d["t_ms"], d["phase"], d["class"], d["ops"]) for d in run]))
        svg_timeline(os.path.join(exp, "transition-timeline.svg"),
                     "Workload transitions: phase, detected class, throughput", labelled)
        md += ["## Workload transition", "", "| run | " + " | ".join(
            f"detect {p}" for p in ["cpu", "random-io", "sequential-io", "network", "interactive"]) + " |",
               "|---" * 6 + "|"]
        for d in tr:
            md.append(f"| {d['policy']} | " + " | ".join(
                ("not detected" if d[f"detect_ms_{p}"] < 0 else f"{d[f'detect_ms_{p}']} ms")
                for p in ["cpu", "random-io", "sequential-io", "network", "interactive"]) + " |")
        md += ["", "Detection = first 250 ms sample in the phase where the kernel's system-level class "
               "equals the phase's expected class (cpu→CPU_BOUND, I/O and network→IO_BOUND, "
               "interactive→INTERACTIVE).", "", "![timeline](transition-timeline.svg)", ""]
        result["transition"] = {"summary": tr, "timeline": tl}

    md += ["## Threats to validity", "",
           "- Nested virtualisation (Windows → WSL2 → KVM): absolute times are not bare-metal times; "
           "comparisons are between policies within one boot.",
           "- FuhrerOS schedules on one CPU (the BSP); extra vCPUs offered by QEMU are idle.",
           "- Sleepers are woken by the 1000 Hz tick, so *wake* latency includes up to 1 ms of timer "
           "granularity whose phase depends on the per-boot LAPIC calibration (F-117): compare wake latency "
           "only within one experiment. *Dispatch* latency (runnable → running) does not have this problem.",
           "- Small repetition counts; see CV%.", ""]
    open(os.path.join(exp, "summary.md"), "w", encoding="utf-8").write("\n".join(md))
    json.dump(result, open(os.path.join(exp, "result.json"), "w", encoding="utf-8"), indent=1)
    print("\n".join(md))


if __name__ == "__main__":
    main()
