#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["onnxruntime==1.30.0", "onnx==1.23.0", "numpy==2.5.3", "scipy==1.18.1"]
# ///
"""ESC-10 の実録音を ST と同じ前処理で int8 入力にし、ONNX Runtime の1位と一緒に C ヘッダへ出す。

Phase 1 タスク6: NPU の推論が正しいかを、実録音 30 本の1位クラスを PC と比べて判定する。
ここで作る log-mel の Python 実装は、タスク8 でボード側に前処理を移植するときの参照にもなる。

使い方 (uv が依存パッケージを用意する):
    uv run scripts/aed_clips.py <STM32N6-GettingStarted-Audio> <ESC-50>                       ESC10 版 (既定)
    uv run --with tensorflow scripts/aed_clips.py --model fsd50k <ESC-50>                    FSD50K 版 (5-1)

    <STM32N6-GettingStarted-Audio>: v2.3.0 のリポジトリ (ESC10 版のモデルと前処理の設定・表を読む)
    <ESC-50>: github.com/karolpiczak/ESC-50 のリポジトリ (meta/esc50.csv と下の wav)

出力 (ESC10 版。MODEL_DIR = mtk3bsp2_stm32n657/Appli/Application/npu/model_esc10):
    MODEL_DIR/aed_test_clips.h
      30 本の int8 入力と PC の1位 (NPU の推論の判定用。npu_selftest.c の clips_check)。
      ROM が足りないときは npu_selftest.c の AED_USE_TEST_CLIPS を 0 にしてビルドから外す
    MODEL_DIR/aed_ref_clips.h
      上のうち2本ぶんの生 PCM も入れたもの (ボードの前処理を突き合わせる用。タスク8。
      infer_task.c の preproc_test)
出力 (FSD50K 版。MODEL_DIR = .../npu/model_fsd50k。モデルは MODEL_DIR の tflite、--tflite で別の場所も可):
    MODEL_DIR/aed_test_input.h   乱数入力 (seed 2026) と tflite の期待値 (scripts/aed_ref.py と同じ形式)
    MODEL_DIR/aed_ref_clips.h    play test のクリップのうち 2 本 (生 PCM + int8 テンソル + PC の1位)
    tflite は TensorFlow Lite の 2 つの実装 (最適化カーネル = ort、参照カーネル = noopt) で回す
    (ESC10 版の ONNX Runtime の最適化あり / なしにあたる)。int8 化はモデル自身の入力の scale / zero_point
    aed_test_clips.h (30 本) は FSD50K 版では作らない (ESC-50 に対応するクラスが 3 つしかない)
    aed_ref_clips.h / aed_test_clips.h は ESC-50 由来のデータを含むのでリポジトリには入れない
    (.gitignore 済み)。無ければボード側はその確認を飛ばす (__has_include)。

前処理 (ST の GettingStarted-Audio と同じ。確認箇所は各関数のコメント):
    int16 16kHz の先頭 15600 サンプル → 96 列 (ホップ 160、窓 400)
    列ごとに: /32768 → 周期ハン窓 400 → 左右 56 ずつゼロ詰めして 512 点 rfft → 振幅 |X|
            → メルフィルタ 64 本 (HTK、125〜7500Hz、正規化なし) → 0 以下は FLT_MIN → 自然対数
            → int8 = SSAT(roundf(logmel / scale + zero_point))   scale / zero_point はモデルの入力
    並び: [メル 0..63][列 0..95] (列が内側)
    ST は FP16 で計算するが、ここは float32 (学習時の Python 前処理に近い側)。差は int8 で 1 LSB 程度
"""

import argparse
import csv
import hashlib
import re
import sys
import wave
from pathlib import Path

import numpy as np

# onnx / onnxruntime / scipy は使う関数の中で import する。このファイルは
# scripts/aed_play_test.py からクリップ表とメタデータの読み込みだけを取るために
# import されるので、そのときに重い依存を要求しないようにしている

# ---------------------------------------------------------------------------
# 前処理のパラメータ。ST の Projects/Dpu/ai_model_config.h.aed と一致するかを起動時に確かめる
# (GenHeader/user_config_aed.yaml から生成されたもの)
SR = 16000
N_MELS = 64
N_COLS = 96
HOP = 160
WIN = 400
N_FFT = 512
FMIN = 125
FMAX = 7500
N_SAMPLES = HOP * (N_COLS - 1) + WIN          # 15600
PAD_L = (N_FFT - WIN) // 2                     # 56 (preproc_dpu.c の pad_left)
PAD_R = (N_FFT - WIN) // 2 + (N_FFT - WIN) % 2  # 56 (pad_right)

# 出力の並び。ST の gen_h_file.py がクラス名をソートして CLASS_LIST を作る
CLASSES = ["chainsaw", "clock_tick", "crackling_fire", "crying_baby", "dog",
           "helicopter", "rain", "rooster", "sea_waves", "sneezing"]

# ---------------------------------------------------------------------------
# 使うクリップ (ESC-50 の ESC-10 サブセット、fold 5)。
# 選び方: クラスごとにファイル名順で、先頭 15600 サンプル (16kHz) の RMS がクリップ全体の
# RMS から 6dB 以内 (= 最初の約1秒に音がある) のものを、元の録音 (src_file) が違うものを
# 優先して 3 本。fold 5 に元の録音が2つしかない crying_baby だけ同じ録音から2本。
# 条件はこのスクリプトでも毎回確かめ、外れたら警告する
CLIPS = {
    "chainsaw":       ["5-170338-A-41.wav", "5-171653-A-41.wav", "5-185579-A-41.wav"],
    "clock_tick":     ["5-201194-A-38.wav", "5-208624-A-38.wav", "5-209698-A-38.wav"],
    "crackling_fire": ["5-186924-A-12.wav", "5-189212-A-12.wav", "5-189237-A-12.wav"],
    "crying_baby":    ["5-151085-A-20.wav", "5-198411-B-20.wav", "5-198411-C-20.wav"],
    "dog":            ["5-203128-A-0.wav", "5-208030-A-0.wav", "5-212454-A-0.wav"],
    "helicopter":     ["5-177957-A-40.wav", "5-191131-A-40.wav", "5-205898-A-40.wav"],
    "rain":           ["5-181766-A-10.wav", "5-188655-A-10.wav", "5-193339-A-10.wav"],
    "rooster":        ["5-194930-A-1.wav", "5-200334-A-1.wav", "5-233160-A-1.wav"],
    "sea_waves":      ["5-200461-A-11.wav", "5-208810-B-11.wav", "5-213077-A-11.wav"],
    "sneezing":       ["5-187979-A-21.wav", "5-194533-A-21.wav", "5-202220-A-21.wav"],
}
LEVEL_MARGIN_DB = 6.0

