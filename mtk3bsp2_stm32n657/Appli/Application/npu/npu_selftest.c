#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <string.h>		// memcpy, memcmp, strcmp
#include "main.h"		// CMSIS (SCB_CleanInvalidateDCache_by_Addr, SCB_InvalidateDCache_by_Addr)
#include "npu_selftest.h"
#include "npu_rt.h"
#include AED_MODEL_TEST_INPUT_H	// 固定入力と期待値 (aed_model.h で選んだモデルの生成ヘッダ。include はこのファイルだけ)
#include "ll_aton_NN_interface.h"	// EpochBlock_Flags_pure_sw

/*
 * 1: ESC-10 の実録音 30 本で NPU の推論を判定する (aed_test_clips.h。入力だけでコード領域が
 * 184,320B 増える)。0 にするとヘッダがあってもビルドから外れる。
 * タスク8 の前処理セルフテスト (infer_task.c) が生 PCM 入りのヘッダ (aed_ref_clips.h、
 * 2 本で 74,688B) を持つので、両方を同時に載せるとコード領域 511KB に収まらない。
 * NPU の推論そのものを 30 本で確かめ直すときだけ 1 に戻す (そのときは infer_task.c の
 * PREPROC_TEST を 0 にする)
 */
#define AED_USE_TEST_CLIPS	(0)

/*
 * ESC-10 の実録音 30 本の入力と PC の1位 (scripts/aed_clips.py の生成物)。ESC-50 由来のデータ
 * なのでコミットしない (.gitignore)。無ければ実録音の判定だけを飛ばしてビルドは通す
 */
#if AED_USE_TEST_CLIPS && __has_include(AED_MODEL_TEST_CLIPS_H)
#include AED_MODEL_TEST_CLIPS_H
#define NPU_HAVE_CLIPS		(1)
#else
#define NPU_HAVE_CLIPS		(0)
#endif
#include "../trace/trace.h"	// NOW(), trace_cyc_to_us()
#include "../trace/log.h"	// log_block_begin / log_block_end (レポータ前なので直接出る)

/*
 * 固定入力による推論の確認 (Phase 1 タスク6)。呼ぶのは推論タスク (infer_task.c) だけ
 * (ll_aton はスレッドセーフでなく、推論はスタックを 1KB 以上使うため。infer_task.c の先頭)。
 *
 *   npu_selftest():
 *      - 乱数入力で1回推論し、ONNX Runtime の期待値2通りと比べる (参考値。判定しない)
 *      - 同じ乱数入力で softmax 直前の int8 ロジットを比べる (参考値。フックの呼ばれ方も記録)
 *      - ESC-10 の実録音 30 本を推論し、1位を PC と比べる (これで PASS / FAIL。
 *        AED_USE_TEST_CLIPS が 0 のときは飛ばし、2 本での判定を前処理セルフテストに任せる)
 *      - 乱数入力で10回連続。毎回の推論時間と、最初の推論の出力との差
 *   npu_selftest_run_fixed():
 *      乱数入力で1回推論し、最初の推論の出力と比べる (パススルー中の推論の予行に使う)
 */

/*
 * 乱数入力の目安 (参考値。判定には使わない): 1位がこのクラスで、期待値2通りのどちらかとの差
 * (各クラスの差の最大値) が上限以内。外れたら入力バッファの先頭を出してキャッシュを確かめる
 */
#define ST_EXPECT_TOP		AED_TEST_EXPECT_TOP	/* PC の1位 (生成ヘッダ aed_test_input.h) */
#define ST_TOLERANCE		(0.05f)

#define ST_REPEAT		(10)		/* 連続実行の回数 */

_Static_assert(NPU_SELFTEST_TOL_X1E4 == 500, "keep in sync with ST_TOLERANCE");

#define HEAD_BYTES		(16)		/* 失敗時に表示する入力バッファの先頭 */
#define LINE_BYTES		(32)		/* D キャッシュのライン */

