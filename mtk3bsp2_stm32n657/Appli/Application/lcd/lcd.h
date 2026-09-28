#ifndef LCD_LCD_H
#define LCD_LCD_H

#include <tk/tkernel.h>
#include "st/rk050hr18.h"	// パネルの解像度とタイミング (ST BSP のコンポーネント、無改変)

/*
 * LCD (Phase 2 タスク2-1)
 *
 * パネル: RK050HR18 (800x480)。LTDC 直結で、DSI は使わない
 * (STM32N657 に DSI ペリフェラルは無い。stm32n657xx.h に DSI_TypeDef が無いことを確認済み)。
 *
 * フレームバッファは AXISRAM3 の先頭に置く。L8 (1 画素 1 バイト) + CLUT 16 色で 384,000B。
 * AXISRAM3 は 448KB (stm32n657xx.h の SRAM3_AXI_BASE_S のコメント) なので収まり、
 * NPU が使う AXISRAM6 (0x34350000〜) とは約 969KB 離れていて重ならない。
 * AXISRAM3 のクロックと電源は npu_hw_init() が入れている (npu_hw.c の NPU_MEMEN)。
 *
 * 画素クロックは main.c の MX_LTDC_Clock_Init() がカーネル起動前に作る
 * (PLL4 1200MHz → IC16 /48 → LTDC 25MHz)。ここでは LTDC 本体・GPIO・パネルの制御線を扱う。
 *
 * 呼ぶのは表示タスク (lcd_task.c) だけ。CPU が書いた内容は lcd_flush_rows() で
 * D キャッシュを clean してから LTDC に読ませる。
 */

#define LCD_WIDTH	((UINT)RK050HR18_WIDTH)		/* 800 */
#define LCD_HEIGHT	((UINT)RK050HR18_HEIGHT)	/* 480 */
#define LCD_FB_ADDR	(0x34200000U)			/* AXISRAM3 の先頭 */
#define LCD_FB_SIZE	(LCD_WIDTH * LCD_HEIGHT)	/* L8 なので 1 画素 1 バイト */
#define LCD_CLUT_LEN	(16)				/* パレットの色数 */

/*
 * パレットの番号 (CLUT の添字)。増やすときは lcd.c の clut[] にも足す。
 * 2〜5 は通知の背景色 (タスク5-4。クラスごとの割り当ては model_<name>/aed_model_cfg.h の AED_DISP_COLORS)
 */
#define LCD_BLACK	(0)
#define LCD_WHITE	(1)
#define LCD_RED		(2)
#define LCD_YELLOW	(3)
#define LCD_ORANGE	(4)
#define LCD_BLUE	(5)

/* 背景色 bg の上で読める文字色 (明るい背景なら黒、暗い背景なら白。CLUT の RGB の輝度で決める) */
EXPORT UB lcd_contrast_color(UB bg);

/*
 * GPIO → LTDC → レイヤ/CLUT の順に初期化する。段階ごとに結果をログに出す。
 * 戻り値: E_OK / E_SYS 画素クロックが用意できていない / E_IO LTDC の初期化に失敗
 */
EXPORT ER lcd_init(void);

/* lcd_init() が成功したか */
EXPORT BOOL lcd_ready(void);

/* 画面全体を1色で塗る (フレームバッファだけ。clean は lcd_flush_rows) */
EXPORT void lcd_clear(UB color);

/*
 * 行の帯 (y から rows 行) を1色で塗る。書き換えるところだけ消すのに使う
 * (全画面を消さないため)。画面の外は切り詰める
 */
EXPORT void lcd_fill_rows(UINT y, UINT rows, UB color);

/*
 * 文字列を描く。フォントは ST BSP の Font24 (17x24) を scale 倍に拡大したもの。
 * 画面の外に出る部分は描かない
 */
EXPORT void lcd_text(UINT x, UINT y, const char *s, UB color, UINT scale);

/* scale 倍で描いたときの文字列の幅・高さ [画素] */
EXPORT UINT lcd_text_width(const char *s, UINT scale);
EXPORT UINT lcd_text_height(UINT scale);

/*
 * 書き換えた行 (y から rows 行) の D キャッシュを clean して、LTDC から見えるようにする。
 * 1行 800 バイトはキャッシュライン 32B の倍数なので、行の境界はライン境界に揃う
 */
EXPORT void lcd_flush_rows(UINT y, UINT rows);

#endif	/* LCD_LCD_H */
