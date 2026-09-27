#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# ///
"""ボードの通知の規則 (notify.h / notify.c の notify_gate) を UART ログの窓から PC で再生し、通知の件数を出す。

ログの win 行 (rms= と peak=) と、その次の -> 行 (1位のクラスと p) から窓ごとに
    p > 0.70  AND  クラスが通知対象  AND  peak >= 1036 (-30 dBFS)  AND  rms_db >= floor + 10 dB
をこの順に判定する (最初に引っかかった理由が below_thr / offlist / gated_abs / gated_rel。全部通れば PASS)。
floor は直近 60 窓の rms_db (20*log10(rms/32768)) の 10 パーセンタイル (昇順 6 番目) で、
履歴が 60 窓に満たない間は省く。floor は判定のあとに更新する (現在の窓は自分の floor に入らない)。
ボードと同じ整数の rms / peak から同じ式で計算するので、丸めの境界を除いて同じ結果になる
(p だけはログの小数 2 桁で、ボードの float との比較は >= にしている)。

    uv run scripts/notify_replay.py logs/uart_20260927_161828.log            # READY 以降の窓で再生 → 通知 4
    uv run scripts/notify_replay.py logs/uart_20260927_142118.log            # 1-Ex 再走行 → 19
    uv run scripts/notify_replay.py logs/uart_x.log --all                     # READY 前の窓も含める
    uv run scripts/notify_replay.py logs/uart_x.log -v                        # 候補の窓 (p > 閾値) を全部出す
    uv run scripts/notify_replay.py logs/uart_x.log --rel-db 8 --windows 30   # 規則の値を変えて試す

出すもの: 窓の数、通知 (PASS) の数とクラス別、止めた理由ごとの数 (gated_abs / gated_rel / below_thr /
offlist)、PASS の窓の一覧 (時刻・窓番号・クラス・p・rms_db・floor・Δ)、ボードが win 行に付けた印
("(gated abs)" / "(gated rel ...)" / "(floor ...)"。3.5 の "(gated)" は abs) との照合。
1 つのログに起動が 2 回以上あれば (READY が複数)、起動ごとに履歴を作り直して別に出す。
"""

from __future__ import annotations

import argparse
import math
import re
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

# notify.h の既定値 (引数で変えられる)
DEF_THR = 0.7
DEF_PEAK_DBFS = -30
DEF_WINDOWS = 60
DEF_PERCENTILE = 10
DEF_REL_DB = 10
DEF_CLASSES = "dog,crying_baby,sneezing"

RE_TS = re.compile(r"^(\d\d:\d\d:\d\d\.\d\d\d) ")
RE_WIN = re.compile(r"^win +(\d+) pos= *\d+ +(-?\d+)dBFS \[[#.]*\] rms= *(\d+) peak= *(\d+) ")
RE_ARROW = re.compile(r"^  -> (\S+) +p=([0-9.]+)(.*?)  preproc=")

PASS, GATED_ABS, GATED_REL, BELOW_THR, OFFLIST = "PASS", "gated_abs", "gated_rel", "below_thr", "offlist"


@dataclass
class Rule:
    thr: float = DEF_THR
    peak_dbfs: int = DEF_PEAK_DBFS
    windows: int = DEF_WINDOWS
    percentile: int = DEF_PERCENTILE
    rel_db: float = DEF_REL_DB
    classes: frozenset = frozenset(DEF_CLASSES.split(","))

    @property
    def gate_amp(self) -> int:
        """notify_init と同じ: (UW)(32768 * 10^(dBFS/20) + 0.5)。-99 以下で切 (0)"""
        if self.peak_dbfs <= -99:
            return 0
        return int(32768.0 * 10 ** (self.peak_dbfs / 20.0) + 0.5)

    @property
    def floor_idx(self) -> int:
        """notify.c の FLOOR_IDX: 昇順で 0 始まり。60 窓の 10 パーセンタイル → [5]"""
        k = self.windows * self.percentile // 100
        return k - 1 if k > 0 else 0

    def describe(self) -> str:
        return (f"p>{self.thr:.2f} AND peak>={self.gate_amp} ({self.peak_dbfs} dBFS) AND "
                f"rms_db >= floor+{self.rel_db:g} dB (floor = p{self.percentile} of last {self.windows} windows, "
                f"off until {self.windows}) AND cls in {','.join(sorted(self.classes))}")