/*
 * softmax 直前の int8 ロジット。番地はモデルごと (model_<name>/aed_model_cfg.h の AED_LOGIT_*。
 * AED_LOGIT_HOOK が 0 のモデルでは logits_check を飛ばす)。以下は ESC10 版の network.c の説明
 *   epoch 29 (NPU): 最後の Gemm の結果を int8 x10 で 0x34350000 に書く
 *                   (出力はストリームエンジン0。network.c:10939 で開始時にこのラインを invalidate)
 *   epoch 30 (SW) : DequantizeLinear。0x34350000 の int8 を読み、float x10 を 0x34350440 に書く。
 *                   scale (float) は 0x704a1590、zero_point (int8) は 0x704a1760 (network.c:11014-11034)
 *   epoch 31 (SW) : Softmax。0x34350440 を読んで 0x34350410 に出力し、0x34350000〜 を作業域に使う
 *                   (network.c:11079-11103) → 推論が終わると 0x34350000 の int8 は残っていない
 * そこで ll_aton の epoch コールバックで epoch 30 の終了処理 (= DequantizeLinear) の直前に取る。
 * epoch block の配列は epoch 1 から並び、最後に終端の空の block がある
 * (network.c の LL_ATON_EpochBlockItems_network)。epoch 30 は末尾から3番目。
 * Softmax の入力 (0x34350440 の float) は推論の後も残っているので、推論後に読んで int8 に戻し、
 * 取った int8 と一致するかも確かめる。
 */
#if AED_LOGIT_HOOK
#define LOGIT_Q_ADDR		AED_LOGIT_Q_ADDR
#define LOGIT_F_ADDR		AED_LOGIT_F_ADDR	/* 64B 境界 */
#define LOGIT_SCALE_ADDR	AED_LOGIT_SCALE_ADDR
#define LOGIT_ZP_ADDR		AED_LOGIT_ZP_ADDR
#define LOGIT_EB_FROM_END	AED_LOGIT_EB_FROM_END
#endif
#define LOGIT_TOL_LSB		(2)			/* 目安 (参考値): PC との差がこれ以内 */

_Static_assert(AED_TEST_INPUT_LEN == NPU_RT_IN_BYTES, "test input size");
_Static_assert(AED_TEST_CLASSES == NPU_RT_OUT_CLASSES, "test output size");

LOCAL float	ref_out[NPU_RT_OUT_CLASSES];	/* 最初の推論の出力。連続実行と予行の比較の基準 */
LOCAL BOOL	ref_valid = FALSE;

#if AED_LOGIT_HOOK
LOCAL B		logit_q[NPU_RT_OUT_CLASSES];	/* epoch 30 の直前に取った int8 ロジット */
LOCAL BOOL	logit_got;
LOCAL UW	logit_flags;			/* 取ったときの epoch block の flags */
#endif

/* ---------------------------------------------------------------- */

/* tm_printf は浮動小数点を出せないので、1万倍して丸めた整数で出す */
LOCAL INT x10k(float v)
{
	return (INT)(v * 10000.0f + ((v >= 0.0f) ? 0.5f : -0.5f));
}

LOCAL INT argmax(const float *v)
{
	INT	i, top = 0;

	for(i = 1; i < NPU_RT_OUT_CLASSES; i++) {
		if(v[i] > v[top]) top = i;
	}
	return top;
}

LOCAL float max_abs_diff(const float *a, const float *b)
{
	float	d, m = 0.0f;
	INT	i;

	for(i = 0; i < NPU_RT_OUT_CLASSES; i++) {
		d = a[i] - b[i];
		if(d < 0.0f) d = -d;
		if(d > m) m = d;
	}
	return m;
}

LOCAL void show_head(const char *label, const B *p)
{
	INT	i;

	tm_printf((UB*)"    %s:", label);
	for(i = 0; i < HEAD_BYTES; i++) tm_printf((UB*)" %02x", (UW)(UB)p[i]);
	tm_printf((UB*)"\n");
}

/*
 * 固定入力を NPU の入力バッファへ写して1回推論し、出力を out に写す。
 * *us に npu_rt_run の所要時間 (DWT) を返す。
 * src は int8 の入力 NPU_RT_IN_BYTES バイト。
 * head_before が NULL でなければ、推論の直前に RAM にある入力の先頭を写す。
 * in_diff が NULL でなければ、推論の直前に RAM の入力全体を src と比べ、
 * 食い違うバイト数を返す。
 */