# ---------------------------------------------------------------------------
# ボードの前処理を突き合わせる2本 (タスク8)。生 PCM (15600 x int16 = 30.5KB) も
# ヘッダに入れるので本数を絞る。
# 選び方: 上の CLIPS のうち PC の1位が正解と一致して最適化あり・なしで同じ、かつ確率が
# REF_MIN_PROB 以上のものから、音量と時間構造が対照的な2本
#   5-203128-A-0.wav  (dog,        -16dBFS) 大きい音・短い立ち上がりが並ぶ
#   5-201194-A-38.wav (clock_tick, -38dBFS) 小さい音・ほぼ無音の中に点在する (量子化の下限側を通る)
# 条件はこのスクリプトでも毎回確かめ、外れたら警告する
REF_CLIPS = ["5-203128-A-0.wav", "5-201194-A-38.wav"]
REF_MIN_PROB = 0.999

# ---------------------------------------------------------------------------
# FSD50K 版 (5-1)。モデルは ST model zoo の yamnet_e256_64x96_tl (FSD50K の 5 クラス、unknown 無し)。
# 出力の並びは config の class_names ['Speech', 'Gunshot_and_gunfire', 'Crying_and_sobbing', 'Knock', 'Glass']
# を昇順に並べたもの (ESC10 版と同じ規則。ESC10 の config も class_names は dog, chainsaw, ... の順だが
# モデルの出力は chainsaw, clock_tick, ... の昇順で、ESC-10 の 30 本で 29/30 正解だった)。
# この並びで正しいことは、下の CLIPS_FSD50K (ESC-50 の knock / glass / crying_baby) の PC の1位で毎回確かめる
CLASSES_FSD50K = ["Crying_and_sobbing", "Glass", "Gunshot_and_gunfire", "Knock", "Speech"]
CLASSES_FSD50K_CONFIG_ORDER = ["Speech", "Gunshot_and_gunfire", "Crying_and_sobbing", "Knock", "Glass"]

# play test (scripts/aed_play_test.py --model fsd50k) で鳴らすクリップ。ESC-50 の fold 5 (ESC-10 に限らない)。
# 通知対象 3 クラスに対応する door_wood_knock (30) / glass_breaking (39) / crying_baby (20) を各 2 本と、
# 対象外の clock_tick (38) を 2 本。元の録音 (src_file) が違うものを選ぶ
CLIPS_FSD50K = {
    "door_wood_knock": ["5-218980-A-30.wav", "5-235644-A-30.wav"],
    "glass_breaking":  ["5-221528-A-39.wav", "5-233605-A-39.wav"],
    "crying_baby":     ["5-151085-A-20.wav", "5-198411-B-20.wav"],
    "clock_tick":      ["5-201194-A-38.wav", "5-208624-A-38.wav"],
}
PLAY_ORDER_FSD50K = ["door_wood_knock", "glass_breaking", "crying_baby", "clock_tick"]
# ESC-50 のカテゴリ → FSD50K 版のクラス (None は対象外 = 正解なし)
TRUTH_FSD50K = {"door_wood_knock": "Knock", "glass_breaking": "Glass", "crying_baby": "Crying_and_sobbing",
                "clock_tick": None}
# ボードの前処理を突き合わせる 2 本 (FSD50K 版の aed_ref_clips.h)。knock と glass: どちらも新しいクラスで、
# 短く鋭い音 (knock) と広帯域で長い音 (glass) の対照。PC の1位が正解で p >= REF_MIN_PROB_FSD50K のこと
REF_CLIPS_FSD50K = ["5-218980-A-30.wav", "5-221528-A-39.wav"]
REF_MIN_PROB_FSD50K = 0.9

REPO = Path(__file__).resolve().parent.parent
NPU_DIR = REPO / "mtk3bsp2_stm32n657" / "Appli" / "Application" / "npu"
OUT_H = NPU_DIR / "model_esc10" / "aed_test_clips.h"
SELFTEST_C = NPU_DIR / "npu_selftest.c"
INFER_TASK_C = NPU_DIR / "infer_task.c"
REF_OUT_H = NPU_DIR / "model_esc10" / "aed_ref_clips.h"
FSD50K_DIR = NPU_DIR / "model_fsd50k"
FSD50K_TFLITE = FSD50K_DIR / "yamnet_e256_64x96_tl_int8.tflite"
FSD50K_TEST_INPUT_H = FSD50K_DIR / "aed_test_input.h"
FSD50K_REF_OUT_H = FSD50K_DIR / "aed_ref_clips.h"
MODEL_REL = Path("Projects/X-CUBE-AI/models/yamnet_1024_64x96_tl_qdq_int8.onnx")
CONFIG_REL = Path("Projects/Dpu/ai_model_config.h.aed")
TABLES_REL = Path("Projects/Dpu/user_mel_tables.c.aed")


# ---------------------------------------------------------------------------
# ST の設定・表との照合

