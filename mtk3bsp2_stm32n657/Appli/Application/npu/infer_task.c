#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <string.h>		// memcpy, memcmp
#include <math.h>		// sqrtf, log10f
#include "infer_task.h"
#include "npu_rt.h"
#include "npu_selftest.h"
#include "aed_model.h"		// AED_MODEL、AED_MODEL_REF_CLIPS_H
#include "../aed/preproc.h"
#include "../aed/notify.h"
#include "../lcd/lcd_task.h"	// lcd_post_stats() (集計行の disp)
#include "../audio/tap_ring.h"
#include "../audio/audio_task.h"
#include "../trace/trace.h"	// trace_muted(), trace_busy(), trace_cyc_per_us()
#include "../trace/log.h"	// log_printf() (出力はレポータ経由。tm_printf を直接呼ばない)

/*
 * 1: 起動後のパススルー中に、ボードの前処理 (Application/aed/preproc.c) の結果を PC と
 * 突き合わせる (Phase 1 タスク8)。0 で無効。
 * 実録音2本の生 PCM と PC のテンソルでコード領域が 74,688B 増えるので、
 * npu_selftest.c の AED_USE_TEST_CLIPS (30 本、184,320B) とは同時に載せられない
 */
#define PREPROC_TEST		(1)

/*
 * 前処理を PC と突き合わせるための実録音2本 (生 PCM + PC の int8 テンソル + PC の1位。
 * scripts/aed_clips.py の生成物)。ESC-50 由来のデータなのでコミットしない (.gitignore)。
 * 無ければ前処理セルフテストだけを飛ばしてビルドは通す
 */
#if PREPROC_TEST && __has_include(AED_MODEL_REF_CLIPS_H)
#include AED_MODEL_REF_CLIPS_H
#define HAVE_REF_CLIPS		(1)
#else
#define HAVE_REF_CLIPS		(0)
#endif

/*
 * 推論タスク (タスク7 で骨組み、タスク8 で前処理、タスク9 で本番の 前処理→推論→通知)
 *
 * ll_aton (npu_rt.c) を呼ぶのはこのタスクだけにする。
 *   - ll_aton はスレッドセーフでない (OSAL は BARE_METAL でロックが無い)
 *   - 推論はスタックを 1KB 以上使う。.su の値で、ll_sw_forward_softmax 360B、
 *     LL_ATON_End_EpochBlock_30 200B、ランタイムの各段 24〜40B に、ST のライブラリ
 *     (NetworkRuntime、.su 無し) の分が加わる。usermain は μT-Kernel の初期タスクで、
 *     スタックが 1KB (INITTASK_STKSZ、mtkernel/include/sys/inittask.h) しかなく、
 *     溢れても検出されない (USE_SPMON 無効)。npu_rt_init も同じ理由でこのタスクで呼ぶ
 *
 * 流れ:
 *   1. preproc_init() で前処理の表を作り、npu_rt_init() と自己テスト (npu_selftest.c)。
 *      終わったら usermain に知らせる
 *      (usermain は推論時間を音声の負荷なしで測るため、音声を始める前にこれを待つ)
 *   2. タップリングから窓を取り出す。書き手 (task_pcm) が窓をそろえたときにセマフォで起きる
 *      - 窓を静的バッファ win_buf へコピー
 *      - preproc_run で log-mel にして npu_rt_infer で推論し、notify_decide の判定を
 *        notify_window で通知する (JSON 1行と赤 LED)。通知は診断の表示より先に行う
 *      - 音量 (RMS・ピーク) と、前の窓の末尾 240 サンプルとの一致を確かめて1行表示
 *   3. PREPROC_TEST が 1 なら、パススルー稼働中に1回だけ前処理セルフテストを行う。
 *      実録音2本 (aed_ref_clips.h の生 PCM) をボードで log-mel にして、PC が同じ音から
 *      作った int8 テンソルと 6144 要素すべてを比べ、そのテンソルで推論して1位を PC と並べる。
 *      前処理の差と NPU の差を切り分けるため、PC のテンソルでの推論も行う
 *   4. NPU_PT_TEST が 1 なら、パススルー稼働中の最初の 10 窓で乱数入力の推論を1回ずつ行い、
 *      前後で音声の under / over / late が増えないかを見る (推論を載せたときの予行)。
 *      タスク6 の npu pt test (同じ優先度の別ループで 960ms ごとに推論) をここへ統合した。
 *      間隔はタップリングの窓 (15360 サンプル = 約 960ms) で決まる
 *
 * 優先度 15 は音声 (task_pcm 5、task_audio・task_1 10) より低く、reporter (20) より高い。
 * POLLING の推論中はこのタスクが CPU を回し続けるので、推論のあいだ reporter と dump は動けない
 * (出したい行はメッセージバッファに溜まり、推論が終わってから出る)。
 * 表示はすべて log_printf で、UART に書くのは reporter だけ (Application/trace/log.h)。
 */

/*
 * 1: パススルー中に乱数入力の推論を10回行う (タスク7 の予行)。
 * タスク9 で窓ごとの本番の推論が入ったので既定は 0。予行だけをもう一度見るときに 1 にする
 * (本番の推論と同じ窓で2回推論することになる)
 */