LOCAL ER run_once(const B *src, float *out, UW *us, B *head_before, UW *in_diff)
{
	B		*in = npu_rt_input();
	UW		t0, n;
	INT		i;
	ER		er;

	/* 入力を写して D キャッシュを clean+invalidate する (理由は npu_rt.c の npu_rt_load_input) */
	npu_rt_load_input(src);

	/*
	 * ラインを捨てた後なので、ここで読むと RAM の中身が見える。読んだラインはクリーンな
	 * ので推論の邪魔はしない (CPU が NPU の結果を読む箇所は、network.c が先に invalidate する)
	 */
	if(head_before != NULL) memcpy(head_before, in, HEAD_BYTES);
	if(in_diff != NULL) {
		for(i = 0, n = 0; i < NPU_RT_IN_BYTES; i++) {
			if(in[i] != src[i]) n++;
		}
		*in_diff = n;
	}

	t0  = NOW();
	er  = npu_rt_run();
	*us = trace_cyc_to_us((UW)(NOW() - t0));
	if(er != E_OK) return er;

	npu_rt_read_output(out);
	return E_OK;
}

/*
 * 入力バッファの先頭を、推論前 (RAM)・推論後 (キャッシュ越し)・推論後 (RAM)・期待値で並べる。
 * 推論後は領域が中間結果で上書きされているのが正常。キャッシュ越しと RAM が食い違えば、
 * CPU のキャッシュに古いラインが残っている。
 * 推論後の先頭ラインは network.c が最後に clean している (0x34350000〜+1088) ので、
 * ここで invalidate しても失うデータは無い
 */
LOCAL void show_input_heads(const B *src, const B *before)
{
	B	*in = npu_rt_input();
	B	after_cache[HEAD_BYTES], after_ram[HEAD_BYTES];

	memcpy(after_cache, in, HEAD_BYTES);
	SCB_InvalidateDCache_by_Addr((volatile void *)in, LINE_BYTES);
	memcpy(after_ram, in, HEAD_BYTES);

	tm_printf((UB*)"  input buffer head @ 0x%08x:\n", (UW)in);
	show_head("expected (source input)  ", src);
	show_head("before inference (RAM)   ", before);
	show_head("after inference (cache)  ", after_cache);
	show_head("after inference (RAM)    ", after_ram);
}

/* ---------------------------------------------------------------- */
/* softmax 直前の int8 ロジット                                        */
/* ---------------------------------------------------------------- */

#if AED_LOGIT_HOOK
/* npu_rt の epoch フック。推論の途中 (このタスクの中) で呼ばれるので表示はしない */
LOCAL void logit_hook(INT idx, INT n, UW flags)
{
	if(idx != n - LOGIT_EB_FROM_END) return;

	/*
	 * NPU (epoch 29) が RAM に書いた値を読むので、読む前に invalidate する。このラインは
	 * epoch 29 の開始時に network.c が invalidate しており、その後 CPU は書いていないので、
	 * もう一度 invalidate しても失うデータは無い
	 */
	SCB_InvalidateDCache_by_Addr((volatile void *)LOGIT_Q_ADDR, LINE_BYTES);
	memcpy(logit_q, (const void *)LOGIT_Q_ADDR, sizeof(logit_q));
	logit_flags = flags;
	logit_got   = TRUE;
}

/*
 * 1回推論して、softmax 直前の int8 ロジットを PC (ONNX Runtime) と比べる (乱数入力。参考値)。
 * 目安: 10個すべてが PC のどちらかの値と ±LOGIT_TOL_LSB 以内で、scale / zero_point が PC と同じ。
 * フックが呼ばれない件 (npu_rt.h の「未解決」) を確かめるため、epoch コールバックが推論の
 * 前後で設定されているかと、推論中の呼び出しを全部記録して表示する
 */