def check_st_config(path: Path) -> None:
    """ai_model_config.h.aed の値がこのスクリプトの定数と同じか。"""
    text = path.read_text(encoding="utf-8", errors="replace")

    def define(name: str) -> str:
        m = re.search(rf"#define\s+{name}\s+(.+?)\s*(?:/\*|$)", text, re.M)
        if not m:
            sys.exit(f"{path}: {name} が無い")
        return m.group(1).strip()

    expect = {
        "CTRL_X_CUBE_AI_SENSOR_ODR": "(16000.0F)",
        "CTRL_X_CUBE_AI_PREPROC": "(CTRL_AI_SPECTROGRAM_LOG_MEL)",
        "CTRL_X_CUBE_AI_SPECTROGRAM_NMEL": f"({N_MELS}U)",
        "CTRL_X_CUBE_AI_SPECTROGRAM_COL": f"({N_COLS}U)",
        "CTRL_X_CUBE_AI_SPECTROGRAM_HOP_LENGTH": f"({HOP}U)",
        "CTRL_X_CUBE_AI_SPECTROGRAM_NFFT": f"({N_FFT}U)",
        "CTRL_X_CUBE_AI_SPECTROGRAM_WINDOW_LENGTH": f"({WIN}U)",
        "CTRL_X_CUBE_AI_SPECTROGRAM_NORMALIZE": "(0U)",
        "CTRL_X_CUBE_AI_SPECTROGRAM_FORMULA": "(MEL_HTK)",
        "CTRL_X_CUBE_AI_SPECTROGRAM_FMIN": f"({FMIN}U)",
        "CTRL_X_CUBE_AI_SPECTROGRAM_FMAX": f"({FMAX}U)",
        "CTRL_X_CUBE_AI_SPECTROGRAM_TYPE": "(SPECTRUM_TYPE_MAGNITUDE)",
        "CTRL_X_CUBE_AI_SPECTROGRAM_LOG_FORMULA": "(LOGMELSPECTROGRAM_SCALE_LOG)",
    }
    for name, value in expect.items():
        got = define(name)
        if got != value:
            sys.exit(f"{path}: {name} = {got} (このスクリプトは {value} を前提にしている)")
    m = re.search(r"CLASS_LIST\s+\{(.+?)\}", text, re.S)
    names = re.findall(r'"([^"]+)"', m.group(1)) if m else []
    if names != CLASSES:
        sys.exit(f"{path}: クラスの並びが違う: {names}")


def hann_periodic(n: int) -> np.ndarray:
    """librosa.filters.get_window('hann', n) (fftbins=True なので周期ハン窓)。
    ST の lookup_tables_generator.py:generate_hann_window_LUT と同じ"""
    k = np.arange(n, dtype=np.float64)
    return (0.5 - 0.5 * np.cos(2.0 * np.pi * k / n)).astype(np.float32)


