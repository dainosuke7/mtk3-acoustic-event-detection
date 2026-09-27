#!/bin/bash
# FSD50K 版 (5-1) の network.c を STEdgeAI Core 4.0 で生成する。手順は model_fsd50k/README.md。
#
#   bash scripts/stedgeai/generate_fsd50k.sh [<stedgeai.exe>]
#
# stedgeai の場所は引数か環境変数 STEDGEAI で渡す (既定: C:/ST/STEdgeAI/4.0/Utilities/windows/stedgeai.exe)。
# 生成物は st_ai_output/ に出て、network.c / network.h / stai_network.c / stai_network.h と重みの raw ファイルを
# model_fsd50k/ に写す。重みは raw2c.py で C の配列 (AXISRAM4 のセクション) にする。
#
# ESC10 版 (GettingStarted-Audio の generate-n6-model.sh) との違い:
#   - --st-neural-art axisram@user_neural_art_axisram.json: メモリプールは AXISRAM4 (重み) と AXISRAM6 (作業域)
#     だけ (stm32n6_axisram.mpool)。外部フラッシュ (xSPI2) と hyperRAM (xSPI1) を持たない
#   - --input-data-type int8 --output-data-type float32: ボードの入口は int8、出口は float32 のまま
#     (tflite 自体が int8 入力 / float32 出力なので、無くても同じになるはず。念のため明示)
#   - ランタイム (ll_aton / NetworkRuntime) の版は生成物の要求 (network.c 冒頭の #if LL_ATON_VERSION_*)
#     が repo の atonn-v1.1.3-262 / NetworkRuntime1200 と違えば、ツール同梱の両方をまとめて入れ替える
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MODEL_DIR="$ROOT/mtk3bsp2_stm32n657/Appli/Application/npu/model_fsd50k"
TFLITE="$MODEL_DIR/yamnet_e256_64x96_tl_int8.tflite"
STEDGEAI="${1:-${STEDGEAI:-C:/ST/STEdgeAI/4.0/Utilities/windows/stedgeai.exe}}"

if [ ! -x "$STEDGEAI" ] && [ ! -f "$STEDGEAI" ]; then
	echo "stedgeai が無い: $STEDGEAI (STEdgeAI Core 4.0 を入れ、場所を引数で渡す)" >&2
	exit 1
fi
cd "$(dirname "$0")"

"$STEDGEAI" generate -m "$TFLITE" --target stm32n6 --st-neural-art axisram@user_neural_art_axisram.json \
	--input-data-type int8 --output-data-type float32 --name network

for f in network.c network.h stai_network.c stai_network.h network_c_info.json network_generate_report.txt; do
	cp "./st_ai_output/$f" "$MODEL_DIR/$f"
done
ls -la ./st_ai_output/network_atonbuf.*.raw
# 重み (AXISRAM4 のプール) を C の配列にする。番地は network.c の "index=... AXISRAM4 ... offset=0x34270000" と同じこと
uv run python raw2c.py ./st_ai_output/network_atonbuf.AXISRAM4.raw "$MODEL_DIR/network_weights.c" 0x34270000

echo "generated into $MODEL_DIR. next: check the #if LL_ATON_VERSION_* line of network.c against"
echo "  mtk3bsp2_stm32n657/Appli/Application/npu/st/ll_aton/ll_aton_version.h, then bash scripts/build.sh"