def rms_db_of(rms: int) -> float:
    """notify_rms_db と同じ。0 なら -99"""
    return 20.0 * math.log10(rms / 32768.0) if rms > 0 else -99.0


def gate(rule: Rule, rms_db: float, peak: int, p: float, cls: str, floor: float | None) -> str:
    """notify_gate と同じ順 (BELOW_THR → OFFLIST → GATED_ABS → GATED_REL → PASS)。
    p はログの小数 2 桁なので >= で比べる (ボードは float を >)"""
    if cls == "unknown" or not (p >= rule.thr):
        return BELOW_THR
    if cls not in rule.classes:
        return OFFLIST
    if rule.gate_amp > 0 and peak < rule.gate_amp:
        return GATED_ABS
    if floor is not None and rms_db < floor + rule.rel_db:
        return GATED_REL
    return PASS


def board_verdict(note: str) -> str | None:
    """ボードが -> 行に付けた印 → 判定。無印は None (unknown / 対象外 / 3.5 以前の PASS)"""
    n = note.strip()
    if n.startswith("(gated rel"):
        return GATED_REL
    if n.startswith("(gated abs") or n == "(gated)":
        return GATED_ABS
    if n.startswith("(floor"):
        return PASS
    return None


@dataclass
class Window:
    t: str | None
    win: int
    dbfs: int
    rms: int
    peak: int
    cls: str
    p: float
    note: str


@dataclass
class Run:
    path: Path
    boot: int
    after_ready: bool = False
    windows: list[Window] = field(default_factory=list)   # 判定に使う窓 (--all でなければ READY 以降)
    n_before_ready: int = 0


def parse(path: Path, use_all: bool) -> list[Run]:
    runs: list[Run] = [Run(path, 1)]
    run = runs[0]
    cur: tuple | None = None
    for raw in path.open(encoding="utf-8", errors="replace"):
        line = raw.rstrip("\r\n")
        m = RE_TS.match(line)
        t = m.group(1) if m else None
        text = line[m.end():] if m else line
        if text.startswith("CONFIG:") and (run.windows or run.after_ready):
            run = Run(path, len(runs) + 1)        # 再起動
            runs.append(run)
        if text.startswith("READY"):
            if run.after_ready:                    # READY が 2 回 = 再起動 (CONFIG: 行が無いログ)
                run = Run(path, len(runs) + 1)
                runs.append(run)
            run.after_ready = True
            continue
        m = RE_WIN.match(text)
        if m:
            cur = (t, int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)))
            continue
        m = RE_ARROW.match(text)
        if m and cur is not None:
            w = Window(cur[0], cur[1], cur[2], cur[3], cur[4], m.group(1), float(m.group(2)), m.group(3))
            cur = None
            if run.after_ready or use_all:
                run.windows.append(w)
            else:
                run.n_before_ready += 1
    return [r for r in runs if r.windows]