def mel_filterbank() -> np.ndarray:
    """librosa.filters.mel(sr, n_fft, n_mels, fmin, fmax, htk=True, norm=None) と同じ計算。
    ST の lookup_tables_generator.py:generate_mel_LUTs が使う (戻り値は float32、64 x 257)"""
    hz_to_mel = lambda f: 2595.0 * np.log10(1.0 + f / 700.0)
    mel_to_hz = lambda m: 700.0 * (10.0 ** (m / 2595.0) - 1.0)
    fftfreqs = np.linspace(0.0, SR / 2.0, 1 + N_FFT // 2)
    mel_f = mel_to_hz(np.linspace(hz_to_mel(FMIN), hz_to_mel(FMAX), N_MELS + 2))
    fdiff = np.diff(mel_f)
    ramps = np.subtract.outer(mel_f, fftfreqs)
    weights = np.zeros((N_MELS, 1 + N_FFT // 2), dtype=np.float32)
    for i in range(N_MELS):
        lower = -ramps[i] / fdiff[i]
        upper = ramps[i + 2] / fdiff[i + 1]
        weights[i] = np.maximum(0.0, np.minimum(lower, upper))
    return weights


def check_st_tables(path: Path, win: np.ndarray, fb: np.ndarray) -> str:
    """user_mel_tables.c.aed (ST が生成した表) とここで作った表を比べる。"""
    text = path.read_text(encoding="utf-8", errors="replace")

    def array(name: str) -> np.ndarray:
        m = re.search(rf"{name}\[\d+\]\s*=\s*\{{(.*?)\}};", text, re.S)
        if not m:
            sys.exit(f"{path}: {name} が無い")
        return np.array([float(v.rstrip("Ff")) for v in re.findall(r"[-0-9.eE+]+[Ff]?", m.group(1))])

    st_win = array("user_win")
    st_lut = array("user_melFiltersLut")
    st_start = array("user_melFiltersStartIndices").astype(int)
    st_stop = array("user_melFiltersStopIndices").astype(int)

    start = np.array([np.nonzero(r)[0][0] for r in fb])
    stop = np.array([np.nonzero(r)[0][-1] for r in fb])
    lut = fb[np.nonzero(fb)]
    if not (np.array_equal(start, st_start) and np.array_equal(stop, st_stop) and lut.size == st_lut.size):
        sys.exit("メルフィルタの非ゼロ範囲が ST の表と違う")
    d_win = float(np.abs(win - st_win).max())
    d_lut = float(np.abs(lut - st_lut).max())
    if d_win > 1e-7 or d_lut > 1e-7:
        sys.exit(f"ST の表との差が大きい: window {d_win:.3g}, mel {d_lut:.3g}")
    return (f"window 400 max diff {d_win:.2g}, mel LUT {lut.size} coefs max diff {d_lut:.2g}, "
            f"start/stop indices identical")


# ---------------------------------------------------------------------------
# 前処理 (ST の C 実装の float32 版)

def roundf(v: np.ndarray) -> np.ndarray:
    """C の roundf (0.5 は 0 から遠い側へ)。numpy の rint (偶数丸め) とは違う"""
    return np.sign(v) * np.floor(np.abs(v) + 0.5)


def logmel_q8(x: np.ndarray, win: np.ndarray, fb: np.ndarray,
              inv_scale: np.float32, zp: int) -> np.ndarray:
    """int16 x[15600] → int8 [64][96]。
    ST の対応: preproc_dpu.c:135-143 (列のループと転置)、preproc_dpu.c:52-53,68-70 (ゼロ詰め・Ref・TopdB)、
    feature_extraction_f16.c:264-347 LogMelSpectrogramColumn_q15_f16_Q8 (量子化は :334-337)、
    audio_din_f16.c:30-41 audio_is16of16_pad (arm_q15_to_f16 = /32768)、
    feature_extraction_f16.c:73 SpectrogramColumn_f16 (MAGNITUDE)、mel_filterbank_f16.c:214 MelFilterbank_f16"""
    out = np.empty((N_MELS, N_COLS), dtype=np.int8)
    for i in range(N_COLS):
        frame = x[HOP * i: HOP * i + WIN].astype(np.float32) / np.float32(32768.0)
        buf = np.concatenate([np.zeros(PAD_L, np.float32), frame * win, np.zeros(PAD_R, np.float32)])
        mag = np.abs(np.fft.rfft(buf)).astype(np.float32)          # 257 本
        mel = (fb @ mag).astype(np.float32)                         # /ref (1.0) は省略
        mel = np.where(mel <= 0.0, np.float32(np.finfo(np.float32).tiny), mel)
        logmel = np.log(mel).astype(np.float32)
        q = roundf(logmel * inv_scale + np.float32(zp))
        out[:, i] = np.clip(q, -128, 127).astype(np.int8)          # __SSAT(.., 8)
    return out


# ---------------------------------------------------------------------------
# ESC-50 のメタデータ (scripts/aed_play_test.py もここを使う)

def read_meta(esc50: Path) -> dict[str, dict]:
    """meta/esc50.csv を filename -> 行 の辞書にする。"""
    path = esc50 / "meta" / "esc50.csv"
    if not path.exists():
        sys.exit(f"{path} が無い (ESC-50 のリポジトリを指すこと)")
    with open(path, encoding="utf-8") as f:
        return {r["filename"]: r for r in csv.DictReader(f)}


def clip_path(meta: dict[str, dict], fn: str, label: str, esc50: Path, esc10: bool = True) -> Path:
    """CLIPS の1本がメタデータ (クラス・ESC-10 (esc10=True のとき)・fold 5) と合っているか確かめ、wav の場所を返す。"""
    r = meta.get(fn)
    if r is None or r["category"] != label or (esc10 and r["esc10"] != "True"):
        sys.exit(f"{fn}: ESC-50 のメタデータと合わない")
    if r["fold"] != "5":
        sys.exit(f"{fn}: fold {r['fold']} (このスクリプトは fold 5 を前提にしている)")
    path = esc50 / "audio" / fn
    if not path.exists():
        sys.exit(f"{path} が無い (ESC-50 の audio/ を取得すること)")
    return path


def read_wav_16k(path: Path) -> tuple[np.ndarray, float, float]:
    """ESC-50 の wav (44.1kHz mono int16) を 16kHz int16 にし、先頭 15600 サンプルと、
    先頭部分・全体の RMS (dBFS) を返す"""
    from scipy.signal import resample_poly

    with wave.open(str(path)) as w:
        if (w.getframerate(), w.getnchannels(), w.getsampwidth()) != (44100, 1, 2):
            sys.exit(f"{path}: 44.1kHz mono 16bit でない")
        x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64)
    y = resample_poly(x, 160, 441)
    y16 = np.clip(np.rint(y), -32768, 32767).astype(np.int16)
    db = lambda v: 20.0 * np.log10(max(float(np.sqrt(np.mean((v / 32768.0) ** 2))), 1e-12))
    return y16[:N_SAMPLES], db(y16[:N_SAMPLES].astype(np.float64)), db(y16.astype(np.float64))


# ---------------------------------------------------------------------------
# ONNX

def onnx_input(model) -> tuple[str, np.float32, int]:
    """入力名と、入力直後 (Transpose の次) の QuantizeLinear の scale / zero_point。"""
    from onnx import numpy_helper

    g = model.graph
    inits = {t.name: numpy_helper.to_array(t) for t in g.initializer}
    consumers: dict[str, list] = {}
    for n in g.node:
        for x in n.input:
            consumers.setdefault(x, []).append(n)
    x = g.input[0].name
    for _ in range(2):
        n = consumers[x][0]
        if n.op_type == "QuantizeLinear":
            return g.input[0].name, np.float32(inits[n.input[1]].reshape(())), int(inits[n.input[2]].reshape(()))
        x = n.output[0]
    sys.exit("入力の直後に QuantizeLinear が無い")


def sessions(model_bytes: bytes) -> dict:
    import onnxruntime as ort

    out = {}
    for mode, level in (("ort", ort.GraphOptimizationLevel.ORT_ENABLE_ALL),
                        ("noopt", ort.GraphOptimizationLevel.ORT_DISABLE_ALL)):
        so = ort.SessionOptions()
        so.graph_optimization_level = level
        out[mode] = ort.InferenceSession(model_bytes, so, providers=["CPUExecutionProvider"])
    return out


# ---------------------------------------------------------------------------

def c_rows(values, per_line: int, indent: str = "\t\t") -> str:
    """整数の配列を1行 per_line 個で並べる (末尾のカンマは C では問題ない)"""
    return "\n".join(indent + ",".join(str(int(v)) for v in values[i:i + per_line]) + ","
                     for i in range(0, len(values), per_line))


def write_ref_header(clips: list[dict], scale: np.float32, zp: int, tables_note: str,
                     out_h: Path = REF_OUT_H, ref_files: list[str] = REF_CLIPS, classes: list[str] = CLASSES,
                     model_lines: list[str] | None = None, pc_a: str | None = None, pc_b: str | None = None,
                     min_prob: float = REF_MIN_PROB) -> list[dict]:
    """前処理をボードで突き合わせる2本 (生 PCM + PC の int8 テンソル + PC の1位) を出す。
    既定 (引数なし) は ESC10 版。FSD50K 版は out_h / ref_files / classes / model_lines / pc_a / pc_b を渡す"""
    if model_lines is None:
        import onnxruntime as ort
        model_lines = ["yamnet_1024_64x96_tl_qdq_int8.onnx (ESC-10) sha256 " + clips[0].get("sha256", "")]
        pc_a = f"ONNX Runtime {ort.__version__} (CPU) の既定の最適化"
        pc_b = f"ONNX Runtime {ort.__version__} (CPU) の最適化なし"

    by_file = {c["file"]: c for c in clips}
    ref = []
    for fn in ref_files:
        c = by_file.get(fn)
        if c is None:
            sys.exit(f"REF_CLIPS の {fn} が CLIPS に無い")
        if c["truth"] < 0:
            sys.exit(f"{fn}: 対象外のクラスは前処理の突き合わせに使えない")
        if c["top_ort"] != c["truth"] or c["top_noopt"] != c["truth"]:
            print(f"(!) {fn}: PC の1位が正解と違う (前処理の突き合わせには向かない)")
        elif min(c["p_ort"], c["p_noopt"]) < min_prob:
            print(f"(!) {fn}: PC の確率が {min(c['p_ort'], c['p_noopt']):.3f} < {min_prob}"
                  f" (1位が入れ替わりやすい)")
        ref.append(c)

    arr = lambda key, fmt: ", ".join(fmt(c[key]) for c in ref)
    pcm = "\n".join(f"\t{{ /* {c['file']} ({c['label']}) */\n" + c_rows(c["pcm"], 16) + "\n\t}," for c in ref)
    ten = "\n".join(f"\t{{ /* {c['file']} ({c['label']}) */\n" + c_rows(c["q"].reshape(-1), 32) + "\n\t}," for c in ref)
    model_txt = "\n".join((" *   モデル: " if i == 0 else " *           ") + line for i, line in enumerate(model_lines))
    text = f"""/* 自動生成: scripts/aed_clips.py。手で編集しない。ESC-50 由来のデータを含むのでコミットしない */
#ifndef AED_REF_CLIPS_H
#define AED_REF_CLIPS_H

#include <stdint.h>

/*
 * ボードの前処理 (Application/aed/preproc.c) を PC と突き合わせるための実録音 {len(ref)} 本 (タスク8)
 *   音声:   ESC-50 (K. J. Piczak, github.com/karolpiczak/ESC-50) fold 5。
 *           ESC-10 は CC BY 3.0、ESC-50 全体は CC BY-NC 3.0 (各クリップの出典は ESC-50 の LICENSE)
 *   pcm:    16kHz int16 に直した先頭 {N_SAMPLES} サンプル (ボードの前処理に入れる生の音)
 *   tensor: その pcm を PC が log-mel にした int8 (scripts/aed_clips.py の logmel_q8)。
 *           ボードの前処理の結果と 1 バイトずつ比べる。並びは [メル 0..63][列 0..95]
 *           {tables_note}
 *   量子化: scale={float(scale):.9g}, zero_point={zp} (モデルの入力の値。ボードは stai_network_get_info の値と照合する)
 *   1位:    上の tensor を入れたときの PC の1位と確率。ort = {pc_a}、noopt = {pc_b}
{model_txt}
 * static な配列なので、include するのは1つの .c だけにする。
 */

#define AED_REF_CLIP_COUNT	({len(ref)})
#define AED_REF_SAMPLES		({N_SAMPLES})
#define AED_REF_TENSOR_LEN	({N_MELS * N_COLS})
#define AED_REF_CLASSES		({len(classes)})
#define AED_REF_SCALE		({float(scale):.9g}f)
#define AED_REF_ZP		({zp})

/* 前処理に入れる生の音 (16kHz int16) */
static const int16_t aed_ref_pcm[AED_REF_CLIP_COUNT][AED_REF_SAMPLES] = {{
{pcm}
}};

/* PC が同じ pcm から作った int8 テンソル (ボードの前処理の答え合わせ) */
static const int8_t aed_ref_tensor[AED_REF_CLIP_COUNT][AED_REF_TENSOR_LEN] __attribute__((aligned(32))) = {{
{ten}
}};

static const char *const aed_ref_file[AED_REF_CLIP_COUNT] = {{
	{arr("file", lambda v: f'"{v}"')}
}};

/* 正解のクラス番号 (ESC-50 のラベルをモデルのクラスに対応させたもの) */
static const uint8_t aed_ref_truth[AED_REF_CLIP_COUNT] = {{
	{arr("truth", str)}
}};

/* 上の tensor での PC の1位のクラス番号と、その確率 */
static const uint8_t aed_ref_top_ort[AED_REF_CLIP_COUNT] = {{
	{arr("top_ort", str)}
}};
static const uint8_t aed_ref_top_noopt[AED_REF_CLIP_COUNT] = {{
	{arr("top_noopt", str)}
}};
static const float aed_ref_prob_ort[AED_REF_CLIP_COUNT] = {{
	{arr("p_ort", lambda v: f"{v:.6f}f")}
}};
static const float aed_ref_prob_noopt[AED_REF_CLIP_COUNT] = {{
	{arr("p_noopt", lambda v: f"{v:.6f}f")}
}};

/* クラス名 (並びは aed_test_input.h の aed_test_class_names と同じ) */
static const char *const aed_ref_class_names[AED_REF_CLASSES] = {{
	{", ".join(f'"{n}"' for n in classes)}
}};

#endif	/* AED_REF_CLIPS_H */
"""
    out_h.parent.mkdir(parents=True, exist_ok=True)
    out_h.write_text(text, encoding="utf-8", newline="\n")
    return ref


def write_header(clips: list[dict], sha256: str, scale: np.float32, zp: int, tables_note: str) -> None:
    import onnxruntime as ort

    lines = [f"\t{{ /* {c['file']} ({c['label']}) */\n" + c_rows(c["q"].reshape(-1), 32) + "\n\t},"
             for c in clips]
    arr = lambda key, fmt: ", ".join(fmt(c[key]) for c in clips)
    text = f"""/* 自動生成: scripts/aed_clips.py。手で編集しない。ESC-50 由来のデータを含むのでコミットしない */
#ifndef NPU_AED_TEST_CLIPS_H
#define NPU_AED_TEST_CLIPS_H

#include <stdint.h>

/*
 * ESC-10 の実録音 {len(clips)} 本を ST と同じ前処理で int8 にした NPU の入力と、ONNX Runtime の1位
 *   音声:   ESC-50 (K. J. Piczak, github.com/karolpiczak/ESC-50) の ESC-10 サブセット、fold 5。
 *           ESC-10 は CC BY 3.0、ESC-50 全体は CC BY-NC 3.0 (各クリップの出典は ESC-50 の LICENSE)
 *   前処理: 16kHz 先頭 15600 サンプル、log-mel 64 x 96 (scripts/aed_clips.py の logmel_q8)
 *           {tables_note}
 *   量子化: scale={float(scale):.9g}, zero_point={zp}。並びは [メル 0..63][列 0..95]
 *   モデル: yamnet_1024_64x96_tl_qdq_int8.onnx sha256 {sha256}
 *   PC:     ONNX Runtime {ort.__version__} (CPU)。ort = 既定の最適化、noopt = 最適化なし
 * クラス番号の並びは aed_test_input.h の aed_test_class_names と同じ。
 * static な配列なので、include するのは1つの .c だけにする。
 */

#define AED_CLIP_COUNT	({len(clips)})

static const int8_t aed_clip_input[AED_CLIP_COUNT][{N_MELS * N_COLS}] __attribute__((aligned(32))) = {{
{chr(10).join(lines)}
}};

static const char *const aed_clip_file[AED_CLIP_COUNT] = {{
	{arr("file", lambda v: f'"{v}"')}
}};

/* 正解のクラス番号 (ESC-50 のラベル) */
static const uint8_t aed_clip_truth[AED_CLIP_COUNT] = {{
	{arr("truth", str)}
}};

/* PC の1位のクラス番号と、その確率 */
static const uint8_t aed_clip_top_ort[AED_CLIP_COUNT] = {{
	{arr("top_ort", str)}
}};
static const uint8_t aed_clip_top_noopt[AED_CLIP_COUNT] = {{
	{arr("top_noopt", str)}
}};
static const float aed_clip_prob_ort[AED_CLIP_COUNT] = {{
	{arr("p_ort", lambda v: f"{v:.6f}f")}
}};
static const float aed_clip_prob_noopt[AED_CLIP_COUNT] = {{
	{arr("p_noopt", lambda v: f"{v:.6f}f")}
}};

#endif	/* NPU_AED_TEST_CLIPS_H */
"""
    OUT_H.write_text(text, encoding="utf-8", newline="\n")


def main_esc10(gs_audio: Path, esc50: Path) -> None:
    import onnx

    check_st_config(gs_audio / CONFIG_REL)
    win = hann_periodic(WIN)
    fb = mel_filterbank()
    tables_note = check_st_tables(gs_audio / TABLES_REL, win, fb)

    blob = (gs_audio / MODEL_REL).read_bytes()
    sha256 = hashlib.sha256(blob).hexdigest()
    name, scale, zp = onnx_input(onnx.load_from_string(blob))
    inv_scale = np.float32(1.0) / scale        # ai_dpu.c:170 の 1 / scale (ST は FP16 に丸める)
    sess = sessions(blob)

    meta = read_meta(esc50)

    print(f"ST config  : {CONFIG_REL} matches (16kHz, {N_MELS} mel x {N_COLS} col, hop {HOP}, "
          f"win {WIN}, nfft {N_FFT}, HTK {FMIN}-{FMAX}Hz, magnitude, log)")
    print(f"ST tables  : {tables_note}")
    print(f"model      : sha256 {sha256[:16]}..., input {name} scale={float(scale):.9g} zp={zp}")
    print()
    print(f"{'#':>2} {'file':<20} {'truth':<15} {'lvl/all dB':>10} {'PC ort':<15} {'p':>6} {'PC noopt':<15} {'p':>6}")

    clips = []
    for label in CLASSES:
        for fn in CLIPS[label]:
            path = clip_path(meta, fn, label, esc50)
            x, lvl, lvl_all = read_wav_16k(path)
            warn = " (!) quiet start" if lvl < lvl_all - LEVEL_MARGIN_DB else ""
            q = logmel_q8(x, win, fb, inv_scale, zp)
            xin = ((q.astype(np.float32) - np.float32(zp)) * scale).reshape(1, N_MELS, N_COLS, 1)
            c = {"file": fn, "label": label, "truth": CLASSES.index(label), "q": q, "pcm": x, "sha256": sha256}
            for mode, s in sess.items():
                y = s.run(None, {name: xin})[0].reshape(-1)
                c[f"top_{mode}"] = int(y.argmax())
                c[f"p_{mode}"] = float(y.max())
            clips.append(c)
            print(f"{len(clips) - 1:>2} {fn:<20} {label:<15} {lvl:4.0f}/{lvl_all:<4.0f}  "
                  f"{CLASSES[c['top_ort']]:<15} {c['p_ort']:6.3f} {CLASSES[c['top_noopt']]:<15} {c['p_noopt']:6.3f}{warn}")

    n = len(clips)
    agree = sum(c["top_ort"] == c["top_noopt"] for c in clips)
    acc_ort = sum(c["top_ort"] == c["truth"] for c in clips)
    acc_noopt = sum(c["top_noopt"] == c["truth"] for c in clips)
    print()
    print(f"PC top-1 == truth: ort {acc_ort}/{n}, noopt {acc_noopt}/{n}; ort == noopt: {agree}/{n}")

    write_header(clips, sha256, scale, zp, tables_note)
    ref = write_ref_header(clips, scale, zp, tables_note)
    # ボード側はヘッダが無いと __has_include で読まない。ヘッダ無しでビルドした後だと
    # 依存関係 (.d) にヘッダが載っておらず make が作り直さないので、.c の更新時刻を進める
    SELFTEST_C.touch()
    INFER_TASK_C.touch()
    print(f"wrote {OUT_H.relative_to(REPO)} ({n} x {N_MELS * N_COLS} B), touched {SELFTEST_C.name}")
    print(f"wrote {REF_OUT_H.relative_to(REPO)} ({len(ref)} clips with PCM), touched {INFER_TASK_C.name}")


# ---------------------------------------------------------------------------
# FSD50K 版: TensorFlow Lite

def tflite_interpreters(path: Path) -> tuple[dict, str]:
    """tflite を 2 つの実装で開く。ort = 最適化カーネル (XNNPACK デリゲートは外す。中間テンソルを読むため)、
    noopt = 参照カーネル (BUILTIN_REF)。どちらも中間テンソル (softmax 直前の int8) を残す"""
    try:
        import tensorflow as tf
    except ImportError:
        sys.exit("tensorflow が無い: uv run --with tensorflow scripts/aed_clips.py --model fsd50k ... で実行する")
    R = tf.lite.experimental.OpResolverType
    its = {}
    for mode, rt in (("ort", R.BUILTIN_WITHOUT_DEFAULT_DELEGATES), ("noopt", R.BUILTIN_REF)):
        it = tf.lite.Interpreter(model_path=str(path), experimental_op_resolver_type=rt,
                                 experimental_preserve_all_tensors=True, num_threads=1)
        it.allocate_tensors()
        its[mode] = it
    return its, tf.__version__


def tflite_io(it) -> dict:
    """入力 (int8 1x64x96x1) と出力 (float32 1xN) と softmax 直前の int8 テンソルの番号・量子化。"""
    inp = it.get_input_details()
    out = it.get_output_details()
    if len(inp) != 1 or len(out) != 1:
        sys.exit(f"入出力が 1 つずつでない: {len(inp)} / {len(out)}")
    if inp[0]["dtype"] != np.int8 or tuple(inp[0]["shape"]) != (1, N_MELS, N_COLS, 1):
        sys.exit(f"入力が int8 1x{N_MELS}x{N_COLS}x1 でない: {inp[0]['dtype']} {inp[0]['shape']}")
    if out[0]["dtype"] != np.float32:
        sys.exit(f"出力が float32 でない: {out[0]['dtype']} (config の quantization_output_type を確認)")
    scale, zp = inp[0]["quantization"]
    sm = [o for o in it._get_ops_details() if o["op_name"] == "SOFTMAX"]
    if len(sm) != 1:
        sys.exit(f"SOFTMAX が 1 つでない: {len(sm)} (デリゲートで融合されていないこと)")
    l_idx = int(sm[0]["inputs"][0])
    l_scale, l_zp = it.get_tensor_details()[l_idx]["quantization"]
    return {"in": inp[0]["index"], "out": out[0]["index"], "n_out": int(out[0]["shape"][-1]),
            "scale": np.float32(scale), "zp": int(zp), "logit": l_idx,
            "l_scale": np.float32(l_scale), "l_zp": int(l_zp)}


def tflite_run(it, io: dict, q: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """int8 q [64][96] を入れて (softmax 後 float32, softmax 直前 int8) を返す。"""
    it.set_tensor(io["in"], q.reshape(1, N_MELS, N_COLS, 1).astype(np.int8))
    it.invoke()
    y = it.get_tensor(io["out"]).reshape(-1).astype(np.float32)
    lq = it.get_tensor(io["logit"]).reshape(-1).astype(np.int8)
    return y, lq


def main_fsd50k(esc50: Path, tflite: Path, gs_audio: Path | None) -> None:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from aed_ref import SEED, SHAPE, write_test_input_header

    win = hann_periodic(WIN)
    fb = mel_filterbank()
    if gs_audio is not None:
        tables_note = check_st_tables(gs_audio / TABLES_REL, win, fb)
    else:
        tables_note = ("window / mel tables built by this script (same parameters as the FSD50K config: "
                       "hann 400, nfft 512, hop 160, HTK 125-7500Hz, norm None, power 1.0; "
                       "not re-checked against ST tables, pass <GettingStarted-Audio> with --gs-audio to check)")

    blob = tflite.read_bytes()
    sha256 = hashlib.sha256(blob).hexdigest()
    its, tf_ver = tflite_interpreters(tflite)
    io = tflite_io(its["ort"])
    io2 = tflite_io(its["noopt"])
    if (io["scale"], io["zp"], io["n_out"], io["logit"]) != (io2["scale"], io2["zp"], io2["n_out"], io2["logit"]):
        sys.exit("2 つの実装で入出力の情報が違う")
    if io["n_out"] != len(CLASSES_FSD50K):
        sys.exit(f"出力が {io['n_out']} クラス (CLASSES_FSD50K は {len(CLASSES_FSD50K)})")
    scale, zp = io["scale"], io["zp"]
    inv_scale = np.float32(1.0) / scale
    pc = {"ort": f"TensorFlow Lite {tf_ver} 最適化カーネル (BUILTIN、デリゲートなし)",
          "noopt": f"TensorFlow Lite {tf_ver} 参照カーネル (BUILTIN_REF)"}
    model_lines = [f"{tflite.name} (ST model zoo audio_event_detection/yamnet/ST_pretrainedmodel_public_dataset/"
                   f"fsd50k/yamnet_e256_64x96_tl/without_unknown_class、Apache-2.0)", f"sha256 {sha256}"]

    print(f"model      : {tflite.name} sha256 {sha256[:16]}..., input int8 scale={float(scale):.9g} zp={zp}, "
          f"output float32 x{io['n_out']}, softmax input tensor #{io['logit']} scale={float(io['l_scale']):.9g} zp={io['l_zp']}")
    print(f"tflite     : {pc['ort']} / {pc['noopt']}")
    print(f"tables     : {tables_note}")

    # ---- 乱数入力 (seed 2026。ESC10 版の aed_ref.py と同じ生成) → aed_test_input.h
    q = np.random.default_rng(SEED).integers(-128, 128, size=SHAPE, dtype=np.int8)
    y_a, l_a = tflite_run(its["ort"], io, q)
    y_b, l_b = tflite_run(its["noopt"], io, q)
    print()
    print(f"random input (seed {SEED}): top1 ort {int(y_a.argmax())} {CLASSES_FSD50K[int(y_a.argmax())]} p={y_a.max():.4f}, "
          f"noopt {int(y_b.argmax())} {CLASSES_FSD50K[int(y_b.argmax())]} p={y_b.max():.4f}, "
          f"max |ort - noopt| = {np.abs(y_a - y_b).max():.6f}")
    print(f"  logits int8 ort   {[int(v) for v in l_a]}")
    print(f"  logits int8 noopt {[int(v) for v in l_b]}")
    write_test_input_header(
        FSD50K_TEST_INPUT_H, q, y_a, y_b, scale, zp, CLASSES_FSD50K, model_lines, pc["ort"], pc["noopt"],
        "scripts/aed_clips.py --model fsd50k", l_a, l_b, io["l_scale"], io["l_zp"],
        "tflite の SOFTMAX の入力 (int8)。第1は最適化カーネル、第2は参照カーネルの値")

    # ---- クリップ (play test と同じ 8 本) → 並びの確認と aed_ref_clips.h
    meta = read_meta(esc50)
    print()
    print(f"{'#':>2} {'file':<20} {'esc50 label':<16} {'truth':<19} {'lvl/all dB':>10} {'PC ort':<19} {'p':>6} {'PC noopt':<19} {'p':>6}")
    clips = []
    for label in PLAY_ORDER_FSD50K:
        for fn in CLIPS_FSD50K[label]:
            path = clip_path(meta, fn, label, esc50, esc10=False)
            x, lvl, lvl_all = read_wav_16k(path)
            warn = " (!) quiet start" if lvl < lvl_all - LEVEL_MARGIN_DB else ""
            qc = logmel_q8(x, win, fb, inv_scale, zp)
            truth_name = TRUTH_FSD50K[label]
            c = {"file": fn, "label": label, "truth": CLASSES_FSD50K.index(truth_name) if truth_name else -1,
                 "q": qc, "pcm": x, "sha256": sha256}
            for mode, it in its.items():
                y, _ = tflite_run(it, io, qc)
                c[f"top_{mode}"] = int(y.argmax())
                c[f"p_{mode}"] = float(y.max())
            clips.append(c)
            print(f"{len(clips) - 1:>2} {fn:<20} {label:<16} {truth_name or '-':<19} {lvl:4.0f}/{lvl_all:<4.0f}  "
                  f"{CLASSES_FSD50K[c['top_ort']]:<19} {c['p_ort']:6.3f} "
                  f"{CLASSES_FSD50K[c['top_noopt']]:<19} {c['p_noopt']:6.3f}{warn}")

    # 出力の並びの確認: 正解のあるクリップの1位が、昇順の表と config の順の表のどちらに合うか
    target = [c for c in clips if c["truth"] >= 0]
    ok_sorted = sum(c["top_ort"] == c["truth"] for c in target)
    ok_config = sum(CLASSES_FSD50K_CONFIG_ORDER[c["top_ort"]] == TRUTH_FSD50K[c["label"]] for c in target)
    print()
    print(f"class order check ({len(target)} clips with a truth): sorted order {ok_sorted}/{len(target)} correct, "
          f"config order {ok_config}/{len(target)} correct"
          + ("  -> sorted order (CLASSES_FSD50K) confirmed" if ok_sorted > ok_config else
             "  (!) config order fits better: check CLASSES_FSD50K"))
    agree = sum(c["top_ort"] == c["top_noopt"] for c in clips)
    print(f"ort == noopt: {agree}/{len(clips)}")

    ref = write_ref_header(clips, scale, zp, tables_note, out_h=FSD50K_REF_OUT_H, ref_files=REF_CLIPS_FSD50K,
                           classes=CLASSES_FSD50K, model_lines=model_lines, pc_a=pc["ort"], pc_b=pc["noopt"],
                           min_prob=REF_MIN_PROB_FSD50K)
    SELFTEST_C.touch()
    INFER_TASK_C.touch()
    print(f"wrote {FSD50K_TEST_INPUT_H.relative_to(REPO)} (random input seed {SEED})")
    print(f"wrote {FSD50K_REF_OUT_H.relative_to(REPO)} ({len(ref)} clips with PCM), touched {SELFTEST_C.name} {INFER_TASK_C.name}")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--model", choices=["esc10", "fsd50k"], default="esc10",
                    help="esc10 (既定): <GettingStarted-Audio> <ESC-50> / fsd50k: <ESC-50>")
    ap.add_argument("--tflite", type=Path, default=FSD50K_TFLITE,
                    help=f"fsd50k のモデル (既定 {FSD50K_TFLITE.relative_to(REPO)})")
    ap.add_argument("--gs-audio", type=Path, default=None,
                    help="fsd50k のとき、ST の表と照合する STM32N6-GettingStarted-Audio (任意)")
    ap.add_argument("paths", nargs="+", type=Path, help="esc10: <GettingStarted-Audio> <ESC-50> / fsd50k: <ESC-50>")
    args = ap.parse_args()

    if args.model == "esc10":
        if len(args.paths) != 2:
            sys.exit("esc10: <STM32N6-GettingStarted-Audio> <ESC-50> の 2 つを渡す")
        main_esc10(args.paths[0], args.paths[1])
    else:
        if len(args.paths) != 1:
            sys.exit("fsd50k: <ESC-50> の 1 つを渡す (モデルは --tflite、既定はリポジトリの model_fsd50k/)")
        if not args.tflite.is_file():
            sys.exit(f"tflite が無い: {args.tflite}")
        main_fsd50k(args.paths[0], args.tflite, args.gs_audio)


if __name__ == "__main__":
    main()
