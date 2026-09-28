#include <tk/tkernel.h>
#include <string.h>		// strlen, memmove
#include "lcd_task.h"
#include "lcd.h"
#include "../aed/notify.h"	// AED_CLASSES, AED_CLS_UNKNOWN, AED_DISP_NAMES / AED_DISP_COLORS (aed_model.h 経由)
#include "../trace/trace.h"	// NOW(), trace_cyc_to_us()
#include "../trace/log.h"	// log_printf()

/*
 * 表示タスク。役割と優先度の理由は lcd_task.h。
 *
 * 画面の作り (800x480。タスク2-2 に 5-4 で背景色と履歴を足した):
 *   全面:     待機中は黒。通知を受けたらクラス色 (AED_DISP_COLORS) で塗り、LCD_HOLD_MS 経ったら黒に戻す。
 *             保持中に別の通知が来たら塗り直して保持時間を測り直す (溜めない)
 *   中央の帯: クラス名 (CLASS_SCALE 倍)。文字色は背景と反対の明度 (lcd_contrast_color)
 *   左上:     1秒ごとに変わる数字 (生存表示)。背景色の上に描く。止まったら同じ数字のままになる
 *   下部の帯: 履歴 HIST_LINES 行 "+MM:SS  KNOCK p=0.86" (起動からの経過時間。新しいものが上)。
 *             背景色に関係なく読めるよう、この帯だけ黒地に白
 * 全面の塗りは lcd_fill_rows(0, LCD_HEIGHT) で行い、1 回の描画時間 (paint) をログと集計に出す
 */

#define LCD_TASK_PRI	(25)
#define LCD_TASK_STKSZ	(2048)		/* log_printf が LOG_MSG (約170B) をスタックに置く。履歴の文字列は静的 */

#define MBF_DEPTH	(8)		/* 通知は約 960ms ごと。取りこぼさない程度 */

#define CLASS_SCALE	(4)		/* Font24 (17x24) の4倍 = 68x96 画素 */
#define CLASS_H		(24 * CLASS_SCALE)			/* 96 */
#define CLASS_Y		((LCD_HEIGHT - CLASS_H) / 2U)		/* 192 */
#define IDLE_TEXT	"READY"

#define BEAT_SCALE	(1)
#define BEAT_X		(8)
#define BEAT_Y		(8)
#define BEAT_H		(24 * BEAT_SCALE)
#define BEAT_MS		(1000)		/* 生存表示を変える間隔 */

#define HIST_LINES	(3)
#define HIST_SCALE	(2)		/* 34x48 画素。"+MM:SS  KNOCK p=0.86" 20 文字で 680 画素 */
#define HIST_LINE_H	(24 * HIST_SCALE)			/* 48 */
#define HIST_H		(HIST_LINE_H * HIST_LINES)		/* 144 */
#define HIST_Y		(LCD_HEIGHT - HIST_H - 8U)		/* 328 (中央の帯 192〜287 と重ならない) */
#define HIST_X		(8)
#define HIST_TEXT_MAX	(32)		/* "+MMM:SS  " 9 + クラス名 8 + " p=0.00" 7 + NUL */

#define POLL_MIN_MS	(10)		/* 待ちの下限 (時間の計算が 0 以下になったとき) */

/* 画面に出す短い英字と背景色。並びはモデルの出力順 (aed_model.h で選んだ model_<name>/aed_model_cfg.h) */
LOCAL const char *const disp_name[AED_CLASSES]  = AED_DISP_NAMES;
LOCAL const UB		disp_color[AED_CLASSES] = AED_DISP_COLORS;

typedef struct {
	INT	cls;
	INT	p100;
	UW	win;
	UW	t_notify;
} lcd_msg_t;

LOCAL ID	mbfid = 0;
LOCAL T_CMBF	cmbf_lcd = {
	.mbfatr		= TA_TFIFO,
	.bufsz		= sizeof(lcd_msg_t) * MBF_DEPTH,
	.maxmsz		= sizeof(lcd_msg_t),
};

/* 統計。増やすのは送り手 (推論タスク) と表示タスクなので DI/EI で守る */
LOCAL UW	n_sent, n_dropped, lat_max_us, lat_sum_us, lat_n;

/* 描画の状態 (表示タスクだけが触る) */
LOCAL UB	bg = LCD_BLACK;			/* 今の背景色 */
LOCAL UINT	beat_now;			/* 生存表示の数字 (全面を塗り直すときに描き直す) */
LOCAL char	hist[HIST_LINES][HIST_TEXT_MAX];	/* 履歴。[0] が最新 */
LOCAL UINT	hist_n;

LOCAL void task_lcd(INT stacd, void *exinf);
LOCAL ID	tskid_lcd;
LOCAL T_CTSK	ctsk_lcd = {
	.itskpri	= LCD_TASK_PRI,
	.stksz		= LCD_TASK_STKSZ,
	.task		= task_lcd,
	.tskatr		= TA_HLNG | TA_RNG3,
};

