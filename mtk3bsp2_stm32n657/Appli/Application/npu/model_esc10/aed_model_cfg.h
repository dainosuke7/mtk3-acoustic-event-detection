#ifndef AED_MODEL_CFG_ESC10_H
#define AED_MODEL_CFG_ESC10_H

/*
 * ESC10 版 (Phase 1〜3 で使ってきたモデル。aed_model.h から include される)
 *
 * yamnet_1024_64x96_tl_qdq_int8.onnx (ST の STM32N6-GettingStarted-Audio v2.3.0。ST model zoo の
 * Yamnet 1024 を ESC-10 で転移学習し ONNX で int8 量子化したもの) を ST Edge AI Core 4.0.1
 * (scripts/stedgeai/generate_esc10.sh。GettingStarted の user_neural_art.json / stm32n6.mpool と同じ設定) で
 * NPU 向けに生成した network.c / stai_network.c。2026-09-27 に 4.0.0 (STAI-3.0.0-254-g7cc654104) の生成物から
 * 生成し直した (ランタイム ll_aton 1.1.3-275 に合わせるため)。差はヘッダの記述と版の行だけで、epoch の構成・番地・
 * 重みは 4.0.0 と同一。重みは外部フラッシュ 0x70180000 (aed_weights.hex、3,282,785 B。README の書き込み手順。
 * 4.0.0 で書いたものがそのまま使える)、作業領域は AXISRAM6 0x34350000 (144KB)。
 * 出力は softmax 後の float32 x10。並びはモデルの config の class_names を昇順に並べたもの
 * (ST の ai_model_config.h.aed の CTRL_X_CUBE_AI_MODEL_CLASS_LIST と同じ)
 */
#define AED_MODEL_NAME		"esc10"
#define AED_MODEL_DESC		"yamnet_1024_64x96_tl_qdq_int8.onnx (ESC-10, 10 classes, weights in external flash)"

#define AED_CLASSES		(10)
#define AED_CLASS_NAMES		{ "chainsaw", "clock_tick", "crackling_fire", "crying_baby", "dog", \
				  "helicopter", "rain", "rooster", "sea_waves", "sneezing" }
#define AED_CLASS_LIST_STR	"chainsaw clock_tick crackling_fire crying_baby dog helicopter rain rooster sea_waves sneezing"

/* 画面に出す短い英字 (lcd_task.c)。並びは上と同じ */
#define AED_DISP_NAMES		{ "CHAINSAW", "CLOCK", "FIRE", "BABY", "DOG", \
				  "HELI", "RAIN", "ROOSTER", "WAVES", "SNEEZE" }

/*
 * 通知するクラス番号 (モデルの出力順)。屋内で知らせたい音に絞る。
 * ここに無いクラスが1位になったときは LED も JSON も出さない。推論は毎窓続けるので、
 * 落とした数は notify_stats() の offlist に出る (誤報の内訳はこの数で見る)。
 * crackling_fire (2) は 1-Ex の対照試験 (1-Ex_対照試験結果.docx 4.2) で外した (2026-09-27):
 * 紙を丸める・手拍子・マグを置く生活音を crackling_fire と通知し (誤報 7 件中 6 件)、
 * 火の音のクリップは 2 本中 1 本しか拾えなかった (p=0.55)。dog 4 / crying_baby 3 / sneezing 9 の 3 クラス
 *
 * NOTIFY_CLASS_NAMES は上の番号が指すべきクラス名 (同じ順)。モデルを差し替えて出力順が
 * 変わると番号がずれるので、notify_init() がクラス名と照合して食い違いを報告する。
 * NOTIFY_COOLDOWN_S は同じクラスを続けて通知しない時間 [秒] (同じ順。0 で無し。notify.h)
 */
#define NOTIFY_CLASSES		{ 4, 3, 9 }
#define NOTIFY_CLASS_NAMES	{ "dog", "crying_baby", "sneezing" }
#define NOTIFY_COOLDOWN_S	{ 0, 0, 0 }

/* 生成物のヘッダ・ソース (npu/ からの相対パス。#include AED_xxx で使う) */
#define AED_MODEL_NETWORK_C		"model_esc10/network.c"
#define AED_MODEL_STAI_NETWORK_C	"model_esc10/stai_network.c"
#define AED_MODEL_STAI_NETWORK_H	"model_esc10/stai_network.h"
#define AED_MODEL_TEST_INPUT_H	"model_esc10/aed_test_input.h"
#define AED_MODEL_REF_CLIPS_H		"model_esc10/aed_ref_clips.h"
#define AED_MODEL_TEST_CLIPS_H	"model_esc10/aed_test_clips.h"

/*
 * セルフテストで softmax 直前の int8 ロジットを取る番地 (npu_selftest.c の logits_check。参考値)。
 * この network.c 固有の値 (npu_selftest.c の説明)。1 なら使う、0 なら logits_check を飛ばす
 */
#define AED_LOGIT_HOOK		(1)
#define AED_LOGIT_Q_ADDR	(0x34350000U)			/* epoch 29 (NPU) が書く int8 x10 */
#define AED_LOGIT_F_ADDR	(0x34350440U)			/* epoch 30 (SW DequantizeLinear) が書く float x10。64B 境界 */
#define AED_LOGIT_SCALE_ADDR	(0x70180000U + 3282320U)	/* DequantizeLinear の scale (外部フラッシュ) */
#define AED_LOGIT_ZP_ADDR	(0x70180000U + 3282784U)	/* 同 zero_point */
#define AED_LOGIT_EB_FROM_END	(3)				/* epoch 30 は epoch block 配列の末尾から 3 番目 */

#endif	/* AED_MODEL_CFG_ESC10_H */