LOCAL BOOL logits_check(void)
{
	float	out[NPU_RT_OUT_CLASSES], lf[NPU_RT_OUT_CLASSES];
	float	b_scale, pc_scale = AED_TEST_LOGIT_SCALE, v;
	B	b_zp;
	UW	us;
	INT	i, d_opt, d_flt, max_opt = 0, max_flt = 0, max_pc = 0, back;
	BOOL	back_ok = TRUE, scale_ok, zp_ok, pass_opt, pass_flt, pass, cb_before, cb_after;
	ER	er;

	logit_got = FALSE;
	(void)npu_rt_set_epoch_hook(logit_hook);
	cb_before = npu_rt_epoch_cb_installed();
	npu_rt_cb_log(TRUE);
	er = run_once(aed_test_input, out, &us, NULL, NULL);
	npu_rt_cb_log(FALSE);
	cb_after = npu_rt_epoch_cb_installed();
	(void)npu_rt_set_epoch_hook(NULL);

	tm_printf((UB*)"npu logits (reference only; int8 softmax input captured just before epoch 30 @ 0x%08x):\n",
			LOGIT_Q_ADDR);
	tm_printf((UB*)"  [npu] epoch callback installed: before run=%d, after run=%d\n",
			cb_before ? 1 : 0, cb_after ? 1 : 0);
	npu_rt_cb_log_show();
	if(er != E_OK || !logit_got) {
		tm_printf((UB*)"  not captured (er=%d, captured=%d)\n", er, logit_got ? 1 : 0);
		return FALSE;
	}
	if((logit_flags & EpochBlock_Flags_pure_sw) == 0) {
		/* 末尾から3番目が SW の block でない = network.c が想定と違う */
		tm_printf((UB*)"  [WARN] captured block is not pure SW (flags=0x%x): check LOGIT_* against network.c\n",
				logit_flags);
	}

	/*
	 * ボードの scale / zero_point: softmax 直前の int8 の量子化。SW epoch が重みの領域から読む値
	 * (ESC10: DequantizeLinear が外部フラッシュから。FSD50K: 整数 softmax が AXISRAM4 から)
	 */
	b_scale  = *(const volatile float *)LOGIT_SCALE_ADDR;
	b_zp     = *(const volatile B *)LOGIT_ZP_ADDR;
	scale_ok = (memcmp(&b_scale, &pc_scale, sizeof(float)) == 0);
	zp_ok    = ((INT)b_zp == AED_TEST_LOGIT_ZP);
	tm_printf((UB*)"  scale: board %u e-9 (flash 0x%08x), pc %u e-9 -> %s\n",
			(UW)(b_scale * 1e9f + 0.5f), LOGIT_SCALE_ADDR, (UW)(pc_scale * 1e9f + 0.5f),
			scale_ok ? "same" : "DIFFERENT");
	tm_printf((UB*)"  zero_point: board %d (flash 0x%08x), pc %d -> %s\n",
			(INT)b_zp, LOGIT_ZP_ADDR, AED_TEST_LOGIT_ZP, zp_ok ? "same" : "DIFFERENT");

	/*
	 * Softmax の入力 (epoch 30 の出力 float) を読み、int8 に戻して取った値と比べる。
	 * CPU (epoch 30) が書いて network.c が clean 済みのラインなので、invalidate しても
	 * 失うものは無い (0x34350440〜0x3435047F。epoch 31 はここに書かない)
	 */
#if AED_LOGIT_F_ADDR != 0
	SCB_InvalidateDCache_by_Addr((volatile void *)LOGIT_F_ADDR, 2 * LINE_BYTES);
	memcpy(lf, (const void *)LOGIT_F_ADDR, sizeof(lf));
#else
	/*
	 * 整数 softmax のモデル (FSD50K): float の softmax 入力は無いので、取った int8 を scale / zp で
	 * float に直した値を表示だけする (逆算の確認は自明に一致)
	 */
	for(i = 0; i < NPU_RT_OUT_CLASSES; i++) lf[i] = ((float)logit_q[i] - (float)b_zp) * b_scale;
#endif

	tm_printf((UB*)"   # class            npu  ort noopt  d_ort d_noopt  logit(x1e-4) back\n");
	for(i = 0; i < NPU_RT_OUT_CLASSES; i++) {
		d_opt = (INT)logit_q[i] - (INT)aed_test_logits_ort[i];
		d_flt = (INT)logit_q[i] - (INT)aed_test_logits_ort_noopt[i];
		if(d_opt < 0) d_opt = -d_opt;
		if(d_flt < 0) d_flt = -d_flt;
		if(d_opt > max_opt) max_opt = d_opt;
		if(d_flt > max_flt) max_flt = d_flt;
		back = (INT)aed_test_logits_ort[i] - (INT)aed_test_logits_ort_noopt[i];
		if(back < 0) back = -back;
		if(back > max_pc) max_pc = back;

		v    = lf[i] / b_scale;
		back = (INT)(v + ((v >= 0.0f) ? 0.5f : -0.5f)) + (INT)b_zp;
		if(back != (INT)logit_q[i]) back_ok = FALSE;

		tm_printf((UB*)"  %2d %-15s %4d %4d %5d %6d %7d  %12d %4d%s\n", i, aed_test_class_names[i],
				(INT)logit_q[i], (INT)aed_test_logits_ort[i], (INT)aed_test_logits_ort_noopt[i],
				(INT)logit_q[i] - (INT)aed_test_logits_ort[i],
				(INT)logit_q[i] - (INT)aed_test_logits_ort_noopt[i],
				x10k(lf[i]), back, (back == (INT)logit_q[i]) ? "" : " <-");
	}
	tm_printf((UB*)"  max |npu - ort| = %d LSB, max |npu - ort_noopt| = %d LSB"
			" (the two PC references differ by up to %d LSB)\n", max_opt, max_flt, max_pc);
#if AED_LOGIT_F_ADDR != 0
	tm_printf((UB*)"  softmax input float -> int8 matches captured int8: %s\n", back_ok ? "yes" : "NO");
#else
	tm_printf((UB*)"  softmax input float -> int8 check: skipped (integer softmax, no float softmax input in this model)\n");
#endif

	pass_opt = (max_opt <= LOGIT_TOL_LSB);
	pass_flt = (max_flt <= LOGIT_TOL_LSB);
	for(i = 0; i < NPU_RT_OUT_CLASSES && !pass_opt && !pass_flt; i++) {
		d_opt = (INT)logit_q[i] - (INT)aed_test_logits_ort[i];
		d_flt = (INT)logit_q[i] - (INT)aed_test_logits_ort_noopt[i];
		if(d_opt > LOGIT_TOL_LSB || d_opt < -LOGIT_TOL_LSB || d_flt > LOGIT_TOL_LSB || d_flt < -LOGIT_TOL_LSB) {
			tm_printf((UB*)"  outside +-%d LSB: %d %s (ort %+d, noopt %+d)\n", LOGIT_TOL_LSB,
					i, aed_test_class_names[i], d_opt, d_flt);
		}
	}

	pass = (pass_opt || pass_flt) && scale_ok && zp_ok && back_ok;
	tm_printf((UB*)"  logits %s +-%d LSB of a PC reference (reference only)\n",
			pass ? "within" : "NOT within", LOGIT_TOL_LSB);
	return pass;
}
#else
/* このモデルの番地がまだ無い (aed_model_cfg.h の AED_LOGIT_HOOK=0)。参考値なので飛ばすだけ */
LOCAL BOOL logits_check(void)
{
	tm_printf((UB*)"npu logits: skipped (AED_LOGIT_HOOK=0 for model %s; fill AED_LOGIT_* in aed_model_cfg.h"
			" after reading the generated network.c)\n", AED_MODEL_NAME);
	return FALSE;
}
#endif	/* AED_LOGIT_HOOK */

