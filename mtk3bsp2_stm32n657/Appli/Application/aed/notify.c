#include <tk/tkernel.h>
#include <string.h>		// strcmp
#include <math.h>		// powf (音量の門のしきい値を作るときに1回だけ)
#include "main.h"		// HAL_GPIO_WritePin, LED_RED_*
#include "notify.h"
#include "../audio/audio_task.h"	// audio_pt_counts()
#include "../lcd/lcd_task.h"	// lcd_post() (画面に出す。待たない)
#include "../trace/log.h"	// log_printf()
#include "../trace/trace.h"	// NOW(), trace_cyc_to_us()

/*
 * 判定と通知。形式と規則は notify.h。
 *
 * クラスの並びはモデルの出力の順 (ST の CTRL_X_CUBE_AI_MODEL_CLASS_LIST。
 * GenHeader/gen_h_file.py がクラス名をソートして作ったもの)。
 * 検証用のヘッダ (aed_test_input.h / aed_ref_clips.h) の並びと同じで、
 * infer_task.c の前処理セルフテストがそれを確かめている。
 *
 * LED: 検出したら LED_RED (PG10) を点け、NOTIFY_LED_MS 後にアラームハンドラで消す。
 * 続けて検出したらアラームを張り直すので、鳴り続けている間は点いたままになる。
 * LED_GREEN (PO1) は usermain の task_1 が点滅させている (生存表示) ので触らない。
 */

LOCAL const char *const class_name[AED_CLASSES] = {
	"chainsaw", "clock_tick", "crackling_fire", "crying_baby", "dog",
	"helicopter", "rain", "rooster", "sea_waves", "sneezing"
};

/* 通知するクラス (notify.h)。番号と名前の対応は notify_init が確かめる */
LOCAL const INT		target_cls[]  = NOTIFY_CLASSES;
LOCAL const char *const	target_name[] = NOTIFY_CLASS_NAMES;

#define N_TARGETS	((INT)(sizeof(target_cls) / sizeof(target_cls[0])))

_Static_assert(sizeof(target_cls) / sizeof(target_cls[0])
		== sizeof(target_name) / sizeof(target_name[0]),
		"NOTIFY_CLASSES と NOTIFY_CLASS_NAMES の数が違う");

LOCAL INT	prev_cls = AED_CLS_UNKNOWN;	/* 前の窓の判定 (unknown の連続を抑えるため) */
LOCAL BOOL	first = TRUE;			/* 最初の窓は unknown でも1回出す */
LOCAL UW	n_emitted, n_held, n_offlist, n_gated, lat_max_us, n_loose;

/* 音量の門のしきい値 (int16 の振幅)。0 なら門なし。notify_init が dBFS から作る */
LOCAL UW	gate_amp;

/* 通知するクラスか */
LOCAL BOOL is_target(INT cls)
{
	INT	i;

	for(i = 0; i < N_TARGETS; i++) {
		if(target_cls[i] == cls) return TRUE;
	}
	return FALSE;
}

/* ---------------------------------------------------------------- */
/* LED                                                               */
/* ---------------------------------------------------------------- */

LOCAL ID	almid = 0;

LOCAL void led_off(void)
{
	HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET);
}

/* アラームハンドラ (タスク独立部)。レジスタを1つ書くだけ */
LOCAL void led_almhdr(void *exinf)
{
	led_off();
}

LOCAL T_CALM	calm_led = {
	.almatr		= TA_HLNG,
	.almhdr		= (FP)led_almhdr,
};

LOCAL void led_on_for_a_while(void)
{
	HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
	/* 起動中のアラームに対する tk_sta_alm は、前の指定を取り消して測り直す */
	if(almid > 0) (void)tk_sta_alm(almid, NOTIFY_LED_MS);
}

/* ---------------------------------------------------------------- */

EXPORT ER notify_init(void)
{
	ID	id;
	INT	i, cls;
	BOOL	bad = FALSE;

	led_off();

	/*
	 * NOTIFY_CLASSES の番号が、いまのモデルの出力順で狙ったクラスを指しているか。
	 * ずれていても LED と JSON は動くので、報告だけして続ける (呼び出し側が表示する)
	 */
	for(i = 0; i < N_TARGETS; i++) {
		cls = target_cls[i];
		if(cls < 0 || cls >= AED_CLASSES || strcmp(class_name[cls], target_name[i]) != 0) {
			log_printf("notify: [WARN] NOTIFY_CLASSES[%d]=%d is \"%s\", expected \"%s\"\n",
					i, cls, notify_class_name(cls), target_name[i]);
			bad = TRUE;
		}
	}
	/*
	 * 音量の門。毎窓 log を取らずに済むよう、dBFS を int16 の振幅に直して持つ。
	 * -99dBFS は「切」の意味なので、振幅を 0 にして比較そのものを飛ばす
	 */
	gate_amp = (NOTIFY_GATE_PEAK_DBFS <= -99) ? 0U
			: (UW)(32768.0f * powf(10.0f, (float)NOTIFY_GATE_PEAK_DBFS / 20.0f) + 0.5f);

	/*
	 * 判定の規則を起動ログに 1 行で残す (ログを読むとき、どの値で走ったか分かるように):
	 *   notify: 3 target classes (dog crying_baby sneezing), threshold p>0.70, peak gate -30 dBFS (peak >= 1036 of 32768)
	 * クラス名は数が変わっても全部出す (log_printf の書式は可変長にできないので、
	 * ここで空白区切りの 1 つの文字列にする)
	 */
	{
		char	names[64];
		INT	n = 0, len;
		INT	thr100 = (INT)(AED_OOD_THR * 100.0f + 0.5f);

		for(i = 0; i < N_TARGETS; i++) {
			len = (INT)strlen(target_name[i]);
			if(n + (n > 0) + len >= (INT)sizeof(names)) break;
			if(n > 0) names[n++] = ' ';
			memcpy(&names[n], target_name[i], (size_t)len);
			n += len;
		}
		names[n] = '\0';
		if(gate_amp == 0) {
			log_printf("notify: %d target classes (%s), threshold p>%d.%02d, peak gate off\n",
					N_TARGETS, names, thr100 / 100, thr100 % 100);
		} else {
			log_printf("notify: %d target classes (%s), threshold p>%d.%02d, peak gate %d dBFS (peak >= %u of 32768)\n",
					N_TARGETS, names, thr100 / 100, thr100 % 100,
					NOTIFY_GATE_PEAK_DBFS, gate_amp);
		}
	}

	id = tk_cre_alm(&calm_led);
	if(id < E_OK) return (ER)id;
	almid = id;
	return bad ? E_OBJ : E_OK;
}

