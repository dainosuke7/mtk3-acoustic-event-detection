#ifndef NPU_NPU_RT_H
#define NPU_NPU_RT_H

#include <tk/tkernel.h>
#include "aed_model.h"		// AED_MODEL で選んだモデル (AED_CLASSES など)

/*
 * NPU 推論ランタイム (ST Edge AI の ll_aton + stai API) の起動と、推論1回分の実行枠
 * (Phase 1 タスク5)
 *
 * ランタイムとモデルは st/ 以下 (ST のファイルを無改変で同梱。st/LICENSE.md)。
 *   動作モード: LL_ATON_PLATFORM=STM32N6 / OSAL=BARE_METAL / RT_MODE=POLLING / SW_FALLBACK
 *   (定義は .cproject のプリプロセッサ定義)
 *   モデル: aed_model.h の AED_MODEL で選ぶ (model_esc10/ か model_fsd50k/)。入力 int8 1x64x96x1、
 *           出力 float32 x AED_CLASSES (softmax 後)
 *   重み: ESC10 版は外部フラッシュ 0x70180000〜 (extflash_init でメモリマップ済みであること)。
 *         FSD50K 版は AXISRAM (model_fsd50k/README.md)
 *   作業領域: AXISRAM6 0x34350000〜 (144KB。入力・出力もこの中。番地は network.c に固定)
 *
 * NPU の割り込みは使わない。ll_aton は POLLING でも stai_runtime_init() の中で
 * NPU0_IRQn (エラー通知用) を NVIC 有効にするので、npu_rt_init() が直後に無効へ戻す。
 */

#define NPU_RT_IN_BYTES		(6144)	/* int8 1x64x96x1 (メル64 x フレーム96、フレームが内側) */
#define NPU_RT_OUT_CLASSES	(AED_CLASSES)	/* float32 x クラス数 (softmax 後。モデルごと: aed_model.h) */

/*
 * 推論1回の上限。ll_aton のポーリング待ち (LL_Streng_Wait) の中で DWT CYCCNT により判定する。
 * 推論は数 ms の見込み。値はタスク6 で実測して詰める
 */
#define NPU_RT_TIMEOUT_US	(100000)

/*
 * ランタイムとネットワークを初期化し、各ステップの結果を UART に出す。推論はしない。
 * 戻り値: E_OK 成功 / E_OBJ npu_hw_init() が済んでいない / E_IO ランタイムの初期化失敗
 *
 * - npu_hw_init() が E_OK を返した後に1回だけ、NPU タスクから呼ぶ (npu_rt_run と同じ理由で
 *   usermain からは呼ばない)。NPU にクロックが入っていないと
 *   ll_aton の初期化 (LL_ATON_Init) が NPU のバージョンを読み続けて戻らないため、
 *   npu_hw_ready() でなければ何もしない
 * - 失敗しても Error_Handler() は呼ばない。呼び出し側は結果を表示して続行してよい
 */
EXPORT ER npu_rt_init(void);

/* npu_rt_init() が成功したか */
EXPORT BOOL npu_rt_ready(void);

/*
 * モデル入力 (int8) の量子化 scale / zero_point (stai_network_get_info の inputs[0])。
 * npu_rt_init が成功し、モデルが値を持っていれば TRUE。前処理 (preproc_set_quant) はこの値を使う
 * (ESC10 版 0.0305305421 / 33、FSD50K 版 0.0555472746 / 38。定数では持たない)
 */
EXPORT BOOL npu_rt_input_quant(float *scale, INT *zp);

/*
 * 入力・出力バッファ (AXISRAM6 内。ランタイムが決める番地)。準備できていなければ NULL。
 * 入力を書いたら npu_rt_run() の前に D キャッシュを clean+invalidate する
 * (入力の領域は推論中に中間結果と出力の置き場として再利用される)
 */
EXPORT B *npu_rt_input(void);
EXPORT const float *npu_rt_output(void);