#define NPU_PT_TEST		(0)

#define INFER_TASK_STKSZ	(8 * 1024)
#define START_WAIT_MS		(30000)		/* usermain が自己テストの終わりを待つ上限 */

#define WIN_WAIT_MS		(2000)		/* 窓は約 960ms ごと。これだけ来なければ音声が止まったとみなす */
#define SUMMARY_EVERY		(10)		/* 累計を出す間隔 (窓の数) */
#define PT_REPEAT		(10)		/* 予行の回数 */

#define LVL_BAR_LEN		(20)		/* 音量のバーの長さ */
#define LVL_BAR_DB_MIN		(-60)		/* バーの左端 (dBFS)。1文字 3dB */

LOCAL void task_infer(INT stacd, void *exinf);
LOCAL ID	tskid_infer;
LOCAL T_CTSK	ctsk_infer = {
	.itskpri	= INFER_TASK_PRI,
	.stksz		= INFER_TASK_STKSZ,
	.task		= task_infer,
	.tskatr		= TA_HLNG | TA_RNG3,
};

/*
 * 自己テストが終わったことを usermain に知らせる。tk_wup_tsk を使わないのは、usermain が
 * 待ちをタイムアウトした後に起床要求だけが残ると、usermain 最後の tk_slp_tsk(TMO_FEVR) が
 * すぐ戻って usermain が終わってしまうため
 */
LOCAL ID	semid_done;
LOCAL T_CSEM	csem_done = {
	.sematr		= TA_TFIFO | TA_FIRST,
	.isemcnt	= 0,
	.maxsem		= 1,
};

/* 取り出した窓と、つなぎ目の確認用の前の窓の末尾 (静的領域。タスクのスタックに置かない) */
LOCAL H		win_buf[TAP_WIN_LEN];
LOCAL H		prev_tail[TAP_WIN_OVERLAP];
LOCAL BOOL	have_prev = FALSE;
LOCAL UW	prev_pos;
LOCAL UW	seam_ok_n, seam_ng_n;

/* NPU に渡す入力 (log-mel の結果)。静的領域。前処理セルフテストでも使う */
LOCAL B		in_tensor[AED_PREPROC_OUT_LEN] __attribute__((aligned(32)));

/* ---------------------------------------------------------------- */
/* 本番の処理 (窓ごとに 前処理 → 推論 → 判定 → 通知)                    */
/* ---------------------------------------------------------------- */

LOCAL UW	run_n, run_err;			/* 推論できた窓の数 / 失敗した数 */
LOCAL ER	run_last_er;
LOCAL UW	pp_us_max, pp_us_sum, inf_us_max, inf_us_sum;
LOCAL INT	last_cls = AED_CLS_UNKNOWN;	/* 窓の1行に出すための、直前の判定 */
LOCAL INT	last_p100;

#if INFER_SLOW_X > 1
/*
 * 条件 D (infer_task.h の INFER_SLOW_X)。窓 1 つの実測 (前処理+推論+空回し) と空回しの統計を
 * show_summary の 1 行 (このビルドだけ) に出す
 */
LOCAL UW	win_us_max, win_us_sum;			/* 窓 1 つの処理時間 (前処理+推論+空回し) の最大・合計 (us) */
LOCAL UW	spin_n, spin_us_max, spin_us_sum;	/* 空回しの回数・最大・合計 (us) */
LOCAL UW	spin_skipped;				/* READY 前・トレース未完・CYCCNT 無効で飛ばした窓 */
LOCAL UW	slow_spin(UW base_us);			/* 戻り値: 実際に回した時間 (us)。飛ばしたら 0 */
#endif

/*
 * 窓1つを処理する。preproc_run → npu_rt_infer → notify_decide → notify_window。
 * 通知 (JSON と LED) は notify_window が行う。rms・peak は window_level の値 (win 行と同じ)
 */
LOCAL BOOL process_window(const TAP_WIN_INFO *info, UW rms, UW peak, UW *pp_us, UW *inf_us)
{
	float	out[NPU_RT_OUT_CLASSES], p;
	INT	cls;
	ER	er;

	*pp_us  = preproc_run(win_buf, in_tensor);
	er      = npu_rt_infer(in_tensor, out, inf_us);
	if(er != E_OK) {
		run_err++;
		run_last_er = er;
		if(!trace_muted()) {
			log_printf("win %u: inference failed er=%d after %u us\n",
					info->seq, er, *inf_us);
		}
		return FALSE;
	}

	run_n++;
	pp_us_sum  += *pp_us;
	inf_us_sum += *inf_us;
	if(*pp_us > pp_us_max)   pp_us_max  = *pp_us;
	if(*inf_us > inf_us_max) inf_us_max = *inf_us;

#if INFER_SLOW_X > 1
	{
		/* 条件 D: 推論が遅い状態を作る。判定・通知より前なので JSON の lat_ms にこの遅れがそのまま載る */
		UW	spin_us = slow_spin(*pp_us + *inf_us);
		UW	win_us  = *pp_us + *inf_us + spin_us;	/* 窓 1 つの実測 */

		win_us_sum += win_us;
		if(win_us > win_us_max) win_us_max = win_us;
	}
#endif

	/*
	 * 診断の1行にはゲートより前の判定を出す (窓の 1位が何だったかは残したい)。
	 * 通知するかどうか (対象クラスか、ピークと暗騒音からの差が足りるか) は notify_window
	 * (の中の notify_gate) が決める。門で止めた・通した理由は notify_last_note() で窓行に付ける
	 */
	cls       = notify_decide(out, &p);
	last_cls  = cls;
	last_p100 = (INT)(p * 100.0f + 0.5f);
	(void)notify_window(info->seq, cls, p, rms, peak, info->t_ready, info->lag_exact);
	return TRUE;
}

