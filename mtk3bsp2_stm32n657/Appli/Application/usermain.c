#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include "main.h"	// CMSIS (SCB, SCB_CCR_DC_Msk, SCB_*DCache, __DSB), HAL_IncTick
#include "audio/audio_task.h"
#include "extflash/extflash.h"
#include "npu/npu_hw.h"
#include "npu/infer_task.h"
#include "npu/aed_model.h"	// AED_MODEL_NAME (CONFIG: 行)
#include "aed/notify.h"
#include "lcd/lcd_task.h"
#include "audio/tap_ring.h"
#include "fault/fault.h"
#include "trace/trace.h"

#define TRACE_WINDOW_MS		(10000)	/* 記録する区間の長さ */
#define TRACE_WAIT_POLL		(100)	/* パススルー待ちの上限 (x100ms) */

/* ================================================================ */
/* Phase 1 タスク2 (暫定): 外部フラッシュ重み領域の読み出し確認        */
/* ================================================================ */
/*
 * ねらい: extflash_init() (Application/extflash/) がメモリマップした
 *         0x70180000 (aed_weights の先頭) を CPU から読み、書き込んだ
 *         重みと一致するかを確かめる。一致すれば Phase 2 で重みを
 *         そのまま参照できる。
 *
 * 確認済みの前提 (FSBL のソース):
 *   XSPI2_BASE                 = 0x70000000 (stm32n657xx.h:3003)
 *   EXTMEM_LRUN_SOURCE_ADDRESS = 0x00100000 (stm32_extmem_conf.h:76)
 *   source = MapAddress + SOURCE_ADDRESS    (stm32_boot_lrun.c:153)
 *   → アプリの格納先は 0x70100000。重み 0x70180000 とは 512KB 離れて
 *     いて重ならない。アプリ本体は現状 0x1DA40 (121,408B) しかない。
 *
 * 期待値: aed_weights.hex (STM32N6-GettingStarted-Audio v2.3.0,
 *   Projects/X-CUBE-AI/models/) の 0x70180000 からの 64 バイト。
 *   先頭は f3 02 e9 fc ed 03 0d df ...。重みはリポジトリに入れない
 *   方針なので、ここにはその 64 バイトの FNV-1a (32bit) だけを置いて
 *   読んだ値のハッシュと比べる。
 *
 * D-cache を一時的に落としてから読むのは、キャッシュラインフィル中に
 * 出たバスエラーが imprecise になって BFAR が無効になるのを避けるため。
 * 落としたまま読めば PRECISERR + BFARVALID で番地が確定する。
 * 読めた場合もキャッシュを経由しないぶん値が信用できる。
 *
 * 用が済んだらこのブロックごと消す。
 */
#define FLASH_PROBE		(1)		/* 0 で無効化 */
#define FLASH_PROBE_ADDR	((UW)0x70180000)
#define FLASH_PROBE_LEN		(64)
#define FLASH_PROBE_FNV1A	(0x568e47bdU)	/* 期待値 64 バイトの FNV-1a */

#if FLASH_PROBE

LOCAL UW probe_fnv1a(const UB *p, INT len)
{
	UW	h = 2166136261U;
	INT	i;

	for(i = 0; i < len; i++) {
		h ^= p[i];
		h *= 16777619U;
	}
	return h;
}

/* val を digits 桁の 16 進で出す (ゼロ埋め。tm_printf の %0Nx と同じ表示) */
LOCAL void probe_puthex(UW val, INT digits)
{
	INT	i;

	for(i = digits - 1; i >= 0; i--) {
		tm_putchar((INT)"0123456789abcdef"[(val >> (i * 4)) & 0xF]);
	}
}

