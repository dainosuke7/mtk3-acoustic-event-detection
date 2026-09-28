#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["onnxruntime==1.30.0", "onnx==1.23.0", "numpy==2.5.3"]
# ///
"""AED モデルを ONNX Runtime で実行し、NPU との比較用の固定入力と期待値を作る。

Phase 1 タスク5 で用意し、タスク6 でボード (NPU) の出力と比べる。

使い方 (uv が依存パッケージを用意する):
    uv run scripts/aed_ref.py <yamnet_1024_64x96_tl_qdq_int8.onnx>
        → mtk3bsp2_stm32n657/Appli/Application/npu/model_esc10/aed_test_input.h (ESC10 版)
    FSD50K 版 (tflite) の同じヘッダは scripts/aed_clips.py --model fsd50k が
    ここの write_test_input_header() を使って model_fsd50k/ に作る

モデルは STM32N6-GettingStarted-Audio v2.3.0 の
Projects/X-CUBE-AI/models/yamnet_1024_64x96_tl_qdq_int8.onnx (README 参照)。

入力の扱い:
    ONNX の入力 input_1 は float32 1x64x96x1 で、先頭の Transpose の直後に
    QuantizeLinear (scale=0.0305305421352386, zero_point=33, int8) がある。
    ボードの NPU 版 (stai_network.h) は同じ scale / zero_point の int8 を直接受け取る。
    そこで int8 の値 q を決め、ONNX には x = (q - zero_point) * scale を渡す。
    x を量子化し直すと q に戻ること (往復一致) を確かめてから実行する。
    scale / zero_point は ONNX と stai_network.h の両方から読み、一致を確かめる。

出力:
    10 クラスの float32 (softmax 後)。ONNX Runtime の既定 (QDQ を int8 演算に融合) と
    グラフ最適化なし (float で量子化を模擬) の2通りを表示し、C のヘッダにも入れる。

softmax 直前の int8 ロジット:
    グラフの末尾は Gemm → QuantizeLinear → DequantizeLinear → Softmax。
    DequantizeLinear の入力 (int8 x10) と出力 (Softmax の入力 float x10) をグラフの
    出力に追加して取り出し、scale / zero_point と合わせて表示・ヘッダに書き出す。
    出力を足しても計算が変わらないこと (softmax 後の値が元のグラフと完全一致) を確かめる。
    ボードでは NPU が同じ int8 を AXISRAM6 に書き、SW の DequantizeLinear が読む。
"""

import argparse
import hashlib
import re
import sys
from pathlib import Path

import numpy as np
import onnx
import onnxruntime as ort
from onnx import numpy_helper

SEED = 2026
SHAPE = (1, 64, 96, 1)  # [バッチ][メル 64][フレーム 96][1]
CLASSES = [  # ST の Projects/Dpu/ai_model_config.h.aed の CTRL_X_CUBE_AI_MODEL_CLASS_LIST の並び
    "chainsaw", "clock_tick", "crackling_fire", "crying_baby", "dog",
    "helicopter", "rain", "rooster", "sea_waves", "sneezing",
]

REPO = Path(__file__).resolve().parent.parent
NPU_DIR = REPO / "mtk3bsp2_stm32n657" / "Appli" / "Application" / "npu"
STAI_H = NPU_DIR / "model_esc10" / "stai_network.h"
OUT_H = NPU_DIR / "model_esc10" / "aed_test_input.h"


def onnx_input_quant(model: onnx.ModelProto) -> tuple[str, np.float32, int]:
    """入力 → Transpose → QuantizeLinear をたどり、入力名と scale / zero_point を返す。"""
    g = model.graph
    inits = {t.name: numpy_helper.to_array(t) for t in g.initializer}
    consumers: dict[str, list[onnx.NodeProto]] = {}
    for n in g.node:
        for x in n.input:
            consumers.setdefault(x, []).append(n)

    if len(g.input) != 1:
        sys.exit(f"入力が1つでない: {[i.name for i in g.input]}")
    inp = g.input[0]
    tt = inp.type.tensor_type
    dims = tuple(d.dim_value for d in tt.shape.dim)
    if tt.elem_type != onnx.TensorProto.FLOAT or dims != SHAPE:
        sys.exit(f"入力の型が想定と違う: {onnx.helper.printable_type(inp.type)}")

    x = inp.name
    for _ in range(2):  # Transpose の次が QuantizeLinear のはず
        nodes = consumers.get(x, [])
        if len(nodes) != 1:
            break
        n = nodes[0]
        if n.op_type == "QuantizeLinear":
            scale = inits[n.input[1]]
            zp = inits[n.input[2]]
            if scale.size != 1 or zp.dtype != np.int8:
                sys.exit(f"入力の量子化が per-tensor int8 でない: scale={scale}, zp={zp}")
            return inp.name, np.float32(scale.reshape(())), int(zp.reshape(()))
        x = n.output[0]
    sys.exit("入力の直後に QuantizeLinear が見つからない")