/* ---------------------------------------------------------------- */
/* 受け渡し                                                           */
/* ---------------------------------------------------------------- */

EXPORT ER lcd_post(INT cls, INT p100, UW win, UW t_notify)
{
	lcd_msg_t	m;
	UINT		imask;
	ER		er;

	if(mbfid <= 0) return E_OBJ;

	m.cls      = cls;
	m.p100     = p100;
	m.win      = win;
	m.t_notify = t_notify;

	er = tk_snd_mbf(mbfid, &m, sizeof(m), TMO_POL);

	DI(imask);
	if(er < E_OK) n_dropped++;
	else          n_sent++;
	EI(imask);

	return (er < E_OK) ? E_QOVR : E_OK;
}

EXPORT void lcd_post_stats(UW *sent, UW *dropped, UW *lat_max, UW *lat_avg)
{
	UINT	imask;

	DI(imask);
	*sent    = n_sent;
	*dropped = n_dropped;
	*lat_max = lat_max_us;
	*lat_avg = (lat_n > 0) ? (lat_sum_us / lat_n) : 0;
	EI(imask);
}

/* ---------------------------------------------------------------- */
/* 履歴の文字列 (snprintf は使えないので自前。書式は "+MM:SS  KNOCK p=0.86")  */
/* ---------------------------------------------------------------- */

LOCAL char *put_str(char *d, const char *s, const char *end)
{
	while(*s != '\0' && d < end) *d++ = *s++;
	return d;
}

/* 10 進。digits 桁に満たなければ 0 で埋める (0 なら埋めない) */
LOCAL char *put_uint(char *d, UW v, UINT digits, const char *end)
{
	char	b[10];
	UINT	n = 0;

	do {
		b[n++] = (char)('0' + v % 10U);
		v /= 10U;
	} while(v > 0 && n < sizeof(b));
	while(n < digits && n < sizeof(b)) b[n++] = '0';
	while(n > 0 && d < end) *d++ = b[--n];
	return d;
}

LOCAL const char *class_text(INT cls)
{
	if(cls < 0 || cls >= AED_CLASSES) return IDLE_TEXT;
	return disp_name[cls];
}

LOCAL UB class_color(INT cls)
{
	if(cls < 0 || cls >= AED_CLASSES) return LCD_BLACK;
	return disp_color[cls];
}

/* 履歴の先頭に 1 行足す (古いものは下へ、あふれた分は捨てる) */
LOCAL void hist_push(INT cls, INT p100)
{
	SYSTIM	tim;
	UW	sec = 0;
	char	*d, *end;

	/* 起動からの経過時間 (システム稼働時間 [ms]。上位 32bit は 49 日を超えなければ 0) */
	if(tk_get_otm(&tim) == E_OK) sec = tim.lo / 1000U;

	memmove(&hist[1], &hist[0], sizeof(hist[0]) * (HIST_LINES - 1));
	d   = hist[0];
	end = hist[0] + HIST_TEXT_MAX - 1;
	d = put_str(d, "+", end);
	d = put_uint(d, sec / 60U, 2, end);
	d = put_str(d, ":", end);
	d = put_uint(d, sec % 60U, 2, end);
	d = put_str(d, "  ", end);
	d = put_str(d, class_text(cls), end);
	d = put_str(d, " p=", end);
	d = put_uint(d, (UW)(p100 / 100), 1, end);
	d = put_str(d, ".", end);
	d = put_uint(d, (UW)(p100 % 100), 2, end);
	*d = '\0';
	if(hist_n < HIST_LINES) hist_n++;
}

/* ---------------------------------------------------------------- */
/* 描画                                                              */
/* ---------------------------------------------------------------- */

/* 生存表示。0〜9 を順に出すので、止まると同じ数字のままになる。背景色の上に描く */
LOCAL void draw_beat(UINT n, UB bgc, BOOL flush)
{
	char	s[2];

	s[0] = (char)('0' + (n % 10U));
	s[1] = '\0';

	lcd_fill_rows(BEAT_Y, BEAT_H, bgc);
	lcd_text(BEAT_X, BEAT_Y, s, lcd_contrast_color(bgc), BEAT_SCALE);
	if(flush) lcd_flush_rows(BEAT_Y, BEAT_H);
}

/* 履歴の帯 (黒地に白。flush は呼び出し側) */
LOCAL void draw_hist(void)
{
	UINT	i;

	lcd_fill_rows(HIST_Y, HIST_H, LCD_BLACK);
	for(i = 0; i < hist_n; i++) {
		lcd_text(HIST_X, HIST_Y + i * HIST_LINE_H, hist[i], LCD_WHITE, HIST_SCALE);
	}
}

/*
 * 全面を背景色 bgc で塗り、中央にクラス名 text、左上に生存表示、下部に履歴を描いて
 * 全行を clean する。戻り値: かかった時間 [us] (DWT)
 */