EXPORT INT notify_decide(const float *out, float *p)
{
	INT	i, top = 0;

	for(i = 1; i < AED_CLASSES; i++) {
		if(out[i] > out[top]) top = i;
	}
	*p = out[top];

	/* ST と同じ向きの比較: 閾値を超えたときだけクラスを名乗る */
	return (out[top] > AED_OOD_THR) ? top : AED_CLS_UNKNOWN;
}

EXPORT const char *notify_class_name(INT cls)
{
	if(cls == AED_CLS_UNKNOWN) return "unknown";
	if(cls < 0 || cls >= AED_CLASSES) return "?";
	return class_name[cls];
}

EXPORT BOOL notify_window(UW win, INT cls, float p, UW peak, UW t_ready, BOOL lat_exact)
{
	UW	under, over, late, us, lat_ms;
	INT	p100;
	BOOL	gated = FALSE;

	/*
	 * 音量の門: ピークが足りない窓は unknown と同じ扱いにする (門が無効なら何もしない)。
	 * ここで unknown に落としてから下の状態の判定に入るので、静かな時間に出るのは
	 * unknown の1行だけになる。止めたことは戻り値で返し、窓の診断行に印が付く
	 */
	if(cls != AED_CLS_UNKNOWN && gate_amp > 0 && peak < gate_amp) {
		n_gated++;
		gated = TRUE;
		cls   = AED_CLS_UNKNOWN;
	}

	/*
	 * 出すかどうか。
	 *   通知対象のクラス   → 毎窓 JSON を出して LED を点ける
	 *   通知対象外のクラス → 何も出さない (数えるだけ)。unknown と同じ「通知しない状態」に
	 *                        しておき、次に unknown になっても行が増えないようにする
	 *   unknown            → 状態が変わったときだけ1行出す
	 */
	if(cls != AED_CLS_UNKNOWN && !is_target(cls)) {
		n_offlist++;
		prev_cls = AED_CLS_UNKNOWN;
		return gated;
	}
	if(cls == AED_CLS_UNKNOWN) {
		if(!first && prev_cls == AED_CLS_UNKNOWN) {
			/* 状態が変わっていない。出さない */
			n_held++;
			return gated;
		}
	} else {
		led_on_for_a_while();
		/*
		 * 画面にも出す (タスク2-2)。判定はここまでで決まっていて、この呼び出しは
		 * 渡すだけ。表示タスクは優先度25 なので待たない (TMO_POL で、いっぱいなら
		 * 捨てて数える。JSON と同じ流儀)。通知の時刻を渡し、表示までにかかった
		 * 時間は表示タスク側で測る
		 */
		(void)lcd_post(cls, (INT)(p * 100.0f + 0.5f), win, NOW());
	}
	prev_cls = cls;
	first    = FALSE;

	audio_pt_counts(&under, &over, &late);

	/* 確率は小数2桁。tm_printf も log_printf も浮動小数点は出せないので整数2つで組む */
	p100 = (INT)(p * 100.0f + 0.5f);
	if(p100 < 0)   p100 = 0;
	if(p100 > 100) p100 = 100;

	/*
	 * 遅れは行をレポータに渡す直前に測る。UART に出るまでの待ちは含まない
	 * (その待ちは log_lag_max_us() として実効レートの行に出している)
	 */
	us     = trace_cyc_to_us((UW)(NOW() - t_ready));
	lat_ms = (us + 500U) / 1000U;
	if(us > lat_max_us) lat_max_us = us;
	if(!lat_exact) n_loose++;

	log_printf("{\"win\":%u,\"cls\":\"%s\",\"p\":%d.%02d,\"lat_ms\":%u,"
			"\"under\":%u,\"over\":%u,\"late\":%u}\n",
			win, notify_class_name(cls), p100 / 100, p100 % 100, lat_ms,
			under, over, late);
	n_emitted++;
	return gated;
}

EXPORT void notify_stats(UW *emitted, UW *held, UW *offlist, UW *gated,
			UW *lat_max, UW *lat_loose)
{
	*emitted   = n_emitted;
	*held      = n_held;
	*offlist   = n_offlist;
	*gated     = n_gated;
	*lat_max   = lat_max_us;
	*lat_loose = n_loose;
}
