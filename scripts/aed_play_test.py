#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["numpy==2.5.3"]
# ///
"""ESC-50 のクリップとクラス外の生活音を決まった順で出し、時刻とクラスを記録する。

ボードの対照試験に使う。誤報 (鳴っていない・通知すべきでない音なのに通知が出る) と
検出漏れを、このログとボードの UART ログ (1行 JSON) を突き合わせて数える。

進行 (既定):
    0〜5秒      同期用の手拍子。ボードの peak に目印が残るので、ここで時刻を合わせる
    5〜65秒     無音。この間に出た通知は全部誤報
    65〜215秒   ESC-50 のクリップ (5秒) を15秒間隔で10本。dog / crying_baby /
                crackling_fire / sneezing / clock_tick をこの順で2巡
                (通知対象3クラス + 対象外の crackling_fire・clock_tick。crackling_fire は
                1-Ex の結果で通知対象から外したが、クリップは同じ順で鳴らす)
    215〜395秒  クラス外の生活音を15秒間隔で12回。3秒前に予告が出るので、その音を1回出す。
                ドアをノック / 手拍子 / 紙をくしゃくしゃ / 咳ばらい / 椅子を引く /
                マグを机に置く の6種を2巡。モデルの10クラスに無い音なので、
                本来は unknown か通知対象外になるべきもの

クリップはピークを -3dBFS にそろえて鳴らす (ESC-50 は録音ごとに音量が違うため)。
元の RMS と掛けたゲインはログに残す。正規化はメモリ上で行い、winsound の SND_MEMORY で
同期再生する (SND_ASYNC は winsound が許さない。PLAY_FLAGS のところに理由)。

使い方 (uv が依存パッケージを用意する):
    uv run scripts/aed_play_test.py <ESC-50>
    uv run scripts/aed_play_test.py <ESC-50> --dry-run                  クリップを鳴らさない
    uv run scripts/aed_play_test.py <ESC-50> --skip-manual              生活音の区間を飛ばす
    uv run scripts/aed_play_test.py <ESC-50> --silence 5 --interval 6   短くして動作確認

出力: logs/play_<YYYYmmdd_HHMMSS>.txt
    [+0.0s 03:11:00.004]    SYNC clap
    [+5.0s 03:11:05.012]    PHASE silence
    [+65.0s 03:12:05.120]   PHASE clips
    [+65.0s 03:12:05.123]   CLIP dog 5-203128-A-0.wav rms=-17.5dBFS gain=-2.2dB
    [+215.0s 03:14:35.208]  PHASE manual
    [+215.0s 03:14:35.210]  MANUAL knock
    (# で始まる行は条件のメモ。時刻の桁が変わっても種別の位置が揃うように空白を足している)

時刻は「開始からの経過秒」と「PC の時計 (HH:MM:SS.mmm)」の両方。UART ログにも同じ形式の
時計を付けて突き合わせる。クリップの時刻は winsound を呼ぶ直前に取っている。

注意:
    - 再生は winsound なので Windows でだけ動く。音量は OS 側で一定にしておくこと
      (このスクリプトからは OS の音量を変えられない)
    - ボードとは別時計。最初の手拍子が両方のログに残るので、それを 0 点にすると合わせやすい
    - クリップは scripts/aed_clips.py の CLIPS (ESC-10、fold 5) から取る。
      メタデータの照合も aed_clips.py の read_meta / clip_path を使う
"""

import argparse
import io
import sys
import time
import wave
from datetime import datetime
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from aed_clips import CLIPS, clip_path, read_meta  # noqa: E402

try:
    import winsound
except ImportError:		# Windows 以外。--dry-run なら動く
    winsound = None

REPO = Path(__file__).resolve().parent.parent
LOG_DIR = REPO / "logs"

SYNC_S = 5.0			# 同期用の手拍子から無音区間までの間
SILENCE_S = 60.0		# 無音 (誤報の基準)
INTERVAL_S = 15.0		# クリップ・生活音を出す間隔
CLIP_LEN_S = 5.0		# ESC-50 のクリップは全部5秒
MANUAL_NOTICE_S = 3.0		# 生活音の予告を出すタイミング (何秒前か)
TARGET_PEAK_DBFS = -3.0		# 鳴らす前にピークをここにそろえる

