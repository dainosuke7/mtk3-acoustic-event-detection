#ifndef NPU_AED_MODEL_H
#define NPU_AED_MODEL_H

/*
 * AED モデルの切替 (5-1)。脳 (NPU 向けに生成した network.c と、それに固有のクラス表・参照テンソル) だけを
 * 差し替え、音の経路・前処理・タスク・ログ・判定規則は共通のまま使う。
 *
 * モデルごとのファイルは Application/npu/model_<name>/ に置く:
 *   network.c / network.h / stai_network.c / stai_network.h   stedgeai の生成物 (無改変)
 *   aed_model_cfg.h    クラス表・通知対象・画面の英字・クールダウン・セルフテストの番地 (手書き)
 *   aed_test_input.h   乱数入力 (seed 2026) と PC の期待値 (scripts/aed_ref.py か aed_clips.py --model fsd50k)
 *   aed_ref_clips.h    前処理の突き合わせ用 2 本 (aed_clips.py。ESC-50 由来なのでコミットしない)
 *   aed_test_clips.h   (ESC10 だけ) 30 本の判定用 (aed_clips.py。コミットしない)
 *
 * 選ぶのは AED_MODEL。ビルドに入る network.c / stai_network.c は aed_model_network.c / aed_model_stai.c が
 * ここの AED_MODEL_NETWORK_C / AED_MODEL_STAI_NETWORK_C を #include して決める (model_<name>/ は .cproject でビルド対象から
 * 外してある。2 つのモデルの network.c は同じシンボルを定義するので同時には載せられない)。
 * ヘッダも AED_MODEL_STAI_NETWORK_H / AED_MODEL_TEST_INPUT_H / AED_MODEL_REF_CLIPS_H を #include する (npu/ からの相対パス)。
 *
 * 既定は FSD50K。ただし model_fsd50k/network.c がまだ無い (生成には stedgeai が要る。model_fsd50k/README.md)
 * 間は ESC10 に倒す (ビルドが通らないより、動く方を既定にしておく)。生成すれば自動で FSD50K になる。
 * どちらが載ったかは起動ログの "CONFIG:" 行の model= と "aed model:" 行で分かる。
 * 明示するときは -DAED_MODEL=AED_MODEL_ESC10 (か下の #define を書き換える)
 */
#define AED_MODEL_ESC10		(1)
#define AED_MODEL_FSD50K	(2)

#ifndef AED_MODEL
#if __has_include("model_fsd50k/network.c")
#define AED_MODEL		AED_MODEL_FSD50K
#else
#define AED_MODEL		AED_MODEL_ESC10
#endif
#endif

#if AED_MODEL == AED_MODEL_ESC10
#include "model_esc10/aed_model_cfg.h"
#elif AED_MODEL == AED_MODEL_FSD50K
#include "model_fsd50k/aed_model_cfg.h"
#else
#error "AED_MODEL は AED_MODEL_ESC10 か AED_MODEL_FSD50K"
#endif

#endif	/* NPU_AED_MODEL_H */