/*
 * 入力 (int8 NPU_RT_IN_BYTES バイト) を入力バッファへ写し、D キャッシュを
 * clean+invalidate する (推論の直前に必ず行う操作。理由は npu_rt.c)
 */
EXPORT void npu_rt_load_input(const B *src);

/* 出力 (float x NPU_RT_OUT_CLASSES) を写す */
EXPORT void npu_rt_read_output(float *out);

/*
 * 入力を載せて1回推論し、出力を写す (load_input → run → read_output)。
 * *us に npu_rt_run の所要時間 (DWT) を返す。
 * 戻り値: E_OK / E_OBJ 未初期化 / その他 npu_rt_run() の戻り値。
 * 呼ぶのは推論タスクだけ (npu_rt_run と同じ理由)
 */
EXPORT ER npu_rt_infer(const B *in, float *out, UW *us);

/*
 * 推論を1回実行し、終わるまで待つ (ポーリング。呼び出したタスクがその間 CPU を使い続ける)。
 * 戻り値: E_OK 完了 / E_OBJ 未初期化 / E_IO ランタイムがエラーを返した・前回タイムアウトして
 *         後始末が済んでいない / E_TMOUT NPU_RT_TIMEOUT_US を超えた
 *
 * 推論の前後に TRACE (EV_INF_START / EV_INF_END) を残す。
 *
 * タイムアウトしたら TRACE (EV_INF_TMO) とカウンタに残し、UART に理由と ll_aton / NPU の状態
 * (実行中の epoch block、ストリームエンジン、割り込みコントローラ) を出して E_TMOUT で戻る
 * (assert では止めない)。ll_aton の内部状態は推論の途中のままなので、それ以降の呼び出しは
 * E_IO を返す。実行中 epoch の中断などの後始末は未実装。
 *
 * ll_aton はスレッドセーフでないので、呼ぶのは NPU タスク (npu_selftest.c) だけにする。
 * スタックを 1KB 以上使うので、usermain (初期タスク、スタック 1KB) からは呼ばない。
 */
EXPORT ER npu_rt_run(void);

/* npu_rt_run() がタイムアウトした回数 */
EXPORT UW npu_rt_timeouts(void);

/*
 * 推論の途中を覗くためのフック (デバッグ用)。各 epoch block の終了処理 (SW の演算を含む) の
 * 直前に、推論を実行しているタスクの中から呼ばれる。
 *   idx: epoch block の配列の何番目か (0 始まり)、n: 配列の長さ (終端の空の block を含む)、
 *   flags: EpochBlock_Flags_* (ll_aton_NN_interface.h)
 * 推論の途中なので、フックの中では表示などの時間のかかる処理をしない。
 * 設定・解除 (NULL) は npu_rt_run の外で行う。戻り値: E_OK / E_OBJ 未初期化
 */
typedef void (*NPU_RT_EPOCH_HOOK)(INT idx, INT n, UW flags);
EXPORT ER npu_rt_set_epoch_hook(NPU_RT_EPOCH_HOOK hook);

/*
 * 未解決: 上のフックは実際には呼ばれない。stai_network_run (__ll_aton_stai_run、
 * ll_aton_stai_internal.c:417-425) が推論のたびに epoch コールバックを、stai のコールバックが
 * 無ければ NULL に設定し直すため。直すなら stai_network_set_callback() で登録する
 * (stai が推論をまたいで保持し、同じイベント種別と epoch block を渡す)。
 * 以下は原因を実機で確かめるための記録。
 */

/* epoch コールバックの呼び出しを記録する (TRUE で記録を空にして開始、FALSE で停止) */
EXPORT void npu_rt_cb_log(BOOL on);
/* 記録した呼び出し (epoch block の番号とイベント種別) を表示する */
EXPORT void npu_rt_cb_log_show(void);
/* いま ll_aton に npu_rt の epoch コールバックが設定されているか */
EXPORT BOOL npu_rt_epoch_cb_installed(void);

#endif	/* NPU_NPU_RT_H */