# メモリから鳴らすときのフラグ。SND_ASYNC は付けられない:
# winsound は SND_MEMORY と SND_ASYNC を同時に使うと
# RuntimeError "Cannot play asynchronously from memory" を投げる
# (参照カウントの面倒を避けるため CPython が禁じている)。
# 同期なので再生の間ずっと PlaySound の中にいて、渡したバイト列は clips が持ったまま生きている
PLAY_FLAGS = 0 if winsound is None else (winsound.SND_MEMORY | winsound.SND_NODEFAULT)

# 鳴らすクリップの順 (1巡5本 x REPEAT)。clock_tick は通知対象外のクラスで、
# 通知対象のクラスと同じように扱われないことを確かめるために入れる
PLAY_ORDER = ["dog", "crying_baby", "crackling_fire", "sneezing", "clock_tick"]
REPEAT = 2

# クラス外の生活音 (ログに残す名前, 画面に出す指示)。2巡する
MANUAL_ITEMS = [
    ("knock",    "ドアをノックする (2〜3回)"),
    ("handclap", "手を1回たたく"),
    ("paper",    "紙をくしゃくしゃにする"),
    ("cough",    "咳ばらいをする"),
    ("chair",    "椅子を引く"),
    ("mug",      "マグを机に置く"),
]
MANUAL_REPEAT = 2


def row(elapsed: float, clock: datetime, tag: str, detail: str) -> str:
    """ログの1行。時刻の桁が変わっても tag の位置が揃うように左詰めする。"""
    stamp = f"[+{elapsed:.1f}s {clock:%H:%M:%S}.{clock.microsecond // 1000:03d}]"
    return f"{stamp:<23} {tag} {detail}"


def countdown(t0: float, target: float, text: str) -> None:
    """開始から target 秒になるまで待つ。同じ行に「text 残り N 秒」を出し続ける。

    端末以外 (ファイルへのリダイレクトやパイプ) では上書きが効かず全部流れてしまうので出さない。
    """
    tty = sys.stdout.isatty()
    while True:
        left = t0 + target - time.monotonic()
        if left <= 0:
            break
        if tty:
            print(f"\r  {text} 残り {left:4.0f} 秒 ", end="", flush=True)
        time.sleep(min(left, 0.2))
    if tty:
        print("\r" + " " * 64 + "\r", end="", flush=True)	# 行を消す