/* ---------------------------------------------------------------- */
/* 窓の確認                                                            */
/* ---------------------------------------------------------------- */

/* 窓全体の RMS・ピーク (int16 の値) と、RMS の dBFS (フルスケール 32768 が 0dB) */
LOCAL void window_level(const H *x, UW *rms, UW *peak, INT *dbfs)
{
	uint64_t	sq = 0;
	UW		pk = 0, a;
	INT		i;
	float		r;

	for(i = 0; i < TAP_WIN_LEN; i++) {
		a = (x[i] < 0) ? (UW)(-(W)x[i]) : (UW)x[i];
		if(a > pk) pk = a;
		sq += (uint64_t)((W)x[i] * (W)x[i]);
	}
	r     = sqrtf((float)sq / (float)TAP_WIN_LEN);
	*rms  = (UW)(r + 0.5f);
	*peak = pk;
	*dbfs = (r >= 1.0f) ? (INT)(20.0f * log10f(r / 32768.0f) - 0.5f) : -99;
}

/* 話しかけたときに伸びるのが一目で分かるバー (LVL_BAR_DB_MIN から 3dB ごとに1文字) */
LOCAL void level_bar(INT dbfs, char *bar)
{
	INT	i, n = (dbfs - LVL_BAR_DB_MIN) / 3;

	if(n < 0) n = 0;
	if(n > LVL_BAR_LEN) n = LVL_BAR_LEN;
	for(i = 0; i < LVL_BAR_LEN; i++) bar[i] = (i < n) ? '#' : '.';
	bar[LVL_BAR_LEN] = '\0';
}

/*
 * 窓の先頭 240 サンプルが、前の窓の末尾 240 サンプルと同じか。前の窓と連続していない
 * (最初の窓、上書きで読み位置を飛ばした直後) ときは確かめない
 */
LOCAL const char *seam_check(const TAP_WIN_INFO *info)
{
	const char	*r;

	if(!have_prev || info->resync || info->pos != prev_pos + TAP_WIN_HOP) {
		r = "-";
	} else if(memcmp(win_buf, prev_tail, sizeof(prev_tail)) == 0) {
		seam_ok_n++;
		r = "ok";
	} else {
		seam_ng_n++;
		r = "NG";
	}
	memcpy(prev_tail, &win_buf[TAP_WIN_HOP], sizeof(prev_tail));
	prev_pos  = info->pos;
	have_prev = TRUE;
	return r;
}

LOCAL UW cyc_to_ns(UW cyc)
{
	return (UW)(((uint64_t)cyc * 1000ULL) / trace_cyc_per_us());
}