/* ---------------------------------------------------------------- */
/* ESC-10 の実録音 (判定)                                              */
/* ---------------------------------------------------------------- */

#if NPU_HAVE_CLIPS
/*
 * 実録音を順に推論し、1位を PC (ONNX Runtime) の1位と比べる。
 * 合否: 全クリップで NPU の1位が PC のどちらか (最適化あり / なし) の1位と一致すれば PASS。
 * 乱数入力は学習データの分布外で上位2クラスが拮抗し、整数演算の丸めの違いの積み重ねで
 * 確率が大きく動くので、判定には実録音を使う (乱数入力の結果は参考値)
 */
LOCAL BOOL clips_check(void)
{
	float	out[NPU_RT_OUT_CLASSES];
	UW	us;
	INT	k, top, truth, t_ort, t_flt;
	INT	n_ort = 0, n_flt = 0, n_pc = 0, n_npu_truth = 0, n_ort_truth = 0, n_flt_truth = 0, n_run = 0;
	BOOL	same_pc, pass;
	ER	er;

	tm_printf((UB*)"npu clip test: %d ESC-10 recordings, NPU top-1 vs PC top-1 (p in x1e-4)\n", AED_CLIP_COUNT);
	tm_printf((UB*)"   # file               truth           PC top-1 (ort/noopt)    p_pc NPU top-1        p_npu     us\n");
	for(k = 0; k < AED_CLIP_COUNT; k++) {
		truth = aed_clip_truth[k];
		t_ort = aed_clip_top_ort[k];
		t_flt = aed_clip_top_noopt[k];
		if(t_ort == truth) n_ort_truth++;
		if(t_flt == truth) n_flt_truth++;

		er = run_once((const B *)aed_clip_input[k], out, &us, NULL, NULL);
		if(er != E_OK) {
			tm_printf((UB*)"  %2d %-18s %-15s inference failed: er=%d\n", k, aed_clip_file[k],
					aed_test_class_names[truth], er);
			continue;
		}
		n_run++;
		top = argmax(out);
		same_pc = (top == t_ort) || (top == t_flt);
		if(top == t_ort) n_ort++;
		if(top == t_flt) n_flt++;
		if(same_pc) n_pc++;
		if(top == truth) n_npu_truth++;

		tm_printf((UB*)"  %2d %-18s %-15s %-15s%s %5d %-15s %5d %6u %s\n", k, aed_clip_file[k],
				aed_test_class_names[truth], aed_test_class_names[t_ort], (t_flt == t_ort) ? "       " : "/diff  ",
				x10k(aed_clip_prob_ort[k]), aed_test_class_names[top], x10k(out[top]), us,
				same_pc ? "ok" : "MISMATCH");
	}
	tm_printf((UB*)"  NPU top-1 == PC top-1: %d/%d (ort %d, noopt %d), inference completed %d/%d\n",
			n_pc, AED_CLIP_COUNT, n_ort, n_flt, n_run, AED_CLIP_COUNT);
	tm_printf((UB*)"  top-1 == truth: NPU %d/%d, PC ort %d/%d, PC noopt %d/%d\n",
			n_npu_truth, AED_CLIP_COUNT, n_ort_truth, AED_CLIP_COUNT, n_flt_truth, AED_CLIP_COUNT);

	pass = (n_pc == AED_CLIP_COUNT);
	tm_printf((UB*)"npu clip test %s (NPU top-1 must match PC top-1 for every clip)\n", pass ? "PASS" : "FAIL");
	return pass;
}
#endif	/* NPU_HAVE_CLIPS */