LOCAL void flash_probe(void)
{
	const volatile UB	*src = (const volatile UB *)FLASH_PROBE_ADDR;
	UB			buf[FLASH_PROBE_LEN];
	BOOL			dcache_on;
	UW			hash;
	INT			i;

	/*
	 * フォルトした場合に UART に残る最後の行がこれになる。
	 * 「この直後の読み出しで落ちた」と一意に分かるようにしておく。
	 */
	tm_putstring((UB*)"\n[probe] reading 64B @ 0x");
	probe_puthex(FLASH_PROBE_ADDR, 8);
	tm_putstring((UB*)" ...\n");

	dcache_on = ((SCB->CCR & SCB_CCR_DC_Msk) != 0);
	if(dcache_on) SCB_DisableDCache();

	for(i = 0; i < FLASH_PROBE_LEN; i++) {
		buf[i] = src[i];
	}
	__DSB();	/* 取りこぼしたバスエラーをここで確定させる */

	if(dcache_on) SCB_EnableDCache();

	/* ここに来たということはフォルトしていない */
	tm_putstring((UB*)"[probe] OK (no fault, D-cache was ");
	tm_putstring(dcache_on ? (UB*)"on)\n" : (UB*)"off)\n");

	for(i = 0; i < FLASH_PROBE_LEN; i++) {
		if((i % 16) == 0) {
			probe_puthex(FLASH_PROBE_ADDR + (UW)i, 8);
			tm_putchar(':');
		}
		tm_putchar(' ');
		probe_puthex((UW)buf[i], 2);
		if((i % 16) == 15) tm_putchar('\n');
	}

	hash = probe_fnv1a(buf, FLASH_PROBE_LEN);
	tm_putstring((UB*)"[probe] FNV-1a 0x");
	probe_puthex(hash, 8);
	tm_putstring((UB*)" (expect 0x");
	probe_puthex(FLASH_PROBE_FNV1A, 8);
	tm_putstring((hash == FLASH_PROBE_FNV1A) ? (UB*)") MATCH\n" : (UB*)") MISMATCH\n");
}

#endif	/* FLASH_PROBE */

/*
 * HAL ティック。カーネル起動後は SysTick の例外ベクタがカーネルの
 * knl_systim_inthdr() に替わり、stm32n6xx_it.c の SysTick_Handler() →
 * HAL_IncTick() が呼ばれなくなる。そのままでは HAL_GetTick() が止まり、
 * HAL のタイムアウト (XSPI, I2C 等) が効かずに失敗時に戻ってこない
 * (HAL_Delay() も戻らない)。カーネルの周期ハンドラで代わりに進める。
 * カーネルのタイマ周期 (CNF_TIMER_PERIOD) が 10ms なので、10ms ごとに
 * HAL_IncTick() を10回呼ぶ (HAL 側は 1ms 単位のまま、分解能だけ 10ms)。
 */
#define HAL_TICK_PERIOD_MS	(10)

LOCAL void hal_tick_cychdr(void *exinf)
{
	INT	i;

	for(i = 0; i < HAL_TICK_PERIOD_MS; i++) {
		HAL_IncTick();
	}
}

LOCAL T_CCYC ccyc_hal_tick = {
	.cycatr	= TA_HLNG | TA_STA,
	.cychdr	= (FP)hal_tick_cychdr,
	.cyctim	= HAL_TICK_PERIOD_MS,
	.cycphs	= HAL_TICK_PERIOD_MS,
};

LOCAL void task_1(INT stacd, void *exinf);	// task execution function
LOCAL ID	tskid_1;			// Task ID number
LOCAL T_CTSK ctsk_1 = {				// Task creation information
	.itskpri	= 10,
	.stksz		= 1024,
	.task		= task_1,
	.tskatr		= TA_HLNG | TA_RNG3,
};


/*
 * 生存表示。緑 LED (PO1) を 500ms ごとに反転するだけ。
 * タスク9 で表示は止めた (UART はレポータタスクに集約し、毎秒の "task 1" は出さない)。
 * 赤 LED (PG10) は検出の通知に使うので触らない (Application/aed/notify.c)。
 * 以前あった task_2 (赤 LED の点滅と "task 2" の表示) はそのために削除した
 */
LOCAL void task_1(INT stacd, void *exinf)
{
	while(1) {
		out_w(GPIO_ODR(O), (in_w(GPIO_ODR(O)))^(1<<1));
		tk_dly_tsk(500);
	}
}