/* 累計 (10分の確認で見る値をまとめて出す)。1行が長くなるので分けて出す */
LOCAL void show_summary(const char *why)
{
	TAP_STATS	st;
	UW		under, over, late;
	UW		n_out, n_held, n_offlist, n_gated_abs, n_gated_rel, n_cooldown, lat_max, lat_loose;
	UW		d_sent, d_drop, d_max, d_avg;

	tap_ring_stats(&st);
	audio_pt_counts(&under, &over, &late);
	notify_stats(&n_out, &n_held, &n_offlist, &n_gated_abs, &n_gated_rel, &n_cooldown, &lat_max, &lat_loose);

	log_printf("tap %s: windows=%u (expected %u from %u samples) overrun=%u torn=%u skipped=%u"
			" seam ok=%u NG=%u\n",
			why, st.windows, tap_ring_expected_windows(), st.head, st.overrun, st.torn,
			st.skipped, seam_ok_n, seam_ng_n);
	log_printf("  tap lag max=%uus (%u of them lower bounds) | tap write max=%uns avg=%uns (%u calls)\n",
			st.lag_max_us, st.late, cyc_to_ns(st.wr_max_cyc),
			(st.wr_calls > 0) ? cyc_to_ns(st.wr_sum_cyc / st.wr_calls) : 0, st.wr_calls);
	log_printf("  audio under=%u over=%u late=%u | aed run=%u err=%u (last er=%d)\n",
			under, over, late, run_n, run_err, run_last_er);
	log_printf("  preproc max=%uus avg=%uus | infer max=%uus avg=%uus\n",
			pp_us_max, (run_n > 0) ? pp_us_sum / run_n : 0,
			inf_us_max, (run_n > 0) ? inf_us_sum / run_n : 0);
#if INFER_SLOW_X > 1
	/* 条件 D だけ: 窓 1 つの実測 (前処理+推論+空回し) と空回しの内訳 */
	log_printf("  slow x%d: window max=%uus avg=%uus | spin n=%u max=%uus avg=%uus skipped=%u\n",
			INFER_SLOW_X, win_us_max, (run_n > 0) ? win_us_sum / run_n : 0,
			spin_n, spin_us_max, (spin_n > 0) ? spin_us_sum / spin_n : 0, spin_skipped);
#endif
	/*
	 * 判定の内訳: out=出した行 / held=続いた unknown で出さなかった窓 /
	 * offlist=通知対象外のクラスだった窓 / gated_abs=ピークの門で止めた窓 /
	 * gated_rel=暗騒音からの差の門で止めた窓 / cooldown=同じクラスの再通知を抑えた窓 (notify.h)
	 */
	log_printf("  notify out=%u held=%u offlist=%u gated_abs=%u gated_rel=%u cooldown=%u lat max=%uus (%u lower bounds)\n",
			n_out, n_held, n_offlist, n_gated_abs, n_gated_rel, n_cooldown, lat_max, lat_loose);
	log_printf("  log sent=%u dropped=%u lag max=%uus\n",
			log_sent(), log_dropped(), log_lag_max_us());

	/* 画面: 通知から描き終わりまで (フェーズ3 で「通知遅延 + 画面」と並べて書く) */
	lcd_post_stats(&d_sent, &d_drop, &d_max, &d_avg);
	log_printf("  disp sent=%u dropped=%u max=%uus avg=%uus\n", d_sent, d_drop, d_max, d_avg);
}

/* ---------------------------------------------------------------- */
/* パススルー中の推論の予行                                            */
/* ---------------------------------------------------------------- */

#if NPU_PT_TEST
LOCAL INT	pt_n = 0;			/* 済んだ回数 */
LOCAL BOOL	pt_reported = FALSE;
LOCAL UW	pt_us[PT_REPEAT];
LOCAL ER	pt_er[PT_REPEAT];
LOCAL INT	pt_dif[PT_REPEAT];		/* 最初の推論の出力との差の最大値 (x1e-4) */
LOCAL BOOL	pt_same[PT_REPEAT];
LOCAL UW	pt_u0, pt_o0, pt_l0, pt_u1, pt_o1, pt_l1;

LOCAL void pt_report(void)
{
	UW	t_min = 0xFFFFFFFFU, t_max = 0, t_sum = 0, n_ok = 0;
	INT	k;
	BOOL	pass;

	log_printf("npu pt test: fixed-input inference on %d tap windows during passthrough (task pri %d)\n",
			PT_REPEAT, INFER_TASK_PRI);
	for(k = 0; k < PT_REPEAT; k++) {
		if(pt_er[k] != E_OK) {
			log_printf("  [%2d] er=%d after %u us\n", k + 1, pt_er[k], pt_us[k]);
			continue;
		}
		log_printf("  [%2d] %6u us, max diff vs first = %d x1e-4%s\n", k + 1, pt_us[k], pt_dif[k],
				pt_same[k] ? " (bit-identical)" : "");
		if(pt_us[k] < t_min) t_min = pt_us[k];
		if(pt_us[k] > t_max) t_max = pt_us[k];
		t_sum += pt_us[k];
		n_ok++;
	}
	if(n_ok > 0) {
		log_printf("  inference time: min=%u mean=%u max=%u us (%u of %d runs OK)\n",
				t_min, t_sum / n_ok, t_max, n_ok, PT_REPEAT);
	}
	log_printf("  audio before: under=%u over=%u late=%u / after: under=%u over=%u late=%u\n",
			pt_u0, pt_o0, pt_l0, pt_u1, pt_o1, pt_l1);

	pass = (n_ok == PT_REPEAT) && (pt_u1 == pt_u0) && (pt_o1 == pt_o0) && (pt_l1 == pt_l0);
	for(k = 0; k < PT_REPEAT; k++) {
		if(pt_er[k] == E_OK && pt_dif[k] > NPU_SELFTEST_TOL_X1E4) pass = FALSE;
	}
	log_printf("npu pt test %s (all runs OK, output within %d x1e-4 of first, no new under/over/late)\n",
			pass ? "PASS" : "FAIL", NPU_SELFTEST_TOL_X1E4);
}

/* 窓ごとに1回呼ぶ */
LOCAL void pt_step(BOOL npu_ok)
{
	if(!npu_ok || pt_reported) return;

	if(pt_n < PT_REPEAT) {
		if(!audio_passthrough_active()) return;
		if(pt_n == 0) audio_pt_counts(&pt_u0, &pt_o0, &pt_l0);
		pt_er[pt_n] = npu_selftest_run_fixed(&pt_us[pt_n], &pt_dif[pt_n], &pt_same[pt_n]);
		pt_n++;
		if(pt_n == PT_REPEAT) audio_pt_counts(&pt_u1, &pt_o1, &pt_l1);
		return;
	}

	/*
	 * usermain がパススルーの立ち上がりと同時に10秒のトレースを始め、その後 CSV をダンプする。
	 * ダンプ中の出力は CSV に混ざるので、記録とダンプが終わってから出す
	 */
	if(trace_busy()) return;
	pt_report();
	pt_reported = TRUE;
	log_lag_reset_when_idle();	/* 予行の間に溜まった行の待ちを持ち越さない (pp_step と同じ) */
}
#endif	/* NPU_PT_TEST */

