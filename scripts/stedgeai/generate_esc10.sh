#!/bin/bash
# ESC10 版の network.c を STEdgeAI で生成し直す (ランタイム ll_aton / NetworkRuntime をツールの版に合わせるため。5-2)。
#
#   bash scripts/stedgeai/generate_esc10.sh <STM32N6-GettingStarted-Audio> [<stedgeai.exe>]
#
# GettingStarted-Audio の Projects/X-CUBE-AI/models/generate-n6-model.sh と同じ設定:
#   user_neural_art_esc10.json / stm32n6_esc10.mpool は同リポジトリの user_neural_art.json / stm32n6.mpool の写し
#   (重みは外部フラッシュ xSPI2 0x70180000、作業域は AXISRAM6)。モデルは同リポジトリの ONNX。
# 生成物は model_esc10/ に写す。重みは st_ai_output_esc10/aed_weights.hex (書き込みは README の STM32_Programmer_CLI)。
# GettingStarted-Audio 同梱の aed_weights.bin (4.0.0 の生成物) と同じなら書き直しは要らない
set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MODEL_DIR="$ROOT/mtk3bsp2_stm32n657/Appli/Application/npu/model_esc10"
GS="$1"
STEDGEAI="${2:-${STEDGEAI:-C:/ST/STEdgeAI/4.0/Utilities/windows/stedgeai.exe}}"
ONNX="$GS/Projects/X-CUBE-AI/models/yamnet_1024_64x96_tl_qdq_int8.onnx"

if [ -z "$GS" ] || [ ! -f "$ONNX" ]; then
	echo "usage: $0 <STM32N6-GettingStarted-Audio> [<stedgeai.exe>]  (ONNX が無い: $ONNX)" >&2
	exit 1
fi
if [ ! -f "$STEDGEAI" ]; then
	echo "stedgeai が無い: $STEDGEAI" >&2
	exit 1
fi
CUBE="/c/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins"
export PATH="$(echo $CUBE/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin):$PATH"
cd "$(dirname "$0")"

"$STEDGEAI" generate -m "$ONNX" --target stm32n6 --st-neural-art default@user_neural_art_esc10.json \
	--name network --output st_ai_output_esc10 --workspace st_ai_ws_esc10

for f in network.c network.h stai_network.c stai_network.h network_c_info.json network_generate_report.txt; do
	cp "./st_ai_output_esc10/$f" "$MODEL_DIR/$f"
done
cp ./st_ai_output_esc10/network_atonbuf.xSPI2.raw ./st_ai_output_esc10/network_data.bin
arm-none-eabi-objcopy -I binary ./st_ai_output_esc10/network_data.bin --change-addresses 0x70180000 -O ihex ./st_ai_output_esc10/aed_weights.hex
ls -la ./st_ai_output_esc10/network_data.bin ./st_ai_output_esc10/aed_weights.hex
if cmp -s ./st_ai_output_esc10/network_data.bin "$GS/Projects/X-CUBE-AI/models/aed_weights.bin"; then
	echo "weights: identical to GettingStarted's aed_weights.bin (the flash does not need rewriting)"
else
	echo "weights: DIFFER from GettingStarted's aed_weights.bin -> write st_ai_output_esc10/aed_weights.hex to 0x70180000"
fi
echo "generated into $MODEL_DIR. next: check the #if LL_ATON_VERSION_* line of network.c, fill AED_LOGIT_* in aed_model_cfg.h"
