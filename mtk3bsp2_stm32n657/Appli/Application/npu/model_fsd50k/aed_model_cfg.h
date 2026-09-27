#ifndef AED_MODEL_CFG_FSD50K_H
#define AED_MODEL_CFG_FSD50K_H

/*
 * FSD50K 版 (5-1。aed_model.h から include される)
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
 * network.c / stai_network.c はまだ無い (STEdgeAI 4.0 の stedgeai で生成する。手順は README.md)。
 * 重みは外部フラッシュを使わず AXISRAM4 (0x34270000、448KB) に置く予定
 */
#define AED_MODEL_NAME		"fsd50k"
#define AED_MODEL_DESC		"yamnet_e256_64x96_tl_int8.tflite (FSD50K subset, 5 classes, weights in AXISRAM)"

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
#define AED_MODEL_STAI_NETWORK_C	"model_fsd50k/stai_network.c"
#define AED_MODEL_STAI_NETWORK_H	"model_fsd50k/stai_network.h"
#define AED_MODEL_TEST_INPUT_H	"model_fsd50k/aed_test_input.h"
#define AED_MODEL_REF_CLIPS_H		"model_fsd50k/aed_ref_clips.h"
#define AED_MODEL_TEST_CLIPS_H	"model_fsd50k/aed_test_clips.h"	/* FSD50K 版では作らない (無ければ飛ぶ) */

/*
 * セルフテストの softmax 直前 int8 ロジット (npu_selftest.c の logits_check)。
 * 番地は生成した network.c を読んでから入れる (どの epoch が SW の DequantizeLinear か、scale / zp の置き場)。
 * それまでは 0 = logits_check を飛ばす (乱数入力の softmax 後の比較と 10 回の連続実行は行う)
 */
#define AED_LOGIT_HOOK		(0)

#endif	/* AED_MODEL_CFG_FSD50K_H */