/* ---------------------------------------------------------------- */
/* 前処理セルフテスト (タスク8)                                        */
/* ---------------------------------------------------------------- */

#if HAVE_REF_CLIPS

_Static_assert(AED_REF_SAMPLES == AED_PREPROC_SAMPLES, "ref clip length");
_Static_assert(AED_REF_TENSOR_LEN == AED_PREPROC_OUT_LEN, "ref tensor size");
_Static_assert(AED_REF_TENSOR_LEN == NPU_RT_IN_BYTES, "npu input size");
_Static_assert(AED_REF_CLASSES == NPU_RT_OUT_CLASSES, "class count");
/* 量子化 (AED_REF_SCALE / AED_REF_ZP) はモデルの実行時の値と pp_report が比べる */

/* 差が 1 を超える要素があれば前処理の移植が合っていない (float32 と float64 の差では出ない) */
#define PP_MAX_ABS_DIFF		(1)

LOCAL BOOL	pp_done = FALSE;

/* 浮動小数点は出せないので、1万倍して丸めた整数で出す (npu_selftest.c と同じ) */
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

/*
 * ボードのテンソルと PC のテンソルを1バイトずつ比べる。
 * *n_same 一致数 / *n_diff 不一致数 / *max_abs 最大絶対差 / *n_big 差が 1 を超えた要素数 /
 * *at 最大絶対差だった要素の番号 (無ければ -1)
 */
LOCAL void pp_compare(const B *got, const B *ref, UW *n_same, UW *n_diff,
			INT *max_abs, UW *n_big, INT *at)
{
	INT	i, d;

	*n_same  = 0;
	*n_diff  = 0;
	*max_abs = 0;
	*n_big   = 0;
	*at      = -1;
	for(i = 0; i < AED_PREPROC_OUT_LEN; i++) {
		d = (INT)got[i] - (INT)ref[i];
		if(d == 0) {
			(*n_same)++;
			continue;
		}
		(*n_diff)++;
		if(d < 0) d = -d;
		if(d > *max_abs) {
			*max_abs = d;
			*at      = i;
		}
		if(d > PP_MAX_ABS_DIFF) (*n_big)++;
	}
}

/* クリップ1本。npu_ok が FALSE なら前処理と突き合わせだけ行う */
LOCAL void pp_one(INT k, BOOL npu_ok, UW *n_same, UW *n_diff, INT *max_abs, UW *n_big,
			BOOL *top_ok, BOOL *ref_top_ok)
{
	float	out[NPU_RT_OUT_CLASSES];
	UW	pp_us, us;
	INT	at, top, t_ort, t_flt;
	ER	er;

	t_ort = (INT)aed_ref_top_ort[k];
	t_flt = (INT)aed_ref_top_noopt[k];

	log_printf("  [%d] %s (%s)\n", k, aed_ref_file[k], aed_ref_class_names[aed_ref_truth[k]]);

	/* 生 PCM からボードで log-mel を作る */
	pp_us = preproc_run(aed_ref_pcm[k], in_tensor);
	log_printf("      preproc %u us\n", pp_us);

	/* PC のテンソルと1バイトずつ比べる */
	pp_compare(in_tensor, aed_ref_tensor[k], n_same, n_diff, max_abs, n_big, &at);
	log_printf("      int8 vs PC: same %u diff %u of %d, max |d| %d, |d|>%d: %u\n",
			*n_same, *n_diff, AED_PREPROC_OUT_LEN, *max_abs, PP_MAX_ABS_DIFF, *n_big);
	if(at >= 0) {
		/* 並びは out[col + 96 * mel] (preproc.h)。どのメル・列でずれたか */
		log_printf("      worst at mel %d col %d: board %d, pc %d\n",
				at / AED_PREPROC_COLS, at % AED_PREPROC_COLS,
				(INT)in_tensor[at], (INT)aed_ref_tensor[k][at]);
	}

	*top_ok     = FALSE;
	*ref_top_ok = FALSE;
	if(!npu_ok) {
		log_printf("      inference skipped (NPU not ready)\n");
		return;
	}

	/* ボードのテンソルで推論 */
	er = npu_rt_infer(in_tensor, out, &us);
	if(er != E_OK) {
		log_printf("      board tensor: inference failed er=%d after %u us\n", er, us);
	} else {
		top     = argmax(out);
		*top_ok = (top == t_ort) || (top == t_flt);
		log_printf("      board tensor -> top1 %d %s p=%d x1e-4 (%u us) %s\n",
				top, aed_ref_class_names[top], x10k(out[top]), us,
				*top_ok ? "ok" : "MISMATCH");
	}

	/* PC のテンソルで推論 (前処理の差と NPU の差を切り分ける) */
	er = npu_rt_infer(aed_ref_tensor[k], out, &us);
	if(er != E_OK) {
		log_printf("      pc tensor: inference failed er=%d after %u us\n", er, us);
	} else {
		top         = argmax(out);
		*ref_top_ok = (top == t_ort) || (top == t_flt);
		log_printf("      pc tensor    -> top1 %d %s p=%d x1e-4 (%u us) %s\n",
				top, aed_ref_class_names[top], x10k(out[top]), us,
				*ref_top_ok ? "ok" : "MISMATCH");
	}

	log_printf("      PC reference: ort top1 %d %s p=%d / noopt top1 %d %s p=%d (x1e-4)\n",
			t_ort, aed_ref_class_names[t_ort], x10k(aed_ref_prob_ort[k]),
			t_flt, aed_ref_class_names[t_flt], x10k(aed_ref_prob_noopt[k]));
}

