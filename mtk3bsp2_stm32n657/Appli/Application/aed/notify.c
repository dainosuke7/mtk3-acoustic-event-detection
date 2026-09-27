#include <tk/tkernel.h>
#include <string.h>		// strcmp, memcpy
#include <math.h>		// powf (絶対の門の振幅を作るときに1回)、log10f・roundf (窓ごと)
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
 * 門は notify_gate (純粋関数) にまとめ、状態 (floor の履歴・通知の状態・統計) は notify_window が持つ。
 * PC の再生 (scripts/notify_replay.py) は notify_gate と notify_floor と同じ計算をする。
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

/* floor にする順位 (昇順で 0 始まり)。60 窓の 10 パーセンタイル → 6 番目 = [5] */
#define FLOOR_IDX	((NOTIFY_FLOOR_WINDOWS * NOTIFY_FLOOR_PERCENTILE / 100 > 0) \
			 ? NOTIFY_FLOOR_WINDOWS * NOTIFY_FLOOR_PERCENTILE / 100 - 1 : 0)

_Static_assert(FLOOR_IDX >= 0 && FLOOR_IDX < NOTIFY_FLOOR_WINDOWS, "FLOOR_IDX が履歴の外");

LOCAL INT	prev_cls = AED_CLS_UNKNOWN;	/* 前の窓の判定 (unknown の連続を抑えるため) */
LOCAL BOOL	first = TRUE;			/* 最初の窓は unknown でも1回出す */
LOCAL UW	n_emitted, n_held, n_offlist, n_gated_abs, n_gated_rel, lat_max_us, n_loose;

/* 絶対の門のしきい値 (int16 の振幅)。0 なら門なし。notify_init が dBFS から作る */
LOCAL UW	gate_amp;

/* 相対の門: 直近の窓の rms_db のリング */
LOCAL float	hist[NOTIFY_FLOOR_WINDOWS];
LOCAL UINT	hist_n, hist_pos;

/* 直前の窓の結果の文字列 (notify_last_note)。書くのは notify_window だけ (推論タスク) */
LOCAL char	note[48];

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
	 * 絶対の門。毎窓 log を取らずに済むよう、dBFS を int16 の振幅に直して持つ。
	 * -99dBFS は「切」の意味なので、振幅を 0 にして比較そのものを飛ばす
	 */
	gate_amp = (NOTIFY_GATE_PEAK_DBFS <= -99) ? 0U
			: (UW)(32768.0f * powf(10.0f, (float)NOTIFY_GATE_PEAK_DBFS / 20.0f) + 0.5f);

	/*
	 * 判定の規則を起動ログに残す (ログを読むとき、どの値で走ったか分かるように):
	 *   notify: 3 target classes (dog crying_baby sneezing), threshold p>0.70, peak gate -30 dBFS (peak >= 1036 of 32768)
	 *   notify: rel gate rms >= floor + 10 dB, floor = p10 of the last 60 windows' rms (not applied until 60 windows)
	 * 2 行に分けるのは 1 行の上限 (log.h の LOG_TEXT_MAX) に収めるため。
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
		log_printf("notify: rel gate rms >= floor + %d dB, floor = p%d of the last %d windows' rms (not applied until %d windows)\n",
				NOTIFY_GATE_REL_DB, NOTIFY_FLOOR_PERCENTILE, NOTIFY_FLOOR_WINDOWS, NOTIFY_FLOOR_WINDOWS);
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

/* ---------------------------------------------------------------- */
/* 門                                                                */
/* ---------------------------------------------------------------- */

EXPORT float notify_rms_db(UW rms)
{
	return (rms > 0) ? 20.0f * log10f((float)rms / 32768.0f) : -99.0f;
}

EXPORT NOTIFY_VERDICT notify_gate(float rms_db, UW peak, float p, INT cls, float floor_db)
{
	/* notify_decide と同じ向き (p > THR)。cls が unknown ならそれだけで閾値以下 */
	if(cls == AED_CLS_UNKNOWN || !(p > AED_OOD_THR)) return NOTIFY_BELOW_THR;
	/* 対象外のクラスは門を見ない (gated_abs + gated_rel + PASS = 対象クラスで閾値を超えた窓、が成り立つように) */
	if(!is_target(cls)) return NOTIFY_OFFLIST;
	if(gate_amp > 0 && peak < gate_amp) return NOTIFY_GATED_ABS;
	if(floor_db > NOTIFY_FLOOR_NONE && rms_db < floor_db + (float)NOTIFY_GATE_REL_DB) return NOTIFY_GATED_REL;
	return NOTIFY_PASS;
}

EXPORT float notify_floor(void)
{
	static float	tmp[NOTIFY_FLOOR_WINDOWS];	/* 呼ぶのは推論タスクだけ。スタックに置かない */
	UINT		i, j;
	float		v;

	if(hist_n < NOTIFY_FLOOR_WINDOWS) return NOTIFY_FLOOR_NONE;

	/* 昇順に並べて FLOOR_IDX 番目。60 個の挿入ソートは数 us で、窓の周期 (960ms) に対して無視できる */
	memcpy(tmp, hist, sizeof(tmp));
	for(i = 1; i < NOTIFY_FLOOR_WINDOWS; i++) {
		v = tmp[i];
		for(j = i; j > 0 && tmp[j - 1] > v; j--) tmp[j] = tmp[j - 1];
		tmp[j] = v;
	}
	return tmp[FLOOR_IDX];
}