LOCAL UW paint_all(UB bgc, const char *text)
{
	UW	t0 = NOW(), us;
	UINT	w, x;

	lcd_fill_rows(0, LCD_HEIGHT, bgc);

	w = lcd_text_width(text, CLASS_SCALE);
	x = (LCD_WIDTH > w) ? ((LCD_WIDTH - w) / 2U) : 0U;
	lcd_text(x, CLASS_Y, text, lcd_contrast_color(bgc), CLASS_SCALE);

	draw_beat(beat_now, bgc, FALSE);
	draw_hist();

	lcd_flush_rows(0, LCD_HEIGHT);

	us = trace_cyc_to_us((UW)(NOW() - t0));
	return us;
}

/* ---------------------------------------------------------------- */

/* 経過 [ms]。DWT は約7.16秒で折り返すので、そのつもりで短い時間だけを測る */
LOCAL UW ms_since(UW t0)
{
	return trace_cyc_to_us((UW)(NOW() - t0)) / 1000U;
}

LOCAL void task_lcd(INT stacd, void *exinf)
{
	lcd_msg_t	msg;
	UW		t_hold = 0, t_beat, lat, left_hold, left_beat, paint_us;
	BOOL		holding = FALSE;
	UINT		imask;
	INT		sz;
	TMO		tmout;
	ER		er;

	er = lcd_init();
	log_printf("lcd_init: ret=%d\n", er);

	if(er != E_OK) {
		/* 画面は出せない。通知を捨て続けないよう、ここで寝る */
		tk_slp_tsk(TMO_FEVR);
	}

	bg       = LCD_BLACK;
	paint_us = paint_all(bg, IDLE_TEXT);
	t_beat   = NOW();
	log_printf("lcd: [ OK ] idle \"%s\" (class band y=%u h=%u scale %d, beat at %u,%u,"
			" history %d lines at y=%u, full paint %u us)\n",
			IDLE_TEXT, (UW)CLASS_Y, (UW)CLASS_H, CLASS_SCALE, (UW)BEAT_X, (UW)BEAT_Y,
			HIST_LINES, (UW)HIST_Y, paint_us);

	for(;;) {
		/*
		 * 次にやること (生存表示の更新、保持の解除) までの短い方だけ待つ。
		 * 通知が来ればそこで起きる
		 */
		left_beat = (ms_since(t_beat) >= BEAT_MS) ? 0U : (BEAT_MS - ms_since(t_beat));
		left_hold = 0xFFFFFFFFU;
		if(holding) {
			left_hold = (ms_since(t_hold) >= LCD_HOLD_MS)
					? 0U : (LCD_HOLD_MS - ms_since(t_hold));
		}
		tmout = (TMO)((left_beat < left_hold) ? left_beat : left_hold);
		if(tmout < POLL_MIN_MS) tmout = POLL_MIN_MS;

		sz = tk_rcv_mbf(mbfid, &msg, tmout);
		if(sz >= (INT)sizeof(msg)) {
			/* 新しい検出。保持中でも背景とクラス名を上書きし、保持時間を測り直す */
			hist_push(msg.cls, msg.p100);
			bg       = class_color(msg.cls);
			paint_us = paint_all(bg, class_text(msg.cls));
			holding  = TRUE;
			t_hold   = NOW();

			/* 通知から描き終わりまで (全面の塗りを含む) */
			lat = trace_cyc_to_us((UW)(NOW() - msg.t_notify));
			DI(imask);
			if(lat > lat_max_us) lat_max_us = lat;
			lat_sum_us += lat;
			lat_n++;
			EI(imask);

			if(!trace_muted()) {
				log_printf("lcd: win %u -> %s p=%d.%02d (%u us after notify) paint=%uus bg=%u\n",
						msg.win, class_text(msg.cls),
						msg.p100 / 100, msg.p100 % 100, lat, paint_us, (UW)bg);
			}
		}

		/* 保持の時間が過ぎたら待機表示 (黒の READY) に戻す。履歴は残す */
		if(holding && ms_since(t_hold) >= LCD_HOLD_MS) {
			bg      = LCD_BLACK;
			(void)paint_all(bg, IDLE_TEXT);
			holding = FALSE;
		}

		/* 生存表示 (今の背景色の上に描く) */
		if(ms_since(t_beat) >= BEAT_MS) {
			beat_now++;
			draw_beat(beat_now, bg, TRUE);
			t_beat = NOW();
		}
	}
}

EXPORT ER lcd_task_start(void)
{
	ID	id;

	id = tk_cre_mbf(&cmbf_lcd);
	if(id < E_OK) return (ER)id;
	mbfid = id;

	tskid_lcd = tk_cre_tsk(&ctsk_lcd);
	if(tskid_lcd < E_OK) return (ER)tskid_lcd;

	return tk_sta_tsk(tskid_lcd, 0);
}