/* 本文は log_block_begin / log_block_end で囲む (呼び出し側の pp_step が囲む) */
LOCAL void pp_report(BOOL npu_ok)
{
	UW	n_same, n_diff, n_big, s_same = 0, s_diff = 0, s_big = 0;
	UW	u0, o0, l0, u1, o1, l1;
	INT	k, max_abs, s_max = 0;
	BOOL	top_ok, ref_top_ok, pass = TRUE;
	INT	n_top = 0, n_ref_top = 0;
	float	q_scale = 0.0f;
	INT	q_zp = 0;

	audio_pt_counts(&u0, &o0, &l0);

	log_printf("preproc test: %d clips, board log-mel (Application/aed/preproc.c) vs PC"
			" (scripts/aed_clips.py)\n", AED_REF_CLIP_COUNT);
	(void)preproc_quant(&q_scale, &q_zp);
	log_printf("  %d x int16 -> int8 1x%dx%d (%d B), scale=%u e-9 zp=%d, mel LUT %u coefs (expected %d)\n",
			AED_PREPROC_SAMPLES, AED_PREPROC_MELS, AED_PREPROC_COLS, AED_PREPROC_OUT_LEN,
			(UW)(q_scale * 1e9f + 0.5f), q_zp, preproc_mel_coefs(), AED_PREPROC_MEL_COEFS);
	if(q_scale != AED_REF_SCALE || q_zp != AED_REF_ZP) {
		/* ボード (stai_network_get_info) と PC (モデルの入力) で量子化が違えばテンソルは合わない */
		log_printf("  [WARN] quantization differs from the PC header: board scale=%u e-9 zp=%d,"
				" header scale=%u e-9 zp=%d (regenerate aed_ref_clips.h for this model)\n",
				(UW)(q_scale * 1e9f + 0.5f), q_zp, (UW)(AED_REF_SCALE * 1e9f + 0.5f), AED_REF_ZP);
		pass = FALSE;
	}
	if(preproc_mel_coefs() != AED_PREPROC_MEL_COEFS) {
		log_printf("  [WARN] mel LUT size differs from the PC / ST tables\n");
		pass = FALSE;
	}

	for(k = 0; k < AED_REF_CLIP_COUNT; k++) {
		pp_one(k, npu_ok, &n_same, &n_diff, &max_abs, &n_big, &top_ok, &ref_top_ok);
		s_same += n_same;
		s_diff += n_diff;
		s_big  += n_big;
		if(max_abs > s_max) s_max = max_abs;
		if(top_ok) n_top++;
		if(ref_top_ok) n_ref_top++;
	}

	log_printf("  totals: same %u diff %u of %d, max |d| %d, |d|>%d: %u\n",
			s_same, s_diff, AED_PREPROC_OUT_LEN * AED_REF_CLIP_COUNT, s_max,
			PP_MAX_ABS_DIFF, s_big);
	log_printf("  top1 == PC: board tensor %d/%d, pc tensor %d/%d\n",
			n_top, AED_REF_CLIP_COUNT, n_ref_top, AED_REF_CLIP_COUNT);

	if(s_big > 0) pass = FALSE;
	if(npu_ok && n_top != AED_REF_CLIP_COUNT) pass = FALSE;
	log_printf("preproc test %s (no |d| > %d, and top1 from the board tensor matches PC%s)\n",
			pass ? "PASS" : "FAIL", PP_MAX_ABS_DIFF,
			npu_ok ? "" : " [inference not run]");

	audio_pt_counts(&u1, &o1, &l1);
	log_printf("  audio before: under=%u over=%u late=%u / after: under=%u over=%u late=%u\n",
			u0, o0, l0, u1, o1, l1);
}