EXPORT void notify_floor_update(float rms_db)
{
	hist[hist_pos] = rms_db;
	hist_pos = (hist_pos + 1U) % NOTIFY_FLOOR_WINDOWS;
	if(hist_n < NOTIFY_FLOOR_WINDOWS) hist_n++;
}

/* ---------------------------------------------------------------- */
/* 窓行に付ける文字列 (log_printf は浮動小数点を出せないので、ここで小数 1 桁の文字列にする) */
/* ---------------------------------------------------------------- */

LOCAL char *put_str(char *d, const char *s)
{
	while(*s) *d++ = *s++;
	return d;
}

LOCAL char *put_uint(char *d, UW v)
{
	char	b[10];
	INT	n = 0;

	do {
		b[n++] = (char)('0' + v % 10U);
		v /= 10U;
	} while(v > 0 && n < (INT)sizeof(b));
	while(n > 0) *d++ = b[--n];
	return d;
}

/* dB を小数 1 桁で。"-42.6" / "+17.0" (plus なら正にも符号を付ける)。roundf は 0.5 を 0 から遠い側へ */
LOCAL char *put_db(char *d, float v, BOOL plus)
{
	INT	t = (INT)roundf(v * 10.0f), a;

	if(t < 0) {
		*d++ = '-';
		a = -t;
	} else {
		if(plus) *d++ = '+';
		a = t;
	}
	if(a > 9999) a = 9999;		/* 999.9 まで (dBFS の範囲には十分) */
	d = put_uint(d, (UW)(a / 10));
	*d++ = '.';
	*d++ = (char)('0' + a % 10);
	return d;
}

LOCAL void make_note(NOTIFY_VERDICT v, float rms_db, float floor_db)
{
	char	*d = note;

	switch(v) {
	case NOTIFY_PASS:
		if(floor_db > NOTIFY_FLOOR_NONE) {
			d = put_str(d, " (floor ");
			d = put_db(d, floor_db, FALSE);
			d = put_str(d, " \xCE\x94");		/* Δ (U+0394、UTF-8) */
			d = put_db(d, rms_db - floor_db, TRUE);
			d = put_str(d, ")");
		} else {
			d = put_str(d, " (floor n/a)");
		}
		break;
	case NOTIFY_GATED_ABS:
		d = put_str(d, " (gated abs)");
		break;
	case NOTIFY_GATED_REL:
		d = put_str(d, " (gated rel floor ");
		d = put_db(d, floor_db, FALSE);
		d = put_str(d, " \xCE\x94");
		d = put_db(d, rms_db - floor_db, TRUE);
		d = put_str(d, ")");
		break;
	default:
		break;
	}
	*d = '\0';
}

EXPORT const char *notify_last_note(void)
{
	return note;
}

/* ---------------------------------------------------------------- */

EXPORT NOTIFY_VERDICT notify_window(UW win, INT cls, float p, UW rms, UW peak, UW t_ready, BOOL lat_exact)
{
	UW		under, over, late, us, lat_ms;
	INT		p100;
	float		rms_db   = notify_rms_db(rms);
	float		floor_db = notify_floor();
	NOTIFY_VERDICT	v        = notify_gate(rms_db, peak, p, cls, floor_db);

	/* 判定のあとに履歴へ (現在の窓は自分の floor に入らない)。門で止めた窓も鳴った窓も全部入れる */
	notify_floor_update(rms_db);
	make_note(v, rms_db, floor_db);

	/*
	 * 門で止めた窓は unknown と同じ扱いにしてから下の状態の判定に入るので、静かな時間に出るのは
	 * unknown の1行だけになる
	 */
	switch(v) {
	case NOTIFY_OFFLIST:
		/*
		 * 通知対象外のクラス: 何も出さない (数えるだけ)。unknown と同じ「通知しない状態」に
		 * しておき、次に unknown になっても行が増えないようにする
		 */
		n_offlist++;
		prev_cls = AED_CLS_UNKNOWN;
		return v;
	case NOTIFY_GATED_ABS:
		n_gated_abs++;
		cls = AED_CLS_UNKNOWN;
		break;
	case NOTIFY_GATED_REL:
		n_gated_rel++;
		cls = AED_CLS_UNKNOWN;
		break;
	case NOTIFY_BELOW_THR:
		cls = AED_CLS_UNKNOWN;
		break;
	case NOTIFY_PASS:
	default:
		break;
	}

	/*
	 * 出すかどうか。
	 *   通知対象のクラス (PASS) → 毎窓 JSON を出して LED を点ける
	 *   unknown                → 状態が変わったときだけ1行出す
	 */
	if(cls == AED_CLS_UNKNOWN) {
		if(!first && prev_cls == AED_CLS_UNKNOWN) {
			/* 状態が変わっていない。出さない */
			n_held++;
			return v;
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
	return v;
}

EXPORT void notify_stats(UW *emitted, UW *held, UW *offlist, UW *gated_abs, UW *gated_rel,
			UW *lat_max, UW *lat_loose)
{
	*emitted   = n_emitted;
	*held      = n_held;
	*offlist   = n_offlist;
	*gated_abs = n_gated_abs;
	*gated_rel = n_gated_rel;
	*lat_max   = lat_max_us;
	*lat_loose = n_loose;
}
