#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# ///
"""3-1 対照実験 (A 本番 / B 優先度逆転 / D 推論を遅くする) の UART ログを集計して表にする。

logs/ の uart_*.log を読み、各ログの先頭付近の CONFIG: 行で条件 (A / B / D) を判定する。
FINAL ブロック (tap final 〜 disp の各行)、READY 以降の毎秒の in=…Hz 行、READY 以降の JSON 行の
lat_ms、READY 以降の lcd 行 (描画までの時間) を集計し、docs/3-1_results.md に表を書き出す
(同じ表を標準出力にも出す)。表の数字はすべてログから取る。手で書き換えない。

    uv run scripts/analyze_31.py                          # PINNED のログ (条件ごとに 1 本に固定) で表を作る
    uv run scripts/analyze_31.py --scan                   # logs/ を走査し、条件ごとに FINAL のある最新のログ
    uv run scripts/analyze_31.py logs/uart_x.log ...      # 使うログを指定 (同じ条件が複数なら最新)
    uv run scripts/analyze_31.py --out -                  # 表を標準出力だけに (ファイルを書かない)

ログを固定するのは、ボードを走らせるたびに表の数字が黙って入れ替わらないようにするため
(2026-09-27 に D を 2 回走らせたとき、「最新」の規則では表が 2 回目に切り替わった)。
別の実験 (FSD50K 版など) では PINNED を差し替えるか、引数で渡す。
使わなかったログ (同じ条件の別の走行、FINAL の無い途中で止めたもの、FINAL の行が欠けたもの) は
表の下に理由付きで書き、完走したものは要点の数字も添える (再現性の参考)。

行の形 (mtk3bsp2_stm32n657/Appli/Application の log_printf の書式そのまま。scripts/log.ps1 -Timestamp の
先頭 HH:mm:ss.fff は取り除いて読む):
    CONFIG: D slow-infer (INFER_PRIO_INVERT=0 INFER_SLOW_X=10 task_infer pri 15)     usermain.c
    READY  起動確認おわり。ここから本番                                            infer_task.c show_ready_banner
    in=16128Hz out=16400Hz ring=864 under=0 over=0 (dt=1000000us) [logdrop=N] loglag=67000us   trace_task.c report_rate
    {"win":2,"cls":"dog","p":0.52,"lat_ms":112,"under":0,"over":0,"late":0}         notify.c notify_window
    lcd: win 19 -> FIRE p=0.93 (542712 us after notify)                             lcd_task.c
    FINAL の本文                                                                    infer_task.c show_summary
1 つのファイルにボードの再起動が 2 回以上入っていれば (CONFIG: 行が複数)、起動ごとに別の走行として扱う。

走行集計 (3.5 誤報対策の前後比較): 読んだ走行ごとに、通知の行数 (FINAL の notify out) とクラス別の JSON 行数、
gated (3.5-2 からは gated_abs / gated_rel) / held / offlist、READY〜FINAL の時間と 1 時間あたりの件数を標準出力に出す
(表の後。docs にも同じものを書く。
引数でログを渡したときはその全部、PINNED / --scan のときは表に使った走行だけ)。
10 分のログでも 1 時間のログでも同じ形式。CONFIG: 行の無いログ (起動後に記録を始めたもの) も、
READY と FINAL があれば集計する (3-1 の表には使わない)。

    uv run scripts/analyze_31.py logs/uart_20260927_130250.log --out -   # 対策前 (A、静かな部屋 10 分) の走行集計だけ見る
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_LOGS = ROOT / "logs"
DEFAULT_OUT = ROOT / "docs" / "3-1_results.md"

# 表に使うログ (条件 → ファイル名)。2026-09-27 の各 10 分の走行
PINNED = {
    "A": "uart_20260927_005956.log",
    "B": "uart_20260927_011552.log",
    "D": "uart_20260927_013950.log",
}

RE_TS = re.compile(r"^(\d\d):(\d\d):(\d\d)\.(\d\d\d) ")
RE_CONFIG = re.compile(r"^CONFIG: ([A-Z]) (\S+) \((.*)\)\s*$")     # 中の key=value は別に読む
RE_KV = re.compile(r"(\w+)=(-?\d+)")
RE_RULE = re.compile(r"^=+$")
RE_READY = re.compile(r"^READY\b")
RE_RATE = re.compile(
    r"^in=(\d+)Hz out=(\d+)Hz ring=(\d+) under=(\d+) over=(\d+) \(dt=(\d+)us\)"
    r"(?: logdrop=(\d+))? loglag=(\d+)us"
)
RE_JSON = re.compile(r'^\{"win":\d+,"cls":"[^"]*","p":[0-9.]+,"lat_ms":\d+,"under":\d+,"over":\d+,"late":\d+\}$')
RE_LCD = re.compile(r"^lcd: win (\d+) -> (\S+) p=([0-9.]+) \((\d+) us after notify\)")

# FINAL の本文。名前 → (正規表現, 取り出す項目名の並び)。slow は条件 D のビルドだけ
FINAL_LINES = {
    "tap": (
        re.compile(
            r"tap final: windows=(\d+) \(expected (\d+) from (\d+) samples\) "
            r"overrun=(\d+) torn=(\d+) skipped=(\d+) seam ok=(\d+) NG=(\d+)"
        ),
        ("windows", "expected", "samples", "overrun", "torn", "skipped", "seam_ok", "seam_ng"),
    ),
    "taplag": (
        re.compile(
            r"tap lag max=(\d+)us \((\d+) of them lower bounds\) \| "
            r"tap write max=(\d+)ns avg=(\d+)ns \((\d+) calls\)"
        ),
        ("lag_max_us", "lag_loose", "wr_max_ns", "wr_avg_ns", "wr_calls"),
    ),
    "audio": (
        re.compile(r"audio under=(\d+) over=(\d+) late=(\d+) \| aed run=(\d+) err=(\d+) \(last er=(-?\d+)\)"),
        ("under", "over", "late", "run", "err", "last_er"),
    ),
    "time": (
        re.compile(r"preproc max=(\d+)us avg=(\d+)us \| infer max=(\d+)us avg=(\d+)us"),
        ("pp_max", "pp_avg", "inf_max", "inf_avg"),
    ),
    "slow": (
        re.compile(
            r"slow x(\d+): window max=(\d+)us avg=(\d+)us \| "
            r"spin n=(\d+) max=(\d+)us avg=(\d+)us skipped=(\d+)"
        ),
        ("x", "win_max", "win_avg", "spin_n", "spin_max", "spin_avg", "spin_skipped"),
    ),
    "notify": (
        # 3.5 までは gated=、3.5-2 からは gated_abs= gated_rel= (無い方は None。gated は後で abs+rel にする)
        re.compile(
            r"notify out=(\d+) held=(\d+) offlist=(\d+) (?:gated=(\d+)|gated_abs=(\d+) gated_rel=(\d+))"
            r"(?: cooldown=(\d+))? lat max=(\d+)us \((\d+) lower bounds\)"
        ),
        ("out", "held", "offlist", "gated", "gated_abs", "gated_rel", "cooldown", "lat_max_us", "lat_loose"),
    ),
    "log": (
        re.compile(r"log sent=(\d+) dropped=(\d+) lag max=(\d+)us"),
        ("sent", "dropped", "lag_max_us"),
    ),
    "disp": (
        re.compile(r"disp sent=(\d+) dropped=(\d+) max=(\d+)us avg=(\d+)us"),
        ("sent", "dropped", "max_us", "avg_us"),
    ),
}
FINAL_REQUIRED = ("tap", "taplag", "audio", "time", "notify", "log", "disp")

COND_LABEL = {"A": "A 本番", "B": "B 優先度逆転", "D": "D 推論を遅くする"}


@dataclass
class Run:
    """ログ 1 本の中の、ボードの起動 1 回ぶん"""
    path: Path
    boot: int = 1               # そのファイルの中で何回目の起動か
    n_boots: int = 1
    cond: str = ""              # "A" / "B" / "D"
    cond_name: str = ""         # "production" / "prio-invert" / "slow-infer"
    switches: dict[str, int] = field(default_factory=dict)   # CONFIG 行の key=value
    t_config: float | None = None   # PC の時計 (秒)。-Timestamp 無しのログでは None
    t_ready: float | None = None
    t_final: float | None = None
    ready_seen: bool = False
    final_seen: bool = False
    final: dict[str, dict[str, int]] = field(default_factory=dict)
    final_text: list[str] = field(default_factory=list)
    rates: list[tuple[int, int, int, int, int | None]] = field(default_factory=list)  # in, out, under, over, logdrop (READY 以降)
    lat_ms: list[int] = field(default_factory=list)      # READY 以降の JSON の lat_ms
    cls_lines: Counter = field(default_factory=Counter)  # JSON 行のクラス別の数 (走行全体。FINAL の notify out と同じ範囲)
    lcd_us: list[int] = field(default_factory=list)      # READY 以降の lcd 行の「通知から描画まで」
    n_lost_lines: int = 0       # "tap: window lost" が出力できた回数 (D では大半が捨てられる)
    n_stopped: int = 0          # PASSTHROUGH STOPPED の見出しの数

    @property
    def name(self) -> str:
        return self.path.name + (f"（{self.boot} 回目の起動）" if self.n_boots > 1 else "")

    @property
    def missing(self) -> list[str]:
        return [k for k in FINAL_REQUIRED if k not in self.final]

    @property
    def complete(self) -> bool:
        return self.final_seen and not self.missing

    @property
    def why_unusable(self) -> str:
        if not self.final_seen:
            return "FINAL が無い（途中で止めたログ）"
        return "FINAL ブロックが不完全（欠けた行: " + ", ".join(self.missing) + "）"

    @property
    def has_config(self) -> bool:
        return self.cond != ""

    def config_line(self) -> str:
        if not self.has_config:
            return "(CONFIG: 行なし)"
        kv = " ".join(f"{k}={v}" for k, v in self.switches.items())
        return f"{self.cond} {self.cond_name} ({kv})"


def ts_seconds(m: re.Match) -> float:
    h, mi, s, ms = (int(x) for x in m.groups())
    return h * 3600 + mi * 60 + s + ms / 1000.0


def parse_log(path: Path) -> list[Run]:
    """CONFIG: 行ごとに 1 つの Run。CONFIG: 行より前に本文があれば (記録を途中から始めたログ)、
    そこも cond="" の Run にする (走行集計にだけ使い、3-1 の表には使わない)"""
    runs: list[Run] = []
    run: Run | None = None
    in_final = False
    final_body_seen = False
    after_ready = False

    with path.open(encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            m_ts = RE_TS.match(line)
            t = ts_seconds(m_ts) if m_ts else None
            text = line[m_ts.end():] if m_ts else line

            if text.startswith("CONFIG:"):
                m = RE_CONFIG.match(text)
                if not m:
                    print(f"[warn] {path.name}: CONFIG: 行の形が違う: {text}", file=sys.stderr)
                    continue
                if runs and not runs[-1].has_config and not runs[-1].ready_seen and not runs[-1].final_seen:
                    runs.pop()      # CONFIG: より前の [FAULT] / [TRACE] の行だけ。走行ではない
                run = Run(path=path, boot=len(runs) + 1)
                runs.append(run)
                run.cond, run.cond_name = m.group(1), m.group(2)
                run.switches = {k: int(v) for k, v in RE_KV.findall(m.group(3))}
                run.t_config = t
                in_final = False
                after_ready = False
                continue
            if run is None:
                if not text.strip():
                    continue
                run = Run(path=path, boot=len(runs) + 1)      # CONFIG: 行の無い走行
                runs.append(run)
                in_final = False
                after_ready = False

            if in_final:
                if RE_RULE.match(text):
                    if final_body_seen:
                        in_final = False        # 本文の後の区切り = ブロックの終わり
                    continue
                if RE_RATE.match(text):
                    # 1 秒の行は reporter が直接出すので、FINAL の本文の間に挟まり得る。本文には入れない
                    if after_ready:
                        g = RE_RATE.match(text).groups()
                        run.rates.append((int(g[0]), int(g[1]), int(g[3]), int(g[4]), int(g[6]) if g[6] else None))
                    continue
                final_body_seen = True
                run.final_text.append(text)
                for key, (rx, names) in FINAL_LINES.items():
                    m = rx.search(text)
                    if m:
                        d = dict(zip(names, (int(v) if v is not None else None for v in m.groups())))
                        if key == "notify" and d["gated"] is None:
                            d["gated"] = d["gated_abs"] + d["gated_rel"]
                        run.final[key] = d
                        break
                continue

            if text == "FINAL":
                in_final = True
                final_body_seen = False
                run.final_seen = True
                run.t_final = t
                continue
            if text == "PASSTHROUGH STOPPED":
                run.n_stopped += 1
                continue
            if RE_READY.match(text):
                after_ready = True
                run.ready_seen = True
                run.t_ready = t
                continue
            if text.startswith("tap: window lost"):
                run.n_lost_lines += 1
                continue
            if RE_JSON.match(text):
                j = json.loads(text)
                run.cls_lines[j["cls"]] += 1          # READY 前の行も数える (FINAL の out と同じ範囲)
                if after_ready:
                    run.lat_ms.append(int(j["lat_ms"]))
                continue
            if not after_ready:
                continue

            m = RE_RATE.match(text)
            if m:
                g = m.groups()
                run.rates.append((int(g[0]), int(g[1]), int(g[3]), int(g[4]), int(g[6]) if g[6] else None))
                continue
            m = RE_LCD.match(text)
            if m:
                run.lcd_us.append(int(m.group(4)))

    for r in runs:
        r.n_boots = len(runs)
    return runs


def load_runs(paths: list[Path]) -> list[Run]:
    runs: list[Run] = []
    for p in sorted(paths):
        if not p.is_file():
            print(f"[warn] 読めない (ファイルでない): {p}", file=sys.stderr)
            continue
        runs.extend(parse_log(p))
    return runs


def select_newest(runs: list[Run]) -> tuple[dict[str, Run], list[tuple[Run, str]]]:
    """条件ごとに完走した最新 (ファイル名順、同じファイルなら後の起動) のもの"""
    chosen: dict[str, Run] = {}
    unused: list[tuple[Run, str]] = []
    for run in runs:                      # sorted(paths) の順 = ファイル名順、起動順
        if not run.has_config:
            continue
        if not run.complete:
            unused.append((run, run.why_unusable))
            continue
        prev = chosen.get(run.cond)
        if prev is not None:
            unused.append((prev, f"同じ条件 {run.cond} のより新しい走行がある"))
        chosen[run.cond] = run
    return chosen, unused


def select_pinned(runs: list[Run]) -> tuple[dict[str, Run], list[tuple[Run, str]], list[str]]:
    """PINNED のファイルを使う。戻り値の 3 つ目は見つからなかった条件のメッセージ"""
    chosen: dict[str, Run] = {}
    unused: list[tuple[Run, str]] = []
    errors: list[str] = []
    for cond, fname in PINNED.items():
        cands = [r for r in runs if r.path.name == fname and r.complete]
        if not cands:
            errors.append(f"条件 {cond} に固定したログ {fname} が無いか完走していない")
            continue
        chosen[cond] = cands[-1]
        if cands[-1].cond != cond:
            errors.append(f"{fname} の CONFIG は {cands[-1].cond} で、固定した条件 {cond} と違う")
    for run in runs:
        if not run.has_config or any(run is c for c in chosen.values()):
            continue
        unused.append((run, run.why_unusable if not run.complete else "表に使うログを固定している（PINNED）"))
    return chosen, unused, errors


# ---------------------------------------------------------------- 表

def fmt_dur(a: float | None, b: float | None) -> str:
    if a is None or b is None:
        return "-"
    d = b - a
    if d < 0:
        d += 24 * 3600     # 日付をまたいだ
    return f"{int(d // 60)}:{int(d % 60):02d}"


def dur_s(a: float | None, b: float | None) -> float | None:
    if a is None or b is None:
        return None
    d = b - a
    if d < 0:
        d += 24 * 3600     # 日付をまたいだ
    return d


def per_hour(n: int, secs: float | None) -> str:
    return f"{n * 3600.0 / secs:.0f} 件/時" if secs else "- 件/時"


def run_summary(run: Run) -> list[str]:
    """走行 1 本の通知の集計 (3.5)。通知の総数はボードのカウンタ (FINAL の notify out)、
    クラス別は JSON 行を数えたもの。両者の差はレポータが捨てた行 (READY 前の CSV ダンプ中。D では満杯のキュー)"""
    nt = run.final.get("notify")
    secs = dur_s(run.t_ready, run.t_final)
    n_lines = sum(run.cls_lines.values())
    n_unknown = run.cls_lines.get("unknown", 0)
    n_detect = n_lines - n_unknown
    out = [f"走行集計: {run.name}  CONFIG: {run.config_line()}"]
    if not run.final_seen:
        out.append("  FINAL が無い (途中で止めたログ)。JSON 行だけ数える")
    out.append(f"  走行時間 READY→FINAL: {fmt_dur(run.t_ready, run.t_final)}"
               + (f" ({secs:.1f} s)" if secs is not None else " (時刻なし。log.ps1 -Timestamp のログでないと件/時は出ない)"))
    if nt:
        n_out = nt["out"]
        out.append(f"  通知 (JSON 行、FINAL の notify out): {n_out} 件 = {per_hour(n_out, secs)}")
        diff = n_out - n_lines
        note = f"（ログ上の JSON 行は {n_lines}。差 {diff} は reporter が捨てた行 = READY 前のダンプ中、D では満杯のキュー）" if diff else ""
    else:
        out.append(f"  通知 (ログ上の JSON 行): {n_lines} 件 = {per_hour(n_lines, secs)}")
        note = ""
    by_cls = " / ".join(f"{c} {n}" for c, n in sorted(run.cls_lines.items(), key=lambda kv: (-kv[1], kv[0])))
    out.append(f"    クラス別: {by_cls or '-'}{note}")
    out.append(f"    検出 (unknown を除く): {n_detect} 件 = {per_hour(n_detect, secs)} / unknown: {n_unknown} 件")
    if nt:
        if nt.get("gated_abs") is None:
            gated = f"gated {nt['gated']}"
            what = "gated=音量の門で止めた窓"
        else:
            gated = f"gated_abs {nt['gated_abs']} / gated_rel {nt['gated_rel']}"
            what = "gated_abs=ピークの門で止めた窓、gated_rel=暗騒音からの差の門で止めた窓"
            if nt.get("cooldown") is not None:
                gated += f" / cooldown {nt['cooldown']}"
                what += "、cooldown=同じクラスの再通知を抑えた窓"
        out.append(f"  {gated} / held {nt['held']} / offlist {nt['offlist']}  (FINAL。{what}、"
                   f"held=unknown が続いて出さなかった窓、offlist=通知対象外のクラスの窓)")
    return out


def med(xs: list[int]) -> str:
    if not xs:
        return "-"
    m = statistics.median(xs)
    return f"{int(m)}" if float(m).is_integer() else f"{m:.1f}"


def g(f: dict[str, dict[str, int]], key: str, name: str) -> str:
    d = f.get(key)
    return str(d[name]) if d and d.get(name) is not None else "-"


def gated_text(f: dict[str, dict[str, int]]) -> str:
    """gated の数。3.5-2 以降のログは abs / rel の内訳付き"""
    d = f.get("notify")
    if not d:
        return "-"
    if d.get("gated_abs") is None:
        return str(d["gated"])
    return f"{d['gated']} (abs {d['gated_abs']}, rel {d['gated_rel']})"


def rows_for(run: Run) -> dict[str, str]:
    f = run.final
    sl = f.get("slow")
    ins = [r[0] for r in run.rates]
    outs = [r[1] for r in run.rates]
    return {
        "ログ": run.name,
        "CONFIG": run.config_line(),
        "CONFIG → READY / CONFIG → FINAL": f"{fmt_dur(run.t_config, run.t_ready)} / {fmt_dur(run.t_config, run.t_final)}",
        "窓 取り出し / 期待": f"{g(f,'tap','windows')} / {g(f,'tap','expected')}",
        "tap overrun / torn / skipped": f"{g(f,'tap','overrun')} / {g(f,'tap','torn')} / {g(f,'tap','skipped')}",
        "tap seam ok / NG": f"{g(f,'tap','seam_ok')} / {g(f,'tap','seam_ng')}",
        "tap lag max (下限値の数)": f"{g(f,'taplag','lag_max_us')}us ({g(f,'taplag','lag_loose')})",
        "tap write max / avg (回数)": f"{g(f,'taplag','wr_max_ns')}ns / {g(f,'taplag','wr_avg_ns')}ns ({g(f,'taplag','wr_calls')})",
        "audio under / over / late": f"{g(f,'audio','under')} / {g(f,'audio','over')} / {g(f,'audio','late')}",
        "aed run / err": f"{g(f,'audio','run')} / {g(f,'audio','err')}",
        "preproc max / avg": f"{g(f,'time','pp_max')}us / {g(f,'time','pp_avg')}us",
        "infer max / avg": f"{g(f,'time','inf_max')}us / {g(f,'time','inf_avg')}us",
        "窓 1 つの実測 max / avg (前処理+推論+空回し)": f"{sl['win_max']}us / {sl['win_avg']}us" if sl else "-",
        "空回し n / max / avg / skipped": f"{sl['spin_n']} / {sl['spin_max']}us / {sl['spin_avg']}us / {sl['spin_skipped']}" if sl else "-",
        "notify out / held / offlist / gated": f"{g(f,'notify','out')} / {g(f,'notify','held')} / {g(f,'notify','offlist')} / {gated_text(f)}",
        "notify lat max (下限値の数)": f"{g(f,'notify','lat_max_us')}us ({g(f,'notify','lat_loose')})",
        "log sent / dropped": f"{g(f,'log','sent')} / {g(f,'log','dropped')}",
        "log lag max": f"{g(f,'log','lag_max_us')}us",
        "disp sent / dropped": f"{g(f,'disp','sent')} / {g(f,'disp','dropped')}",
        "disp max / avg": f"{g(f,'disp','max_us')}us / {g(f,'disp','avg_us')}us",
        "JSON lat_ms min / median / max (行数、READY 以降)": (
            f"{min(run.lat_ms)} / {med(run.lat_ms)} / {max(run.lat_ms)} ({len(run.lat_ms)})" if run.lat_ms else "- (0)"
        ),
        "画面 通知から描画まで median / max (行数、READY 以降)": (
            f"{med(run.lcd_us)}us / {max(run.lcd_us)}us ({len(run.lcd_us)})" if run.lcd_us else "- (0)"
        ),
        "in Hz 中央値 / out Hz 中央値 (行数、READY 以降)": f"{med(ins)} / {med(outs)} ({len(run.rates)})",
        "PASSTHROUGH STOPPED / tap: window lost の行数 (ログ全体)": f"{run.n_stopped} / {run.n_lost_lines}",
    }


def brief(run: Run) -> str:
    """使わなかった完走ログの要点 1 行 (再現性の参考)"""
    f = run.final
    return (f"windows {g(f,'tap','windows')}/{g(f,'tap','expected')}, overrun {g(f,'tap','overrun')}, "
            f"audio {g(f,'audio','under')}/{g(f,'audio','over')}/{g(f,'audio','late')}, "
            f"log dropped {g(f,'log','dropped')}, disp max {g(f,'disp','max_us')}us, "
            f"JSON lat_ms median {med(run.lat_ms)}")


def cond_order(chosen: dict[str, Run]) -> list[str]:
    return [c for c in ("A", "B", "D") if c in chosen] + sorted(c for c in chosen if c not in ("A", "B", "D"))


def build_table(chosen: dict[str, Run]) -> str:
    conds = cond_order(chosen)
    per = {c: rows_for(chosen[c]) for c in conds}
    labels = list(next(iter(per.values())).keys()) if per else []
    out = ["| 項目 | " + " | ".join(COND_LABEL.get(c, c) for c in conds) + " |",
           "|---|" + "---|" * len(conds)]
    for lab in labels:
        out.append(f"| {lab} | " + " | ".join(per[c].get(lab, "-") for c in conds) + " |")
    return "\n".join(out)


def build_doc(chosen: dict[str, Run], unused: list[tuple[Run, str]], how: str, n_no_config: int,
              missing_conds: list[str]) -> str:
    lines = [
        "# 3-1 対照実験の結果（A 本番 / B 優先度逆転 / D 推論を遅くする）",
        "",
        "`uv run scripts/analyze_31.py` が `logs/` の UART ログから生成した表。数字は手で書き換えない。",
        "条件の切替と期待結果は CLAUDE.md「対照実験のスイッチ」、切替は `npu/infer_task.h` の",
        "`INFER_PRIO_INVERT` / `INFER_SLOW_X`。時間の単位はログのまま（us、ns）。",
        "",
        f"使ったログ（{how}）:",
        "",
    ]
    for c in cond_order(chosen):
        r = chosen[c]
        lines.append(f"- {COND_LABEL.get(c, c)}: `{r.name}`（{r.cond_name}）")
    for c in missing_conds:
        lines.append(f"- {COND_LABEL.get(c, c)}: **ログが無い**")
    lines += ["", build_table(chosen), ""]
    lines += [
        "読み方:",
        "",
        "- 「窓 取り出し / 期待」の期待は tap リングに書いたサンプル数から見た窓の数。取り出しが少ない分は上書きで読み飛ばした窓（overrun）。",
        "- audio の under は出力側で pcm_fifo が足りず無音を埋めた回数、over は入力側で捨てた回数（HALF と FULL が同時に立った分を含む）、late は同じ側の HALF と FULL が同時に立っていた＝1 周期分間に合わなかった回数（audio_task.c）。JSON 行の under / over / late と同じカウンタ。",
        "- D の log lag max と disp max / avg は reporter・表示タスクが 10 分間動けなかった間の待ちで、DWT の差分が 7.16 秒で折り返すので目安にしかならない（CLAUDE.md）。飢えの証拠は log dropped と disp dropped。",
        "- disp max は READY 直後の 1 件で立ちやすい（起動確認の直後は表示タスクが後回しになる）。定常の描画時間は「画面 通知から描画まで」の中央値で見る。",
        "- D では PASSTHROUGH STOPPED ブロックは満杯のキューに入らず出ない（audio の値は FINAL の行にある）。tap: window lost も大半が捨てられるので、上書きの回数は tap overrun で見る。",
        "- JSON の lat_ms は窓がそろってから JSON をレポータに渡すまで。UART の待ちは含まない（それは log lag max）。",
        "- tap write max は壁時計。B では tap_ring_write の中の tk_sig_sem で推論タスクに横取りされるので、memcpy の時間ではなく横取りの長さが出る。",
        "",
    ]
    if unused:
        lines += ["使わなかったログ:", ""]
        for r, why in unused:
            extra = f" ― {brief(r)}" if r.complete else ""
            lines.append(f"- `{r.name}`（CONFIG: {r.cond} {r.cond_name}）: {why}{extra}")
        lines.append("")
    if n_no_config:
        lines += [f"CONFIG: 行の無いログ（スイッチを入れる前のもの）{n_no_config} 本は読み飛ばした。", ""]
    lines += ["## 走行集計（通知の件数と 1 時間あたりの件数）", "",
              "3.5 の誤報対策の前後を同じ形式で比べるためのもの。通知の総数は FINAL の notify out、クラス別は JSON 行の数。", "", "```"]
    for c in cond_order(chosen):
        lines += run_summary(chosen[c])
    lines += ["```", ""]
    lines += ["## 付録: FINAL ブロックの本文（ログのまま）", ""]
    for c in cond_order(chosen):
        r = chosen[c]
        lines += [f"### {COND_LABEL.get(c, c)}: `{r.name}`", "", "```"]
        lines += r.final_text
        lines += ["```", ""]
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description="3-1 対照実験の UART ログを集計して表にする")
    ap.add_argument("logs", nargs="*", type=Path, help="使うログ。省略時は PINNED のログ (--scan で logs/ の最新)")
    ap.add_argument("--scan", action="store_true", help="logs/ を走査して条件ごとに FINAL のある最新のログを使う")
    ap.add_argument("--logs-dir", type=Path, default=DEFAULT_LOGS, help=f"走査するディレクトリ (既定 {DEFAULT_LOGS})")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT, help=f"書き出す Markdown (既定 {DEFAULT_OUT}。- で書かない)")
    args = ap.parse_args()

    # 端末にはその端末の文字コード (Windows なら cp932) で出し、出せない文字は ? にする。
    # リダイレクトやパイプのときは UTF-8 (docs と同じ)
    for stream in (sys.stdout, sys.stderr):
        try:
            if stream.isatty():
                stream.reconfigure(errors="replace")
            else:
                stream.reconfigure(encoding="utf-8", errors="replace", newline="\n")
        except Exception:
            pass

    paths = args.logs if args.logs else sorted(args.logs_dir.glob("uart_*.log"))
    if not paths:
        print(f"ログが無い: {args.logs_dir}", file=sys.stderr)
        return 1
    runs = load_runs(paths)
    n_no_config = len([r for r in runs if not r.has_config])

    if args.logs:
        chosen, unused = select_newest(runs)
        how = "引数で指定。同じ条件が複数あれば最新"
    elif args.scan:
        chosen, unused = select_newest(runs)
        how = "logs/ を走査し、条件ごとに FINAL ブロックのある最新のもの（--scan）"
    else:
        chosen, unused, errors = select_pinned(runs)
        how = "scripts/analyze_31.py の PINNED で条件ごとに 1 本に固定"
        for e in errors:
            print(f"[error] {e}", file=sys.stderr)
        if errors:
            return 1
    if not chosen:
        # 表は作れない。走行集計だけ出す (対策前のログのように CONFIG: 行が無いものはここに来る)
        print("CONFIG: 行と完全な FINAL ブロックの両方があるログが無い (3-1 の表は作らない)", file=sys.stderr)
        for r, why in unused:
            print(f"  {r.name} ({r.cond}): {why}", file=sys.stderr)
        if not runs:
            return 1
        for r in runs:
            print("\n".join(run_summary(r)))
            print()
        return 0
    missing_conds = [c for c in ("A", "B", "D") if c not in chosen]
    for c in missing_conds:
        print(f"[warn] 条件 {c} の使えるログが無い", file=sys.stderr)

    doc = build_doc(chosen, unused, how, n_no_config, missing_conds)
    print(build_table(chosen))
    print()
    for r, why in unused:
        print(f"使わなかった: {r.name} ({r.cond}): {why}")
    print()
    for r in (runs if args.logs else [chosen[c] for c in cond_order(chosen)]):
        print("\n".join(run_summary(r)))
        print()
    if str(args.out) != "-":
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(doc, encoding="utf-8", newline="\n")
        print(f"書き出し: {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
