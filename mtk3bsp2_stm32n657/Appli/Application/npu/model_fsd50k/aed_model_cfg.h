#ifndef AED_MODEL_CFG_FSD50K_H
#define AED_MODEL_CFG_FSD50K_H

/*
 * FSD50K 版 (5-1 / 5-2。aed_model.h から include される)
 *
 * yamnet_e256_64x96_tl_int8.tflite: ST model zoo (github.com/STMicroelectronics/stm32ai-modelzoo、
 * audio_event_detection/yamnet/ST_pretrainedmodel_public_dataset/fsd50k/yamnet_e256_64x96_tl/without_unknown_class、
 * Apache-2.0 = このディレクトリの LICENSE.md) の Yamnet 256 を FSD50K の 5 クラスで転移学習し TFLite で int8
 * 量子化したもの (sha256 33649e4f...)。入力 int8 1x64x96x1 (scale 0.0555472746、zero_point 38)、
 * 出力 float32 x5 (int8 softmax の後に DEQUANTIZE)。前処理は ESC10 版と同じ (yamnet_e256_64x96_tl_config.yaml
 * の feature_extraction が ESC10 版の config と同一。int8 化の scale / zero_point だけ違い、ボードは
 * stai_network_get_info の値を使う)。
 *
 * 出力の並びはモデルの config の class_names ['Speech', 'Gunshot_and_gunfire', 'Crying_and_sobbing', 'Knock', 'Glass']
 * を昇順に並べたもの (ESC10 版と同じ規則)。ESC-50 の knock / glass / crying_baby のクリップで PC の1位が
 * この並びで正解になることを scripts/aed_clips.py --model fsd50k が毎回確かめる。
 *
 * network.c / stai_network.c は STEdgeAI Core 4.0.1 (scripts/stedgeai/generate_fsd50k.sh) の生成物。
 * 重みは network_weights.c (raw2c.py が作る const 配列、.npu_weights セクション) で AXISRAM4 0x34270000〜
 * (144.9KB)、作業域は AXISRAM6 0x34350000〜 (144KB)。外部フラッシュは使わない。手順は README.md
 */
#define AED_MODEL_NAME		"fsd50k"
#define AED_MODEL_DESC		"yamnet_e256_64x96_tl_int8.tflite (FSD50K subset, 5 classes, weights in AXISRAM4)"

#define AED_CLASSES		(5)
#define AED_CLASS_NAMES		{ "Crying_and_sobbing", "Glass", "Gunshot_and_gunfire", "Knock", "Speech" }
#define AED_CLASS_LIST_STR	"Crying_and_sobbing Glass Gunshot_and_gunfire Knock Speech"

/* 画面に出す短い英字 (lcd_task.c)。並びは上と同じ */
#define AED_DISP_NAMES		{ "CRY", "GLASS", "GUN", "KNOCK", "VOICE" }

/*
 * 通知するクラス (モデルの出力順の番号): Knock 3 / Glass 1 / Crying_and_sobbing 0 / Speech 4。
 * Gunshot_and_gunfire (2) は屋内の知らせる音ではないので対象外 (offlist に数える)。
 * Speech は「続いている間は 1 回」: 通知してから NOTIFY_COOLDOWN_S 秒 (30) は同じクラスを通知しない
 * (notify.h のクールダウン。他は 0 = 毎窓)
 */
#define NOTIFY_CLASSES		{ 3, 1, 0, 4 }
#define NOTIFY_CLASS_NAMES	{ "Knock", "Glass", "Crying_and_sobbing", "Speech" }
#define NOTIFY_COOLDOWN_S	{ 0, 0, 0, 30 }

/* 生成物のヘッダ・ソース (npu/ からの相対パス) */
#define AED_MODEL_NETWORK_C		"model_fsd50k/network.c"
#define AED_MODEL_WEIGHTS_C		"model_fsd50k/network_weights.c"	/* 重みの配列 (aed_model_network.c が一緒に入れる) */
#define AED_MODEL_STAI_NETWORK_C	"model_fsd50k/stai_network.c"
#define AED_MODEL_STAI_NETWORK_H	"model_fsd50k/stai_network.h"
#define AED_MODEL_TEST_INPUT_H	"model_fsd50k/aed_test_input.h"
#define AED_MODEL_REF_CLIPS_H		"model_fsd50k/aed_ref_clips.h"
#define AED_MODEL_TEST_CLIPS_H	"model_fsd50k/aed_test_clips.h"	/* FSD50K 版では作らない (無ければ飛ぶ) */

/*
 * セルフテストの softmax 直前 int8 ロジット (npu_selftest.c の logits_check。参考値)。
 * 4.0.1 で生成した network.c の末尾 (network_generate_report.txt の Epochs details):
 *   epoch 14 (NPU、Gemm_55): int8 x5 を 0x34350200 に書く (Gemm_55_out_0。scale 0.137729645、zero_point 74
 *                            = tflite の softmax 入力 tensor #40 と同じ)
 *   epoch 15 (SW、Softmax_58、整数 softmax): 0x34350200 を読み、入力の scale (float) を 0x342942c0、
 *                            zero_point (int8) を 0x342943b0 (どちらも AXISRAM4 の重みの領域) から取る。
 *                            int8 の softmax を 0x34350220 に書く。作業域 0x34350000〜 (500B)
 *   epoch 16 (SW、Dequantize_60): 0x34350220 の int8 を float x5 にして 0x34350200 (= 出力バッファ) に書く。
 *                            推論後は 0x34350200 の int8 ロジットは残らない
 * epoch block の配列は 2..16 の後に終端の空 block があるので、epoch 15 は末尾から 3 番目。
 * ESC10 と違い softmax が整数なので float の softmax 入力は無い (AED_LOGIT_F_ADDR 0 = 逆算の確認は飛ばす)
 */
#define AED_LOGIT_HOOK		(1)
#define AED_LOGIT_Q_ADDR	(0x34350200U)			/* epoch 14 (NPU) が書く int8 x5 */
#define AED_LOGIT_F_ADDR	(0U)				/* 整数 softmax なので無し */
#define AED_LOGIT_SCALE_ADDR	(0x34270000U + 148160U)		/* 0x342942c0: softmax 入力の scale (float) */
#define AED_LOGIT_ZP_ADDR	(0x34270000U + 148400U)		/* 0x342943b0: 同 zero_point (int8) */
#define AED_LOGIT_EB_FROM_END	(3)				/* epoch 15 は epoch block 配列の末尾から 3 番目 */

#endif	/* AED_MODEL_CFG_FSD50K_H */