/* 窓ごとに1回呼ぶ。パススルーが動いていて、トレースと予行が済んでから1回だけ行う */
LOCAL void pp_step(BOOL npu_ok)
{
	if(pp_done) return;
	if(!audio_passthrough_active()) return;
	if(trace_busy()) return;		/* 記録中・ダンプ中の出力は CSV に混ざる */
#if NPU_PT_TEST
	/*
	 * 予行の推論時間に前処理の負荷を混ぜないよう、予行の報告を待つ。
	 * NPU が使えないときは予行が始まらない (pt_reported が立たない) ので待たない
	 * (前処理と突き合わせだけは NPU 無しでもできる)
	 */
	if(npu_ok && !pt_reported) return;
#endif

	log_block_begin("PREPROC TEST");
	pp_report(npu_ok);
	log_block_end();
	pp_done = TRUE;

	/*
	 * セルフテストのあいだ (前処理2本 + 推論4回で 300ms ほど) この優先度15のタスクが
	 * CPU を離さないので、その間に溜まった行の待ちで loglag の最大値が立つ。
	 * トレースのダンプと同じように、出し切ったところで測り直す
	 */
	log_lag_reset_when_idle();
}

#endif	/* HAVE_REF_CLIPS */

/* ---------------------------------------------------------------- */
/* 起動確認の終わりの区切り                                            */
/* ---------------------------------------------------------------- */

/*
 * 起動時の確認 (トレースの記録と CSV ダンプ、前処理セルフテスト、予行) がすべて終わって、
 * 窓ごとの本番の処理だけが続く状態になったことを1回だけ知らせる。
 * ログを後から読むとき、ここより後の win 行と JSON だけを見ればよいと分かるように
 */
LOCAL BOOL ready_shown = FALSE;

LOCAL void show_ready_banner(BOOL npu_ok)
{
	(void)npu_ok;

	if(ready_shown) return;
	if(!audio_passthrough_active()) return;
	if(trace_busy()) return;		/* トレースの記録か CSV ダンプがまだ動いている */
#if HAVE_REF_CLIPS
	if(!pp_done) return;			/* 前処理セルフテストがまだ */
#endif
#if NPU_PT_TEST
	if(npu_ok && !pt_reported) return;	/* 予行がまだ (NPU が無いときは待たない) */
#endif

	/* 本文が無いので log_block_end は呼ばない (見出しだけのブロック) */
	log_block_begin("READY  起動確認おわり。ここから本番");
	ready_shown = TRUE;
}

#if INFER_SLOW_X > 1
/*
 * 空回し (条件 D)。窓 1 つの処理が (前処理+推論)×INFER_SLOW_X になるように base_us×(X-1) だけ DWT で回す。
 *   - READY の後 (ready_shown) から。起動確認 (トレースの CSV ダンプ・前処理セルフテスト) を
 *     遅らせないため。READY はダンプ待ち (TRACE_FULL) の隙間にも出得る (trace_busy は FULL で
 *     FALSE) ので trace_state() が IDLE になるまでも待つ。dump は優先度 32 で、空回しが始まると動けない
 *   - CYCCNT が動いていなければ飛ばす (while から抜けられなくなる)
 *   - 1 回の上限は SLOW_SPIN_MAX_US。UW の差分は 2^32 サイクル (600MHz で約 7.16 秒) で折り返す
 *   - 中で tk_dly_tsk 等の待ちは入れない (CPU を離さないのが目的)。DI/EI も掛けない
 *     (音声の ISR と周期ハンドラは動き続ける)
 */
#define SLOW_SPIN_MAX_US	(3000000)	/* 3 秒。2^31 サイクル (約 3.58 秒) より下 */

LOCAL UW slow_spin(UW base_us)
{
	UW	t0, want_us, cyc, us;

	if(!ready_shown || trace_state() != TRACE_IDLE || !trace_cyccnt_valid()) {
		spin_skipped++;
		return 0;
	}
	want_us = base_us * (UW)(INFER_SLOW_X - 1);
	if(want_us > SLOW_SPIN_MAX_US) want_us = SLOW_SPIN_MAX_US;
	cyc = want_us * trace_cyc_per_us();

	t0 = NOW();
	while((UW)(NOW() - t0) < cyc) {
		/* DWT を読むだけ */
	}
	us = trace_cyc_to_us((UW)(NOW() - t0));
	spin_n++;
	spin_us_sum += us;
	if(us > spin_us_max) spin_us_max = us;
	return us;
}
#endif	/* INFER_SLOW_X > 1 */

/* ---------------------------------------------------------------- */