/* ---------------------------------------------------------------- */
/* 自己テスト                                                          */
/* ---------------------------------------------------------------- */

EXPORT BOOL npu_selftest(void)
{
	float	out[NPU_RT_OUT_CLASSES];
	B	before[HEAD_BYTES];
	UW	us, in_diff, t_min, t_max, t_sum, n_ok;
	INT	i, top;
	float	d_opt, d_flt;
	BOOL	pass = FALSE, near;
	ER	er;

	tm_printf((UB*)"npu random-input test (reference only, not a pass/fail criterion): seed=%d\n",
			AED_TEST_SEED);
	{
		/* 生成ヘッダがこのモデルのものか (入力の量子化がボードの値と同じか) */
		float	b_scale;
		INT	b_zp;

		if(npu_rt_input_quant(&b_scale, &b_zp)) {
			tm_printf((UB*)"  input quant: board scale=%u e-9 zp=%d, PC header scale=%u e-9 zp=%d -> %s\n",
					(UW)(b_scale * 1e9f + 0.5f), b_zp, (UW)(AED_TEST_SCALE * 1e9f + 0.5f), AED_TEST_ZP,
					(b_scale == AED_TEST_SCALE && b_zp == AED_TEST_ZP) ? "same"
					: "DIFFERENT (is the header for another model?)");
		}
	}

	/* 乱数入力で1回推論して PC と比べる (参考値) */
	er = run_once(aed_test_input, out, &us, before, &in_diff);
	tm_printf((UB*)"  input in RAM before inference: %u of %d bytes differ from aed_test_input\n",
			in_diff, NPU_RT_IN_BYTES);
	if(er != E_OK) {
		tm_printf((UB*)"  inference failed: er=%d after %u us\n", er, us);
		show_input_heads(aed_test_input, before);
		tm_printf((UB*)"npu selftest FAIL (inference did not complete)\n");
		return FALSE;
	}
	memcpy(ref_out, out, sizeof(ref_out));
	ref_valid = TRUE;

	tm_printf((UB*)"  inference: %u us\n", us);
	tm_printf((UB*)"   # class              npu    ort  ort_noopt  (x1e-4)\n");
	for(i = 0; i < NPU_RT_OUT_CLASSES; i++) {
		tm_printf((UB*)"  %2d %-15s %6d %6d %6d\n", i, aed_test_class_names[i],
				x10k(out[i]), x10k(aed_test_expect_ort[i]), x10k(aed_test_expect_ort_float[i]));
	}
	top   = argmax(out);
	d_opt = max_abs_diff(out, aed_test_expect_ort);
	d_flt = max_abs_diff(out, aed_test_expect_ort_float);
	tm_printf((UB*)"  top1: %d %s (%d x1e-4)\n", top, aed_test_class_names[top], x10k(out[top]));
	tm_printf((UB*)"  max |npu - ort| = %d, max |npu - ort_noopt| = %d (x1e-4)\n", x10k(d_opt), x10k(d_flt));

	/* 大きく外れたら、キャッシュの取り違えを確かめられるよう入力バッファの先頭を出す */
	near = (strcmp(aed_test_class_names[top], ST_EXPECT_TOP) == 0)
		&& ((d_opt <= ST_TOLERANCE) || (d_flt <= ST_TOLERANCE));
	if(!near) show_input_heads(aed_test_input, before);
	tm_printf((UB*)"  random input: top1 %s PC, output %s %d x1e-4 of a PC reference (reference only)\n",
			(strcmp(aed_test_class_names[top], ST_EXPECT_TOP) == 0) ? "matches" : "differs from",
			((d_opt <= ST_TOLERANCE) || (d_flt <= ST_TOLERANCE)) ? "within" : "NOT within",
			x10k(ST_TOLERANCE));

	/* softmax 直前の int8 ロジットを PC と比べる (推論をもう1回。参考値) */
	(void)logits_check();

	/* 判定: ESC-10 の実録音で1位を PC と比べる。結果は区切りで囲む (計測の目印) */
	log_block_begin("NPU SELFTEST");
#if NPU_HAVE_CLIPS
	pass = clips_check();
	tm_printf((UB*)"npu selftest %s (judged by the clip test)\n", pass ? "PASS" : "FAIL");
#elif AED_USE_TEST_CLIPS
	(void)pass;
	tm_printf((UB*)"npu clip test: SKIP (aed_test_clips.h not found: run scripts/aed_clips.py)\n");
	tm_printf((UB*)"npu selftest NOT JUDGED\n");
#else
	(void)pass;
	tm_printf((UB*)"npu clip test: SKIP (AED_USE_TEST_CLIPS=0 to save code space;"
			" the preproc test judges 2 clips instead)\n");
	tm_printf((UB*)"npu selftest NOT JUDGED\n");
#endif
	log_block_end();

	/* 同じ入力で連続実行 */
	tm_printf((UB*)"npu repeat x%d (same input):\n", ST_REPEAT);
	t_min = 0xFFFFFFFFU;
	t_max = 0;
	t_sum = 0;
	n_ok  = 0;
	for(i = 0; i < ST_REPEAT; i++) {
		er = run_once(aed_test_input, out, &us, NULL, NULL);
		if(er != E_OK) {
			tm_printf((UB*)"  [%2d] er=%d after %u us\n", i + 1, er, us);
			continue;
		}
		tm_printf((UB*)"  [%2d] %6u us, max diff vs first = %d x1e-4%s\n", i + 1, us,
				x10k(max_abs_diff(out, ref_out)),
				(memcmp(out, ref_out, sizeof(out)) == 0) ? " (bit-identical)" : "");
		if(us < t_min) t_min = us;
		if(us > t_max) t_max = us;
		t_sum += us;
		n_ok++;
	}
	if(n_ok > 0) {
		tm_printf((UB*)"  inference time: min=%u mean=%u max=%u us (%u of %d runs OK)\n",
				t_min, t_sum / n_ok, t_max, n_ok, ST_REPEAT);
	} else {
		tm_printf((UB*)"  no run completed\n");
	}
	return TRUE;
}

/* ---------------------------------------------------------------- */
/* パススルー中の推論の予行 (推論タスクから窓ごとに呼ぶ)                */
/* ---------------------------------------------------------------- */

EXPORT ER npu_selftest_run_fixed(UW *us, INT *diff_x1e4, BOOL *same)
{
	float	out[NPU_RT_OUT_CLASSES];
	ER	er;

	*us        = 0;
	*diff_x1e4 = -1;
	*same      = FALSE;
	if(!ref_valid) return E_OBJ;

	er = run_once(aed_test_input, out, us, NULL, NULL);
	if(er != E_OK) return er;

	*diff_x1e4 = x10k(max_abs_diff(out, ref_out));
	*same      = (memcmp(out, ref_out, sizeof(out)) == 0);
	return E_OK;
}