def stai_input_quant() -> tuple[np.float32, int]:
    """ボード側 (stai_network.h) の入力の scale / zero_point。"""
    text = STAI_H.read_text(encoding="utf-8")
    s = re.search(r"#define STAI_NETWORK_IN_1_SCALES\s*\\\s*\{\s*\\\s*([-0-9.eE+]+)", text)
    z = re.search(r"#define STAI_NETWORK_IN_1_OFFSETS\s*\\\s*\{\s*\\\s*(-?\d+)", text)
    if not s or not z:
        sys.exit(f"{STAI_H} から入力の scale / zero_point を読めない")
    return np.float32(s.group(1)), int(z.group(1))


def softmax_input_quant(model: onnx.ModelProto) -> tuple[str, str, np.float32, int]:
    """Softmax の入力をたどり、(int8 のテンソル名, Softmax に入る float のテンソル名, scale, zp)。"""
    g = model.graph
    inits = {t.name: numpy_helper.to_array(t) for t in g.initializer}
    producer = {o: n for n in g.node for o in n.output}

    softmax = [n for n in g.node if n.op_type == "Softmax"]
    if len(softmax) != 1:
        sys.exit(f"Softmax が1つでない: {len(softmax)}")
    dq = producer.get(softmax[0].input[0])
    if dq is None or dq.op_type != "DequantizeLinear":
        sys.exit("Softmax の直前が DequantizeLinear でない")
    scale = inits[dq.input[1]]
    zp = inits[dq.input[2]]
    if scale.size != 1 or zp.dtype != np.int8:
        sys.exit(f"Softmax 直前の量子化が per-tensor int8 でない: scale={scale}, zp={zp}")
    return dq.input[0], dq.output[0], np.float32(scale.reshape(())), int(zp.reshape(()))


def with_outputs(model: onnx.ModelProto, extra: list[tuple[str, int]]) -> bytes:
    """中間のテンソルをグラフの出力に足したモデル (シリアライズ済み)。"""
    m = onnx.ModelProto()
    m.CopyFrom(model)
    for name, elem_type in extra:
        m.graph.output.append(onnx.helper.make_tensor_value_info(name, elem_type, None))
    return m.SerializeToString()


def run(model_bytes: bytes, name: str, x: np.ndarray, optimize: bool) -> list[np.ndarray]:
    so = ort.SessionOptions()
    if not optimize:
        so.graph_optimization_level = ort.GraphOptimizationLevel.ORT_DISABLE_ALL
    sess = ort.InferenceSession(model_bytes, so, providers=["CPUExecutionProvider"])
    return [y.reshape(-1) for y in sess.run(None, {name: x})]


def c_float(v: np.float32) -> str:
    """C の float リテラル。0 や 1 のような整数値でも "0.0f" にする ("0f" は C で不正)"""
    s = f"{float(v):.9g}"
    if not any(ch in s for ch in ".eEn"):
        s += ".0"
    return s + "f"