LOCAL void task_infer(INT stacd, void *exinf)
{
	TAP_WIN_INFO	info;
	UW		rms, peak, pp_us, inf_us;
	INT		dbfs;
	char		bar[LVL_BAR_LEN + 1];
	const char	*seam;
	BOOL		npu_ok = FALSE, idle = FALSE, ready, have_window = FALSE, done;
	ER		er;

	/*
	 * ここの表示はまだレポータが無いので直接 UART に出る (log.h)。
	 * 音声が動き出してからの出力はすべてレポータ経由になる
	 */

	/* 前処理の表 (窓・ツイドル・メルフィルタ)。double を使うのでこのタスクで作る */
	er = preproc_init();
	log_printf("preproc_init: ret=%d (mel LUT %u coefs, expected %d)\n",
			er, preproc_mel_coefs(), AED_PREPROC_MEL_COEFS);
#if !PREPROC_TEST
	log_printf("preproc test: SKIP (PREPROC_TEST=0)\n");
#elif !HAVE_REF_CLIPS
	log_printf("preproc test: SKIP (aed_ref_clips.h not found: run scripts/aed_clips.py)\n");
#endif

	er = npu_rt_init();
	log_printf("npu_rt_init: ret=%d\n", er);

	/*
	 * 前処理の量子化 (scale / zero_point) はモデルの入力の値 (stai_network_get_info)。
	 * NPU が使えないときも、前処理の突き合わせだけはできるよう PC のヘッダの値を使う
	 */
	{
		float	q_scale;
		INT	q_zp;

		if(er == E_OK && npu_rt_input_quant(&q_scale, &q_zp)) {
			preproc_set_quant(q_scale, q_zp);
		}
#if HAVE_REF_CLIPS
		else {
			preproc_set_quant(AED_REF_SCALE, AED_REF_ZP);
			log_printf("preproc quant: NPU not ready, using the PC header (scale=%u e-9 zp=%d)\n",
					(UW)(AED_REF_SCALE * 1e9f + 0.5f), AED_REF_ZP);
		}
#endif
	}

	if(er == E_OK) npu_ok = npu_selftest();
	(void)tk_sig_sem(semid_done, 1);

	ready = npu_ok && preproc_ready();
	if(!ready) {
		log_printf("aed: window processing disabled (npu_ok=%d preproc=%d)\n",
				npu_ok ? 1 : 0, preproc_ready() ? 1 : 0);
	}

	for(;;) {
		er = tap_ring_get_window(win_buf, WIN_WAIT_MS, &info);
		if(er == E_TMOUT) {
			/*
			 * 音声が止まった (パススルーの規定時間が過ぎた等)。
			 * 1つも窓を受け取っていないうちは見張らない (起動直後は、ビープと
			 * プリフィルの間にサンプルだけが溜まって窓がまだそろわないので、
			 * head > 0 だけを見ると偽の final が出る)
			 */
			if(!idle && have_window && !trace_muted()) {
				log_block_begin("FINAL");
				log_printf("tap: no window for %d ms (audio stopped?)\n", WIN_WAIT_MS);
				show_summary("final");
				log_block_end();
				idle = TRUE;
			}
			continue;
		}
		if(er != E_OK) {
			/* 上書きされていた (E_OBJ) / コピー中に上書きされた (E_IO)。読み位置は最新の窓へ進んでいる */
			if(!trace_muted()) {
				log_printf("tap: window lost (%s), jumped to the newest window\n",
						(er == E_OBJ) ? "overwritten before read" : "overwritten while copying");
			}
			continue;
		}
		idle        = FALSE;
		have_window = TRUE;

		window_level(win_buf, &rms, &peak, &dbfs);
		seam = seam_check(&info);

		/*
		 * 本番: 前処理 → 推論 → 判定 → 通知 (JSON と LED)。
		 * 下の窓の1行より先に行う。通知を先にレポータへ渡すことで、診断の行が
		 * UART を先に使って JSON が遅れるのを避ける (遅れは JSON の lat_ms と
		 * reporter の loglag= で測っている)
		 */
		pp_us  = 0;
		inf_us = 0;
		done   = ready ? process_window(&info, rms, peak, &pp_us, &inf_us) : FALSE;

#if NPU_PT_TEST
		pt_step(npu_ok);
#endif
#if HAVE_REF_CLIPS
		pp_step(npu_ok);
#endif

		if(!trace_muted()) {
			/* 起動確認が終わったところで区切りを出す (win 行が再開する前) */
			show_ready_banner(npu_ok);

			level_bar(dbfs, bar);
			log_printf("win %4u pos=%8u %4ddBFS [%s] rms=%5u peak=%5u seam=%s lag=%s%uus\n",
					info.seq, info.pos, dbfs, bar, rms, peak, seam,
					info.lag_exact ? "" : ">=", info.lag_us);
			if(done) {
				/*
				 * 門の結果を付ける (notify.h の notify_last_note): 通した窓は "(floor -42.6 Δ+17.0)"、
				 * 止めた窓は "(gated abs)" / "(gated rel floor -42.6 Δ+6.2)"。unknown と対象外は無印
				 */
				log_printf("  -> %-15s p=%d.%02d%s  preproc=%uus infer=%uus\n",
						notify_class_name(last_cls), last_p100 / 100,
						last_p100 % 100, notify_last_note(),
						pp_us, inf_us);
			}
			if(((info.seq + 1U) % SUMMARY_EVERY) == 0) {
				log_block_begin("TAP TOTAL win=%u", info.seq);
				show_summary("total");
				log_block_end();
			}
		}
	}
}

EXPORT ER infer_task_start(void)
{
	ER	er;

	semid_done = tk_cre_sem(&csem_done);
	if(semid_done < E_OK) return semid_done;

	tskid_infer = tk_cre_tsk(&ctsk_infer);
	if(tskid_infer < E_OK) return tskid_infer;

	er = tk_sta_tsk(tskid_infer, 0);
	if(er < E_OK) return er;

	return tk_wai_sem(semid_done, 1, START_WAIT_MS);
}