/* usermain関数 */
EXPORT INT usermain(void)
{
	INT	i;
	ER	er;

	/* 何よりも先に。以降のフォルト・未実装IRQはUARTに出てから止まる */
	app_fault_init();

	/* DWT CYCCNT を起こす。以降の計測はすべてこれが基準 */
	trace_init();

	/* どの条件のログかを最初の行で分かるようにする (npu/infer_task.h の対照実験のスイッチ)。
	 * まだレポータが無いので直接 UART に出る = 後で reporter が飢えても消えない */
	tm_printf((UB*)"CONFIG: %s (INFER_PRIO_INVERT=%d INFER_SLOW_X=%d task_infer pri %d model=%s)\n",
			INFER_CONFIG_NAME, INFER_PRIO_INVERT, INFER_SLOW_X, INFER_TASK_PRI, AED_MODEL_NAME);

	tm_putstring((UB*)"Start User-main program.\n");

	/* HAL のタイムアウトを効かせる。HAL を使う初期化 (extflash, WM8904) より前に */
	if(tk_cre_cyc(&ccyc_hal_tick) < E_OK) {
		tm_printf((UB*)"hal tick cyclic handler FAILED\n");
	}

	/* 外部フラッシュ (重み) をメモリマップする。失敗しても止めずに続行し、
	 * 外部フラッシュを読む処理だけを飛ばす */
	er = extflash_init();
	tm_printf((UB*)"extflash_init: ret=%d\n", er);

#if FLASH_PROBE
	/* 外部フラッシュの重み領域が読めるか。フォルトすれば上の
	 * app_fault_init() の経路で BFAR まで出てから止まる */
	if(extflash_mapped()) {
		flash_probe();
	} else {
		tm_printf((UB*)"[probe] skipped (external flash not mapped)\n");
	}
#endif

	/* NPU 用の内部メモリ・NPU・NPU キャッシュ・RIF。失敗しても止めずに続行する。
	 * RAM の確認で D キャッシュを一時的に止めるので、音声の DMA を始める前に */
	er = npu_hw_init();
	tm_printf((UB*)"npu_hw_init: ret=%d\n", er);

	/* 推論用のタップリング。書き手 (task_pcm) と読み手 (推論タスク) が動き出す前に */
	er = tap_ring_init();
	tm_printf((UB*)"tap_ring_init: ret=%d\n", er);

	/* 通知 (JSON と赤 LED)。推論タスクが窓を処理する前に */
	er = notify_init();
	tm_printf((UB*)"notify_init: ret=%d\n", er);

	/* 表示タスク (優先度25)。LCD の初期化はタスクの中で行う。
	 * フレームバッファは AXISRAM3 なので npu_hw_init の後に起こす */
	er = lcd_task_start();
	tm_printf((UB*)"lcd_task_start: ret=%d\n", er);

	/* 推論タスク: 推論ランタイムの初期化と自己テストの後、タップリングの窓を待つ。
	 * ランタイムはスタックを多く使うので、この初期タスク (スタック 1KB) では呼ばない。
	 * 自己テストが終わるまでここで待つ (推論時間を音声の負荷なしで測るため、音声より先に)。
	 * npu_hw_init が失敗していれば自己テストを飛ばし、窓の確認だけ行う */
	er = infer_task_start();
	tm_printf((UB*)"infer_task_start: ret=%d\n", er);

	/* 受け入れテスト (fault.h の FAULT_TEST)。1〜4なら戻ってこない */
	fault_test_run();

	/* 生存表示 (緑 LED の点滅) */
	tskid_1 = tk_cre_tsk(&ctsk_1);
	tk_sta_tsk(tskid_1, 0);

	/* 計測タスク(レポータ/ダンプ)と1秒周期ハンドラを用意する。
	 * audio 側が trace_rate_start() を呼ぶので、その前に作っておく */
	if(trace_task_start() < E_OK) {
		tm_printf((UB*)"trace_task_start FAILED\n");
	}

	audio_task_start();

	/* ---- 計測したい区間はここ ----
	 * パススルーが立ち上がるのを待ってから10秒間だけ記録する。
	 * 記録は時間経過かリング満杯で自動停止し、そのままCSVダンプに移る。 */
	for(i = 0; i < TRACE_WAIT_POLL; i++) {
		if(audio_passthrough_active()) break;
		tk_dly_tsk(100);
	}
	if(audio_passthrough_active()) {
		trace_start(TRACE_WINDOW_MS);
	} else {
		tm_printf((UB*)"trace: passthrough did not start, skip\n");
	}

	tk_slp_tsk(TMO_FEVR);

	return 0;
}