def load_clip(path: Path) -> tuple[bytes, float, float]:
    """wav を読み、ピークを TARGET_PEAK_DBFS にそろえた WAV バイト列を作る。

    ESC-50 は録音ごとに音量が違うので、そろえないと「小さい音だから出なかった」のか
    「判定が外れた」のか分からなくなる。
    戻り値: (WAV バイト列, 元の RMS [dBFS], 掛けたゲイン [dB])
    """
    with wave.open(str(path)) as w:
        if (w.getnchannels(), w.getsampwidth()) != (1, 2):
            sys.exit(f"{path}: mono 16bit でない")
        rate = w.getframerate()
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)

    peak = float(np.max(np.abs(x))) if x.size else 0.0
    rms = float(np.sqrt(np.mean(x * x))) if x.size else 0.0
    rms_db = 20.0 * np.log10(max(rms, 1e-9) / 32768.0)

    gain = (32768.0 * 10.0 ** (TARGET_PEAK_DBFS / 20.0) / peak) if peak > 0 else 1.0
    y = np.clip(np.rint(x * gain), -32768, 32767).astype("<i2")

    buf = io.BytesIO()
    with wave.open(buf, "wb") as w2:
        w2.setnchannels(1)
        w2.setsampwidth(2)
        w2.setframerate(rate)
        w2.writeframes(y.tobytes())
    return buf.getvalue(), rms_db, 20.0 * np.log10(gain)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("esc50", type=Path, help="ESC-50 のディレクトリ (meta/ と audio/)")
    ap.add_argument("--silence", type=float, default=SILENCE_S,
                    help=f"無音の長さ [秒] (既定 {SILENCE_S})")
    ap.add_argument("--interval", type=float, default=INTERVAL_S,
                    help=f"クリップ・生活音を出す間隔 [秒] (既定 {INTERVAL_S})")
    ap.add_argument("--dry-run", action="store_true", help="クリップを鳴らさずに進行だけ")
    ap.add_argument("--skip-manual", action="store_true", help="クラス外の生活音の区間を飛ばす")
    args = ap.parse_args()

    # 同期再生なので PlaySound は再生時間ぶん戻ってこない。しかも少し余分にかかる
    # (実測: 5.00 秒の音で 5.5 秒)。間隔をクリップ長ぎりぎりにすると毎回わずかに遅れる
    if args.interval < CLIP_LEN_S:
        sys.exit(f"--interval は {CLIP_LEN_S} 秒以上にすること (クリップが重なる)")
    if winsound is None and not args.dry_run:
        sys.exit("winsound が使えない (Windows で実行するか --dry-run を付けること)")

    # cp932 に無い文字があっても表示で落ちないようにする (ログは UTF-8 で書く)
    try:
        sys.stdout.reconfigure(errors="replace")
    except (AttributeError, ValueError):
        pass

    # 鳴らす順にクリップを決めて読み込む。1巡目は各クラスの1本目、2巡目は2本目。
    # 鳴らす直前の処理を無くしておく (時刻と再生開始をずらさないため)
    meta = read_meta(args.esc50)
    clips = []
    for rep in range(REPEAT):
        for label in PLAY_ORDER:
            if rep >= len(CLIPS[label]):
                sys.exit(f"{label}: CLIPS に {rep + 1} 本目が無い")
            fn = CLIPS[label][rep]
            wav, rms_db, gain_db = load_clip(clip_path(meta, fn, label, args.esc50))
            clips.append({"label": label, "file": fn, "wav": wav,
                          "rms_db": rms_db, "gain_db": gain_db})

    # 鳴らせるかを先に試す (無音 0.1 秒なので音は出ない)。
    # 試験を始めてから「1本も鳴っていなかった」と気付くのを避ける
    if not args.dry_run:
        silent = io.BytesIO()
        with wave.open(silent, "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(44100)
            w.writeframes(b"\x00\x00" * 4410)
        try:
            winsound.PlaySound(silent.getvalue(), PLAY_FLAGS)
        except RuntimeError as e:
            sys.exit(f"再生できない: {e}")

    manual = [] if args.skip_manual else MANUAL_ITEMS * MANUAL_REPEAT

    # 区間の境目 (開始からの秒)
    t_silence = SYNC_S
    t_clips   = t_silence + args.silence
    t_manual  = t_clips + args.interval * len(clips)
    t_end     = t_manual + args.interval * len(manual)

    LOG_DIR.mkdir(exist_ok=True)
    log_path = LOG_DIR / f"play_{datetime.now():%Y%m%d_%H%M%S}.txt"

    with open(log_path, "w", encoding="utf-8", newline="\n") as log:

        def out(line: str) -> None:
            print(line)
            log.write(line + "\n")
            log.flush()		# 途中で止めても残るように毎行書き出す

        def note(line: str) -> None:
            """# 付きのメモ (画面には出さない)"""
            log.write("# " + line + "\n")
            log.flush()

        out(f"# aed_play_test start {datetime.now():%Y-%m-%d %H:%M:%S}"
            f"{' (dry run: クリップは鳴らさない)' if args.dry_run else ''}")
        out(f"# {t_silence:.1f}s silence {args.silence:.1f}s"
            f" / {t_clips:.1f}s clips {len(clips)} x {CLIP_LEN_S:.0f}s every {args.interval:.1f}s"
            f" / {t_manual:.1f}s manual {len(manual)} x every {args.interval:.1f}s"
            f" / end {t_end:.1f}s")
        out(f"# clips from {args.esc50} (ESC-10, fold 5), peak normalized to {TARGET_PEAK_DBFS}dBFS")
        out("# SYNC/PHASE/CLIP/MANUAL。時刻は 開始からの経過秒 と PC の時計 (HH:MM:SS.mmm)")

        # --- 使うクリップの一覧 (鳴らす前に全部見せる) ---
        print(f"\n=== 使うクリップ {len(clips)} 本 (ピークを {TARGET_PEAK_DBFS}dBFS にそろえる) ===")
        for i, c in enumerate(clips):
            line = (f"  {i + 1:2d} {c['label']:<15} {c['file']:<20}"
                    f" rms={c['rms_db']:6.1f}dBFS gain={c['gain_db']:+6.1f}dB")
            print(line)
            note(line[2:])	# 先頭の字下げだけ落とす (strip すると番号の桁でずれる)
        for i, (name, how) in enumerate(manual):
            note(f"manual {i + 1:2d} {name} ({how})")

        t0 = time.monotonic()

        # --- 同期用の手拍子 ---
        print("\n=== 同期 ===")
        print("  同期用に手を1回たたいてください (ボードの peak に目印が残ります)")
        out(row(0.0, datetime.now(), "SYNC", "clap"))
        countdown(t0, t_silence, "手をたたく")

        # --- 無音 ---
        print("=== 無音 (この区間の通知は誤報) ===")
        out(row(time.monotonic() - t0, datetime.now(), "PHASE", "silence"))
        countdown(t0, t_clips, "音を立てないでください")

        # --- ESC-50 のクリップ ---
        # 区間の切れ目。無音の待ちが終わった直後なので、最初の CLIP 行とほぼ同じ時刻になる
        # (ここでログを書くのは最初のクリップの時刻を取る前。再生の時刻はずらさない)
        print(f"=== クリップ {len(clips)} 本 ===")
        out(row(time.monotonic() - t0, datetime.now(), "PHASE", "clips"))
        for i, c in enumerate(clips):
            countdown(t0, t_clips + args.interval * i, f"次: {c['label']}")
            print(f"  再生: {c['label']} {c['file']}")

            # 時刻は PlaySound の直前に取る。ここから再生開始までに I/O を挟まない
            # (ログは鳴らし終わってから書く。時刻は上で取った値を使う)
            el, clock = time.monotonic() - t0, datetime.now()
            if not args.dry_run:
                try:
                    winsound.PlaySound(c["wav"], PLAY_FLAGS)
                except RuntimeError as e:
                    # 鳴らないまま試験を続けても意味が無いので止める
                    out(row(el, clock, "CLIP", f"{c['label']} {c['file']} PLAY FAILED: {e}"))
                    sys.exit(f"再生できない: {e}")
            out(row(el, clock, "CLIP", f"{c['label']} {c['file']}"
                    f" rms={c['rms_db']:.1f}dBFS gain={c['gain_db']:+.1f}dB"))

        # --- クラス外の生活音 (人が出す) ---
        if manual:
            print(f"=== クラス外の生活音 {len(manual)} 回 (予告が出たらその音を1回) ===")
        for i, (name, how) in enumerate(manual):
            target = t_manual + args.interval * i
            countdown(t0, target - MANUAL_NOTICE_S, f"次: {how} まで")
            print(f"  次: {how}  ({MANUAL_NOTICE_S:.0f} 秒後)")
            countdown(t0, target, f"{how} まで")
            if i == 0:
                # 区間の切れ目。最初の合図の時刻 (= 生活音区間の始まり) に出す。
                # ループの前に出すと、最後のクリップの間隔ぶん (既定15秒) 早い時刻になる
                out(row(time.monotonic() - t0, datetime.now(), "PHASE", "manual"))
            out(row(time.monotonic() - t0, datetime.now(), "MANUAL", name))
            print(f"  >>> いま: {how} <<<")

        countdown(t0, t_end, "終わりまで")
        out(f"# aed_play_test end (+{time.monotonic() - t0:.1f}s)")

    print(f"\nwrote {log_path.relative_to(REPO)}")


if __name__ == "__main__":
    main()