def write_test_input_header(out_h: Path, q: np.ndarray, y_a: np.ndarray, y_b: np.ndarray,
                            scale: np.float32, zp: int, classes: list[str],
                            model_lines: list[str], pc_a: str, pc_b: str, gen_by: str,
                            l_a: np.ndarray, l_b: np.ndarray, l_scale: np.float32, l_zp: int,
                            logit_note: str) -> None:
    """乱数入力と PC の期待値を C ヘッダに書く (ESC10 版と FSD50K 版で同じ形式。npu_selftest.c が読む)。

    y_a / l_a は PC の第1の実装の softmax 後 / softmax 直前 int8 (aed_test_expect_ort / aed_test_logits_ort)、
    y_b / l_b は第2の実装 (aed_test_expect_ort_float / aed_test_logits_ort_noopt)。
    配列名は ESC10 版 (ONNX Runtime の最適化あり / なし) の名残で、FSD50K 版 (TFLite の
    最適化カーネル / 参照カーネル) も同じ名前で書く。
    """
    flat = q.reshape(-1)  # C の並び (メルが外側、フレームが内側)
    rows = ["\t" + ", ".join(f"{int(v):4d}" for v in flat[i:i + 16]) + "," for i in range(0, flat.size, 16)]
    names = ", ".join(f'"{c}"' for c in classes)
    c_int8 = lambda v: ", ".join(f"{int(x):4d}" for x in v)
    top_a, top_b = int(np.argmax(y_a)), int(np.argmax(y_b))
    model_txt = "\n".join((" *   モデル: " if i == 0 else " *           ") + line for i, line in enumerate(model_lines))
    top_note = "2つの実装で同じ" if top_a == top_b else f"2つの実装で違う ({classes[top_a]} / {classes[top_b]})。第1の実装の値"
    text = f"""/* 自動生成: {gen_by}。手で編集しない (作り直すときはスクリプトを実行する) */
#ifndef NPU_AED_TEST_INPUT_H
#define NPU_AED_TEST_INPUT_H

#include <stdint.h>

/*
 * AED モデルの比較用の固定入力と、PC で求めた期待値 (npu_selftest.c が使う)
 *
{model_txt}
 *   入力:   int8 1x64x96x1。一様乱数 [-128, 127]、numpy default_rng(seed={SEED})
 *           並びは [メル 0..63][フレーム 0..95] (フレームが内側)。NPU の入力バッファ
 *           (npu_rt_input()) にこのままコピーし、D キャッシュを clean+invalidate してから推論する
 *   量子化: scale={float(scale):.9g}, zero_point={zp} (モデルの入力の値。ボードは stai_network_get_info の値と照合する)
 *   期待値: 第1の実装 = {pc_a}
 *           第2の実装 = {pc_b}
 *           (aed_test_expect_ort / _ort_float は softmax 後の float32、aed_test_logits_* は softmax 直前の int8)
 *
 * static な配列なので、このヘッダを include するのは1つの .c だけにする。
 */

#define AED_TEST_SEED		({SEED})
#define AED_TEST_INPUT_LEN	({flat.size})
#define AED_TEST_CLASSES	({len(classes)})
#define AED_TEST_SCALE		({float(scale):.9g}f)
#define AED_TEST_ZP		({zp})

/* 乱数入力での PC の1位 ({top_note}) */
#define AED_TEST_EXPECT_TOP	"{classes[top_a]}"

static const int8_t aed_test_input[AED_TEST_INPUT_LEN] __attribute__((aligned(32))) = {{
{chr(10).join(rows)}
}};

/* 第1の実装: {pc_a} */
static const float aed_test_expect_ort[AED_TEST_CLASSES] = {{
	{", ".join(c_float(v) for v in y_a)}
}};

/* 第2の実装: {pc_b} */
static const float aed_test_expect_ort_float[AED_TEST_CLASSES] = {{
	{", ".join(c_float(v) for v in y_b)}
}};

/* クラス名 (出力の並び。モデルの config の class_names を昇順に並べたもの) */
static const char *const aed_test_class_names[AED_TEST_CLASSES] = {{
	{names}
}};

/*
 * softmax 直前の int8 ロジット。{logit_note}
 * float のロジット = (q - AED_TEST_LOGIT_ZP) * AED_TEST_LOGIT_SCALE が Softmax に入る
 */
#define AED_TEST_LOGIT_SCALE	({c_float(l_scale)})
#define AED_TEST_LOGIT_ZP	({l_zp})

/* 第1の実装 */
static const int8_t aed_test_logits_ort[AED_TEST_CLASSES] = {{
	{c_int8(l_a)}
}};

/* 第2の実装 */
static const int8_t aed_test_logits_ort_noopt[AED_TEST_CLASSES] = {{
	{c_int8(l_b)}
}};

#endif	/* NPU_AED_TEST_INPUT_H */
"""
    out_h.parent.mkdir(parents=True, exist_ok=True)
    out_h.write_text(text, encoding="utf-8", newline="\n")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("onnx", type=Path, help="yamnet_1024_64x96_tl_qdq_int8.onnx")
    args = ap.parse_args()

    blob = args.onnx.read_bytes()
    sha256 = hashlib.sha256(blob).hexdigest()
    model = onnx.load_from_string(blob)

    name, scale, zp = onnx_input_quant(model)
    b_scale, b_zp = stai_input_quant()
    if scale != b_scale or zp != b_zp:
        sys.exit(f"量子化パラメータが食い違う: ONNX scale={scale!r} zp={zp} / "
                 f"stai_network.h scale={b_scale!r} zp={b_zp}")

    q = np.random.default_rng(SEED).integers(-128, 128, size=SHAPE, dtype=np.int8)
    x = ((q.astype(np.float32) - np.float32(zp)) * scale).astype(np.float32)
    # ONNX の QuantizeLinear と同じ計算 (丸めは偶数丸め) で q に戻るか
    back = np.clip(np.rint(x / scale) + zp, -128, 127).astype(np.int8)
    if not np.array_equal(back, q):
        sys.exit(f"量子化の往復が一致しない ({np.count_nonzero(back != q)} 要素)")

    l_name, lf_name, l_scale, l_zp = softmax_input_quant(model)
    probe = with_outputs(model, [(l_name, onnx.TensorProto.INT8), (lf_name, onnx.TensorProto.FLOAT)])

    y_opt, l_opt, lf_opt = run(probe, name, x, optimize=True)
    y_flt, l_flt, lf_flt = run(probe, name, x, optimize=False)
    # 出力を足したことで計算が変わっていないか (元のグラフの softmax 後と完全一致)
    y_opt0 = run(blob, name, x, optimize=True)[0]
    y_flt0 = run(blob, name, x, optimize=False)[0]
    if not (np.array_equal(y_opt, y_opt0) and np.array_equal(y_flt, y_flt0)):
        sys.exit("中間出力を足すと softmax 後の値が変わる (最適化の結果が変わった)")
    # Softmax に入る float が (q - zp) * scale そのものか
    for lq, lf in ((l_opt, lf_opt), (l_flt, lf_flt)):
        if not np.array_equal(((lq.astype(np.float32) - np.float32(l_zp)) * l_scale).astype(np.float32), lf):
            sys.exit("Softmax に入る float が (q - zp) * scale と一致しない")

    print(f"model : {args.onnx} (sha256 {sha256[:16]}...)")
    print(f"ort   : {ort.__version__}, providers={ort.get_available_providers()}")
    print(f"input : {name} float32 {SHAPE} <- int8 q, seed={SEED}, "
          f"q min/max/mean = {q.min()}/{q.max()}/{q.mean():.2f}")
    print(f"quant : scale={float(scale):.9g} zero_point={zp} "
          "(ONNX == stai_network.h, round-trip OK)")
    print()
    print(f"{'#':>2} {'class':<15} {'ORT default':>12} {'ORT no-opt':>12}")
    for i, c in enumerate(CLASSES):
        print(f"{i:>2} {c:<15} {y_opt[i]:12.6f} {y_flt[i]:12.6f}")
    print(f"   {'sum':<15} {y_opt.sum():12.6f} {y_flt.sum():12.6f}")
    print(f"argmax: {int(y_opt.argmax())} {CLASSES[int(y_opt.argmax())]} (default) / "
          f"{int(y_flt.argmax())} {CLASSES[int(y_flt.argmax())]} (no-opt), "
          f"max |default - no-opt| = {np.abs(y_opt - y_flt).max():.6f}")

    print()
    print(f"softmax input (int8 logits): {l_name}")
    print(f"  scale={float(l_scale):.9g} zero_point={l_zp}  "
          "(float logit = (q - zero_point) * scale; probe outputs do not change results)")
    print(f"{'#':>2} {'class':<15} {'q default':>9} {'q no-opt':>9} {'diff':>5} "
          f"{'logit default':>14} {'logit no-opt':>13}")
    for i, c in enumerate(CLASSES):
        print(f"{i:>2} {c:<15} {int(l_opt[i]):9d} {int(l_flt[i]):9d} "
              f"{int(l_opt[i]) - int(l_flt[i]):5d} {lf_opt[i]:14.6f} {lf_flt[i]:13.6f}")

    write_test_input_header(
        OUT_H, q, y_opt, y_flt, scale, zp, CLASSES,
        [f"{args.onnx.name} (ESC-10。ST の GettingStarted-Audio v2.3.0)", f"sha256 {sha256}"],
        f"ONNX Runtime {ort.__version__} (CPU) の既定 (QDQ を int8 演算に融合)",
        f"ONNX Runtime {ort.__version__} (CPU) のグラフ最適化なし (float で量子化を模擬)",
        "scripts/aed_ref.py", l_opt, l_flt, l_scale, l_zp,
        "ONNX の DequantizeLinear の入力。第1は int8 演算の値、第2は float で計算して QuantizeLinear で丸めた値")
    print(f"\nwrote {OUT_H.relative_to(REPO)}")


if __name__ == "__main__":
    main()
