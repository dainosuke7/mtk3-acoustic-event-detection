#ifndef AED_PREPROC_H
#define AED_PREPROC_H

#include <tk/tkernel.h>

/*
 * AED の前処理 (log-mel スペクトログラム。Phase 1 タスク8)
 *
 * マイクの int16 15600 サンプル (975ms) を、AED モデルの入力 int8 1x64x96x1 (6144B) にする。
 * ST の GettingStarted-Audio (Projects/Dpu/preproc_dpu.c + Middlewares の
 * AudioPreprocessing) と同じ計算を float32 で行う。PC 側の同じ実装が
 * scripts/aed_clips.py の logmel_q8 で、ボードの結果はこれと突き合わせて確かめる
 * (infer_task.c の前処理セルフテスト)。
 *
 * 仕様 (CLAUDE.md「前処理 log-mel の仕様」と同じ。確認箇所は preproc.c の各関数のコメント):
 *   列 i (0〜95) はサンプル [160i, 160i+400)
 *   列ごとに: x/32768 → 周期ハン窓 400 → 左右 56 ずつゼロ詰めして 512 点 FFT
 *           → 振幅 |X| 257 本 (2乗しない) → メルフィルタ 64 本 (HTK・125〜7500Hz・正規化なし)
 *           → 0 以下は FLT_MIN → 自然対数 (dB ではない)
 *           → int8 = SSAT(roundf(logmel * (1/scale) + zero_point), 8)
 *   並び: out[i + 96*j] = 列 i・メル j (NPU 入力 1x64x96x1 の [メル][列])
 *
 * CMSIS-DSP は使わない。窓・ツイドル・メルフィルタの表は preproc_init() が作る
 * (ROM を使わない。表を ST・PC と突き合わせた結果は CLAUDE.md に記録)。
 *
 * 作業域と表は静的領域に置く。呼び出しは推論タスク (infer_task.c) からだけにすること
 * (再入不可。他のタスクから同時に呼ぶと作業域が壊れる)。
 */

#define AED_PREPROC_SAMPLES	(15600)		/* 入力サンプル数 (160 x 95 + 400) */
#define AED_PREPROC_MELS	(64)		/* メルフィルタの数 */
#define AED_PREPROC_COLS	(96)		/* 列の数 */
#define AED_PREPROC_HOP		(160)		/* 列の間隔 */
#define AED_PREPROC_WIN		(400)		/* 窓の長さ */
#define AED_PREPROC_NFFT	(512)		/* FFT の点数 */
#define AED_PREPROC_OUT_LEN	(AED_PREPROC_MELS * AED_PREPROC_COLS)	/* 6144 */

/*
 * モデル入力の量子化 (scale / zero_point) はモデルごとに違う (ESC10 0.0305305421 / 33、
 * FSD50K 0.0555472746 / 38) ので、ここに定数は持たない。起動時に npu_rt_init が
 * stai_network_get_info から読み、推論タスクが preproc_set_quant() で渡す。PC 側 (生成ヘッダ
 * aed_ref_clips.h の AED_REF_SCALE / AED_REF_ZP) と同じ値であることは infer_task.c の
 * 前処理セルフテストが実行時に確かめる
 */

/* メルフィルタの非ゼロ係数の数。ST の表 (user_mel_tables.c.aed) と PC の表は 461 個 */
#define AED_PREPROC_MEL_COEFS	(461)

/*
 * 表を作る。preproc_run() より前に1回だけ、推論タスクから呼ぶ
 * (usermain は初期タスクでスタックが 1KB しかない。表作りは double の一時変数を使う)。
 * 戻り値: E_OK / E_NOMEM メルフィルタの係数が表に収まらない (モデルを替えたとき)
 */
EXPORT ER preproc_init(void);

/* preproc_init() が成功し、preproc_set_quant() も済んでいるか (両方そろって preproc_run が動く) */
EXPORT BOOL preproc_ready(void);

/*
 * 量子化の scale / zero_point を設定する (npu_rt_input_quant の値。preproc_run より前に 1 回)。
 * 1/scale を float で持ち、毎窓の割り算を避ける。scale が 0 以下なら未設定のまま
 */
EXPORT void preproc_set_quant(float scale, INT zp);

/* 設定済みの scale / zero_point。未設定なら FALSE */
EXPORT BOOL preproc_quant(float *scale, INT *zp);

/*
 * pcm (AED_PREPROC_SAMPLES サンプル) を out (AED_PREPROC_OUT_LEN バイト) にする。
 * 戻り値: 所要時間 [us] (DWT CYCCNT)。表が無ければ 0 を返し out は触らない。
 *
 * out は呼び出し側が用意する。NPU の入力バッファへ直接書いてもよいが、そのときは
 * 推論の前に D キャッシュを clean+invalidate すること (npu_selftest.c の run_once と同じ)
 */
EXPORT UW preproc_run(const H *pcm, B *out);

/*
 * preproc_init() が作ったメルフィルタの非ゼロ係数の数 (AED_PREPROC_MEL_COEFS と同じはず)。
 * 表が PC・ST と同じ形になっているかの目印
 */
EXPORT UINT preproc_mel_coefs(void);

#endif	/* AED_PREPROC_H */