def replay(run: Run, rule: Rule, verbose: bool) -> list[str]:
    hist: list[float] = []
    counts: Counter = Counter()
    by_cls: Counter = Counter()
    passed: list[str] = []
    cand: list[str] = []
    n_cmp = n_ok = n_ng = 0
    ng: list[str] = []
    # ボードの印の形式: 3.5-2 以降 (floor / gated abs / gated rel) は全窓を比べる。3.5 ((gated) だけ) は
    # 対象クラスの候補で abs の門だけ比べる (ボードの無印 = PASS か、当時無かった rel の門)。印が無ければ比べない
    new_marks = any(board_verdict(w.note) in (PASS, GATED_REL) or w.note.strip().startswith("(gated abs")
                    for w in run.windows)
    old_marks = not new_marks and any(w.note.strip() == "(gated)" for w in run.windows)
    for w in run.windows:
        rms_db = rms_db_of(w.rms)
        floor = sorted(hist)[rule.floor_idx] if len(hist) >= rule.windows else None
        v = gate(rule, rms_db, w.peak, w.p, w.cls, floor)
        hist.append(rms_db)
        if len(hist) > rule.windows:
            del hist[0]
        counts[v] += 1
        fl = f"floor {floor:.1f} Δ{rms_db - floor:+.1f}" if floor is not None else "floor n/a"
        row = f"{w.t or '-':12s} win {w.win:5d} {w.cls:15s} p={w.p:.2f} rms {rms_db:6.1f} dBFS peak {w.peak:5d}  {fl}  {v}"
        if v == PASS:
            by_cls[w.cls] += 1
            passed.append(row)
        if v != BELOW_THR:
            cand.append(row + (f"  board:{w.note.strip()}" if w.note.strip() else ""))
        b = board_verdict(w.note)
        if new_marks:
            if b is None:
                b = OFFLIST if (w.cls != "unknown" and w.p >= rule.thr and w.cls not in rule.classes) else BELOW_THR
            n_cmp += 1
            if b == v:
                n_ok += 1
            else:
                n_ng += 1
                ng.append(row + f"  board:{w.note.strip() or '(none)'}")
        elif old_marks and v in (PASS, GATED_ABS, GATED_REL):
            n_cmp += 1
            if (b == GATED_ABS) == (v == GATED_ABS):
                n_ok += 1
            else:
                n_ng += 1
                ng.append(row + f"  board:{w.note.strip() or '(none)'}")

    name = run.path.name + (f"（{run.boot} 回目の起動）" if run.boot > 1 else "")
    out = [f"notify_replay: {name}  {len(run.windows)} windows"
           + (f" (READY 前の {run.n_before_ready} 窓は除く)" if run.n_before_ready else "")]
    out.append(f"  rule: {rule.describe()}")
    out.append(f"  通知 (PASS): {counts[PASS]}"
               + (f"  内訳 " + " / ".join(f"{c} {n}" for c, n in by_cls.most_common()) if by_cls else ""))
    out.append(f"  gated_abs {counts[GATED_ABS]} / gated_rel {counts[GATED_REL]} / below_thr {counts[BELOW_THR]} / offlist {counts[OFFLIST]}")
    if n_cmp:
        out.append(f"  ボードの印との照合: 一致 {n_ok} / 不一致 {n_ng}"
                   + ("" if new_marks else " (印が (gated) だけの 3.5 のログなので、対象クラスの候補で abs の門だけ比べた)"))
        for r in ng[:20]:
            out.append("    NG " + r)
    else:
        out.append("  ボードの印との照合: 印が無い (門が入る前のログ)")
    out.append("  PASS の窓:")
    out += ["    " + r for r in passed] or ["    (なし)"]
    if verbose:
        out.append("  候補の窓 (閾値を超えたもの全部):")
        out += ["    " + r for r in cand]
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="ボードの通知の規則を UART ログから再生する")
    ap.add_argument("logs", nargs="+", type=Path)
    ap.add_argument("--all", action="store_true", help="READY 前の窓も判定と履歴に含める")
    ap.add_argument("-v", "--verbose", action="store_true", help="閾値を超えた窓を全部出す")
    ap.add_argument("--thr", type=float, default=DEF_THR, help=f"AED_OOD_THR (既定 {DEF_THR})")
    ap.add_argument("--peak-dbfs", type=int, default=DEF_PEAK_DBFS, help=f"NOTIFY_GATE_PEAK_DBFS (既定 {DEF_PEAK_DBFS}。-99 で切)")
    ap.add_argument("--windows", type=int, default=DEF_WINDOWS, help=f"NOTIFY_FLOOR_WINDOWS (既定 {DEF_WINDOWS})")
    ap.add_argument("--percentile", type=int, default=DEF_PERCENTILE, help=f"NOTIFY_FLOOR_PERCENTILE (既定 {DEF_PERCENTILE})")
    ap.add_argument("--rel-db", type=float, default=DEF_REL_DB, help=f"NOTIFY_GATE_REL_DB (既定 {DEF_REL_DB})")
    ap.add_argument("--classes", default=DEF_CLASSES, help=f"通知するクラス (既定 {DEF_CLASSES})")
    args = ap.parse_args()

    for stream in (sys.stdout, sys.stderr):
        try:
            if stream.isatty():
                stream.reconfigure(errors="replace")
            else:
                stream.reconfigure(encoding="utf-8", errors="replace", newline="\n")
        except Exception:
            pass

    rule = Rule(args.thr, args.peak_dbfs, args.windows, args.percentile, args.rel_db,
                frozenset(c for c in args.classes.split(",") if c))
    rc = 0
    for path in args.logs:
        if not path.is_file():
            print(f"[warn] 読めない: {path}", file=sys.stderr)
            rc = 1
            continue
        runs = parse(path, args.all)
        if not runs:
            print(f"notify_replay: {path.name}  窓が無い (READY 以降に win 行と -> 行が無い。--all で READY 前も見る)")
            continue
        for run in runs:
            print("\n".join(replay(run, rule, args.verbose)))
            print()
    return rc


if __name__ == "__main__":
    sys.exit(main())
