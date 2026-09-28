#include <tk/tkernel.h>
#include <string.h>		// memset, strlen
#include "main.h"		// HAL (LTDC, GPIO, RCC), g_ltdc_clk_status, g_ltdc_kerclk, SCB_CleanDCache_by_Addr
#include "lcd.h"
#include "st/fonts.h"		// Font24 (ST BSP の Utilities/Fonts、無改変)
#include "../trace/log.h"	// log_printf()

/*
 * LTDC とパネルの初期化。仕様と置き場所は lcd.h。
 *
 * ST の BSP (STM32CubeN6 v1.3.0 の Drivers/BSP/STM32N6570-DK/stm32n6570_discovery_lcd.c)
 * は取り込まず、そこから値と手順だけを引用して薄く書いた。理由:
 *   - BSP_LCD_InitEx (lcd.c:198-206) が受け付ける形式は RGB565 / RGB888 / ARGB8888 /
 *     ARGB4444 だけで、ここで使う L8 (パレット) を選べない
 *   - BSP は DMA2D も必ず初期化する (lcd.c:256 DMA2D_MspInit) が、描画は CPU で行うので要らない
 *   - BSP を使うには stm32n6570_discovery_conf.h / errno.h / Components/Common/lcd.h と
 *     BSP_LCD_* の描画 API 一式が付いてくる。使うのは初期化だけなので割に合わない
 * 引用元 (STM32CubeN6 v1.3.0):
 *   タイミング  Drivers/BSP/Components/rk050hr18/rk050hr18.h (同梱。無改変)
 *               Drivers/BSP/STM32N6570-DK/stm32n6570_discovery_lcd.c:352-380 MX_LTDC_Init
 *   クロック経路 同 stm32n6570_discovery_lcd.c の MX_LTDC_ClockConfig (LTDC ← IC16 ← PLL4)
 *   GPIO と制御線 同 stm32n6570_discovery_lcd.c の LTDC_MspInit
 *                 (AF14 の信号線、PQ3 LCD_ONOFF / PQ6 LCD_BL_CTRL / PG13 LCD_DE / PE1 出力)
 */

/*
 * 1 で、初期化のあとに LTDC / RIF / PWR / GPIO のレジスタを並べる (読むだけ)。
 * 画面が出ないときに原因を値で決めるためのもの。コミット時は 0。
 * これで見つけた例: RIF 未設定 → RISAF6.IASR=0x2 で読み出しが弾かれ画面が黒 (2026-09-24)
 */
#define LCD_DIAG		(0)

/*
 * RIF (LTDC がバスマスタとして AXISRAM3 を読むための属性)。NPU と同じ考え方で、
 * RIMC でマスタ属性、RISC でスレーブ属性を設定する (npu_hw.c の「RISAF」の節)。
 * 番号の出典 (STM32CubeN6 v1.3.0 の Drivers/STM32N6xx_HAL_Driver/Inc/stm32n6xx_hal_rif.h):
 *   RIF_MASTER_INDEX_LTDC1       = 10                              (:74)
 *   RIF_RISC_PERIPH_INDEX_LTDCL1 = RIF_PERIPH_REG3 | SEC7_Pos      (:183) → レジスタ3の bit7
 * 設定する値は LTDC サンプル (Projects/STM32N6570-DK/Examples/LTDC/
 * LTDC_Horizontal_Mirroring/FSBL/Src/main.c:309-314) と同じ CID1・セキュア・特権
 */
#define LTDC_RIMC_INDEX		(10)
#define LTDC_RISC_REG		(3)
#define LTDC_RISC_SEC		RIFSC_RISC_SECCFGRx_SEC7_Msk
#define LTDC_RISC_PRIV		RIFSC_RISC_PRIVCFGRx_PRIV7_Msk

#define LTDC_MASTER_CID		(1)	/* RIF_CID_1 */
#define LTDC_RIMC_MASK		(RIFSC_RIMC_ATTRx_MCID_Msk | RIFSC_RIMC_ATTRx_MSEC_Msk \
				 | RIFSC_RIMC_ATTRx_MPRIV_Msk)
#define LTDC_RIMC_VALUE		((LTDC_MASTER_CID << RIFSC_RIMC_ATTRx_MCID_Pos) \
				 | RIFSC_RIMC_ATTRx_MSEC_Msk | RIFSC_RIMC_ATTRx_MPRIV_Msk)

LOCAL LTDC_HandleTypeDef	hltdc;
LOCAL BOOL			ready = FALSE;
LOCAL UB			*fb = (UB *)LCD_FB_ADDR;

/*
 * パレット。HAL_LTDC_ConfigCLUT は各要素の下位 24bit (0x00RRGGBB) を使い、
 * 添字は自分で付ける (stm32n6xx_hal_ltdc.c の HAL_LTDC_ConfigCLUT)。
 * 0〜5 を使い、残りは黒のまま空けておく。2〜5 は通知の背景色 (lcd.h の LCD_RED 等。タスク5-4)
 */
LOCAL const uint32_t	clut[LCD_CLUT_LEN] = {
	0x00000000U,	/* 0 LCD_BLACK */
	0x00FFFFFFU,	/* 1 LCD_WHITE */
	0x00E00000U,	/* 2 LCD_RED    (GLASS) */
	0x00FFD800U,	/* 3 LCD_YELLOW (KNOCK / DOG) */
	0x00FF8000U,	/* 4 LCD_ORANGE (CRY) */
	0x000050FFU,	/* 5 LCD_BLUE   (VOICE / SNEEZE) */
	0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U,
	0x00000000U, 0x00000000U, 0x00000000U, 0x00000000U
};

EXPORT UB lcd_contrast_color(UB bg)
{
	uint32_t	c = (bg < LCD_CLUT_LEN) ? clut[bg] : 0U;
	UINT		luma;

	/* 輝度 (BT.601 の重み。0〜255)。半分より明るい背景なら黒い文字、暗ければ白い文字 */
	luma = (299U * ((c >> 16) & 0xFFU) + 587U * ((c >> 8) & 0xFFU) + 114U * (c & 0xFFU)) / 1000U;
	return (luma >= 128U) ? LCD_BLACK : LCD_WHITE;
}

/* ---------------------------------------------------------------- */
/* RIF (LTDC を AXISRAM3 の読み手として通す)                           */
/* ---------------------------------------------------------------- */

/*
 * これが無いと、LTDC がフレームバッファを読もうとして RISAF6 に弾かれ、読めた値が 0 に
 * なって画面が真っ黒になる (実機で RISAF6.IASR=0x2 = IAEF を確認)。
 * RIMC_ATTR のリセット値は 0 (CID0・非セキュア・非特権) で、リージョンを1つも有効に
 * していない RISAF はセキュア・特権のアクセスだけを通すため (npu_hw.c の「RISAF」の節)。
 * 書き方も npu_hw.c と同じ (HAL_RIF は Drivers/ に無いのでレジスタを直接操作する)
 */
LOCAL void rif_config(void)
{
	UW	attr0, sec0, priv0, iasr0, attr, sec, priv;

	attr0 = RIFSC->RIMC_ATTRx[LTDC_RIMC_INDEX];
	sec0  = RIFSC->RISC_SECCFGRx[LTDC_RISC_REG];
	priv0 = RIFSC->RISC_PRIVCFGRx[LTDC_RISC_REG];
	iasr0 = RISAF6_S->IASR;

	/* RIMC: MCID / MSEC / MPRIV だけを書き換える */
	RIFSC->RIMC_ATTRx[LTDC_RIMC_INDEX] = (attr0 & ~LTDC_RIMC_MASK) | LTDC_RIMC_VALUE;

	/* RISC: HAL と同じくセキュア → 特権の順。これが無いと RIMC の MSEC は無視される */
	RIFSC->RISC_SECCFGRx[LTDC_RISC_REG]  |= LTDC_RISC_SEC;
	RIFSC->RISC_PRIVCFGRx[LTDC_RISC_REG] |= LTDC_RISC_PRIV;
	__DSB();

	/*
	 * 記録済みの不正アクセスを消す (write-1-to-clear)。これで、このあと LTDC を
	 * 動かしてもう一度立つかどうかで、通ったかを判定できる
	 */
	RISAF6_S->IACR = RISAF_IACR_CAEF_Msk | RISAF_IACR_IAEF_Msk;
	__DSB();

	attr = RIFSC->RIMC_ATTRx[LTDC_RIMC_INDEX];
	sec  = RIFSC->RISC_SECCFGRx[LTDC_RISC_REG];
	priv = RIFSC->RISC_PRIVCFGRx[LTDC_RISC_REG];
	log_printf("lcd: rif RIMC_ATTR%d 0x%08x -> 0x%08x (MCID=%u MSEC=%u MPRIV=%u)\n",
			LTDC_RIMC_INDEX, attr0, attr,
			(UW)((attr & RIFSC_RIMC_ATTRx_MCID_Msk) >> RIFSC_RIMC_ATTRx_MCID_Pos),
			(UW)(attr & RIFSC_RIMC_ATTRx_MSEC_Msk ? 1U : 0U),
			(UW)(attr & RIFSC_RIMC_ATTRx_MPRIV_Msk ? 1U : 0U));
	log_printf("lcd: rif RISC reg%d bit7 (LTDCL1) SEC %u -> %u, PRIV %u -> %u\n", LTDC_RISC_REG,
			(UW)(sec0 & LTDC_RISC_SEC ? 1U : 0U), (UW)(sec & LTDC_RISC_SEC ? 1U : 0U),
			(UW)(priv0 & LTDC_RISC_PRIV ? 1U : 0U), (UW)(priv & LTDC_RISC_PRIV ? 1U : 0U));
	log_printf("lcd: rif RISAF6.IASR 0x%08x -> 0x%08x (cleared before enabling LTDC)\n",
			iasr0, RISAF6_S->IASR);
}

/* ---------------------------------------------------------------- */
/* GPIO (引用元: stm32n6570_discovery_lcd.c の LTDC_MspInit)          */
/* ---------------------------------------------------------------- */

LOCAL void gpio_config(void)
{
	GPIO_InitTypeDef	gi = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();
	__HAL_RCC_GPIOE_CLK_ENABLE();
	__HAL_RCC_GPIOG_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();
	__HAL_RCC_GPIOQ_CLK_ENABLE();

	/* LTDC の信号線 (RGB・HSYNC・VSYNC・DE・CLK)。すべて AF14 */
	gi.Mode      = GPIO_MODE_AF_PP;
	gi.Pull      = GPIO_NOPULL;
	gi.Speed     = GPIO_SPEED_FREQ_HIGH;
	gi.Alternate = GPIO_AF14_LCD;

	gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_15;
	HAL_GPIO_Init(GPIOA, &gi);
	gi.Pin = GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13
			| GPIO_PIN_14 | GPIO_PIN_15;
	HAL_GPIO_Init(GPIOB, &gi);
	gi.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_15;
	HAL_GPIO_Init(GPIOD, &gi);
	gi.Pin = GPIO_PIN_11;
	HAL_GPIO_Init(GPIOE, &gi);
	gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_6 | GPIO_PIN_8 | GPIO_PIN_11 | GPIO_PIN_12;
	HAL_GPIO_Init(GPIOG, &gi);
	gi.Pin = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_6;
	HAL_GPIO_Init(GPIOH, &gi);

	/* パネルの制御線 (BSP は出力に設定して下の3本を H にする) */
	gi.Mode      = GPIO_MODE_OUTPUT_PP;
	gi.Alternate = 0;
	gi.Pin = GPIO_PIN_1;			/* PE1 (BSP は出力にするだけ) */
	HAL_GPIO_Init(GPIOE, &gi);
	gi.Pin = GPIO_PIN_3 | GPIO_PIN_6;	/* PQ3 LCD_ONOFF、PQ6 LCD_BL_CTRL */
	HAL_GPIO_Init(GPIOQ, &gi);
	gi.Pin = GPIO_PIN_13;			/* PG13 LCD_DE */
	HAL_GPIO_Init(GPIOG, &gi);

	HAL_GPIO_WritePin(GPIOQ, GPIO_PIN_3, GPIO_PIN_SET);	/* パネルの電源 ON */
	HAL_GPIO_WritePin(GPIOG, GPIO_PIN_13, GPIO_PIN_SET);	/* Display Enable */
	HAL_GPIO_WritePin(GPIOQ, GPIO_PIN_6, GPIO_PIN_SET);	/* バックライト 100% */
}

/* ---------------------------------------------------------------- */
/* LTDC (引用元: stm32n6570_discovery_lcd.c:352-380 MX_LTDC_Init)     */
/* ---------------------------------------------------------------- */

LOCAL HAL_StatusTypeDef ltdc_config(void)
{
	__HAL_RCC_LTDC_CLK_ENABLE();
	__HAL_RCC_LTDC_FORCE_RESET();
	__HAL_RCC_LTDC_RELEASE_RESET();

	hltdc.Instance = LTDC;
	hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
	hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
	hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
	hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
	hltdc.Init.HorizontalSync     = RK050HR18_HSYNC - 1U;
	hltdc.Init.AccumulatedHBP     = RK050HR18_HSYNC + RK050HR18_HBP - 1U;
	hltdc.Init.AccumulatedActiveW = RK050HR18_HSYNC + LCD_WIDTH + RK050HR18_HBP - 1U;
	hltdc.Init.TotalWidth         = RK050HR18_HSYNC + LCD_WIDTH + RK050HR18_HBP
						+ RK050HR18_HFP - 1U;
	hltdc.Init.VerticalSync       = RK050HR18_VSYNC - 1U;
	hltdc.Init.AccumulatedVBP     = RK050HR18_VSYNC + RK050HR18_VBP - 1U;
	hltdc.Init.AccumulatedActiveH = RK050HR18_VSYNC + LCD_HEIGHT + RK050HR18_VBP - 1U;
	hltdc.Init.TotalHeigh         = RK050HR18_VSYNC + LCD_HEIGHT + RK050HR18_VBP
						+ RK050HR18_VFP - 1U;
	hltdc.Init.Backcolor.Red   = 0;
	hltdc.Init.Backcolor.Green = 0;
	hltdc.Init.Backcolor.Blue  = 0;

	return HAL_LTDC_Init(&hltdc);
}

/* レイヤ0 を L8 で全面に置き、CLUT を入れる */
LOCAL HAL_StatusTypeDef layer_config(void)
{
	LTDC_LayerCfgTypeDef	lc = {0};
	HAL_StatusTypeDef	st;

	lc.WindowX0 = 0;
	lc.WindowX1 = LCD_WIDTH;
	lc.WindowY0 = 0;
	lc.WindowY1 = LCD_HEIGHT;
	lc.PixelFormat = LTDC_PIXEL_FORMAT_L8;
	lc.Alpha  = 255;
	lc.Alpha0 = 0;
	lc.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
	lc.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
	lc.FBStartAdress = LCD_FB_ADDR;
	lc.ImageWidth  = LCD_WIDTH;
	lc.ImageHeight = LCD_HEIGHT;
	lc.Backcolor.Red   = 0;
	lc.Backcolor.Green = 0;
	lc.Backcolor.Blue  = 0;

	st = HAL_LTDC_ConfigLayer(&hltdc, &lc, 0);
	if(st != HAL_OK) return st;

	st = HAL_LTDC_ConfigCLUT(&hltdc, clut, LCD_CLUT_LEN, 0);
	if(st != HAL_OK) return st;

	return HAL_LTDC_EnableCLUT(&hltdc, 0);
}

/* ---------------------------------------------------------------- */
/* 画面が黒い原因を値で決めるための表示 (読むだけ)                      */
/* ---------------------------------------------------------------- */

#if LCD_DIAG

/* GPIO 1本の MODER (2bit) / ODR / IDR */
LOCAL void diag_pin(const char *name, GPIO_TypeDef *port, UINT pin)
{
	log_printf("  [gpio] %s MODER=%u ODR=%u IDR=%u\n", name,
			(UW)((port->MODER >> (pin * 2U)) & 3U),
			(UW)((port->ODR >> pin) & 1U),
			(UW)((port->IDR >> pin) & 1U));
}

/* RISAF の状態 (npu_hw.c の show_risaf と同じ見方) */
LOCAL void diag_risaf(const char *name, RISAF_TypeDef *r, UINT nregion)
{
	UW	bren = 0, i;

	for(i = 0; i < nregion; i++) {
		if(r->REG[i].CFGR & RISAF_REGx_CFGR_BREN_Msk) bren |= (1U << i);
	}
	log_printf("  [risaf] %s CR=0x%08x IASR=0x%08x BREN=0x%03x\n", name, r->CR, r->IASR, bren);
}

LOCAL void lcd_diag(void)
{
	UW	cpsr1, cpsr2, isr, gcr, l1cr, srcr, rimc, sec, priv, svmcr1;

	log_printf("lcd diag: registers after init (read only)\n");

	/* 1. LTDC が走っているか。CPSR (走査位置) を間を置いて2回読む */
	cpsr1 = LTDC->CPSR;
	tk_dly_tsk(5);				/* 5ms = 62.5Hz のフレーム約 1/3 */
	cpsr2 = LTDC->CPSR;
	gcr  = LTDC->GCR;
	isr  = LTDC->ISR;
	srcr = LTDC->SRCR;
	l1cr = LTDC_LAYER(&hltdc, 0)->CR;
	log_printf("  [ltdc] GCR=0x%08x LTDCEN=%u | L1CR=0x%08x LEN=%u CLUTEN=%u | SRCR=0x%08x\n",
			gcr, (UW)(gcr & LTDC_GCR_LTDCEN_Msk ? 1U : 0U),
			l1cr, (UW)(l1cr & LTDC_LxCR_LEN_Msk ? 1U : 0U),
			(UW)(l1cr & LTDC_LxCR_CLUTEN_Msk ? 1U : 0U), srcr);
	log_printf("  [ltdc] CPSR=0x%08x -> 0x%08x after 5ms: scanning %s\n",
			cpsr1, cpsr2, (cpsr1 != cpsr2) ? "YES" : "NO (stopped)");
	log_printf("  [ltdc] ISR=0x%08x FUIF=%u TERRIF=%u RRIF=%u LIF=%u\n", isr,
			(UW)(isr & LTDC_ISR_FUIF_Msk ? 1U : 0U),
			(UW)(isr & LTDC_ISR_TERRIF_Msk ? 1U : 0U),
			(UW)(isr & LTDC_ISR_RRIF_Msk ? 1U : 0U),
			(UW)(isr & LTDC_ISR_LIF_Msk ? 1U : 0U));
	/*
	 * PFCR の PF は 3bit で、L8 のような CLUT 系は PF=0x7 (= LTDC_LxPFCR_PF) を書いて
	 * 実際の形式を FPF0R / FPF1R で指定する (stm32n6xx_hal_ltdc.c:4037-4048)。
	 * HAL のマクロ LTDC_PIXEL_FORMAT_L8 (0x9) は HAL 内部の識別子で、レジスタの値ではない
	 */
	log_printf("  [ltdc] L1CFBAR=0x%08x CFBLR=0x%08x CFBLNR=%u\n",
			LTDC_LAYER(&hltdc, 0)->CFBAR, LTDC_LAYER(&hltdc, 0)->CFBLR,
			LTDC_LAYER(&hltdc, 0)->CFBLNR);
	log_printf("  [ltdc] PFCR=%u (L8 は flex 形式なので 7 が正しい) FPF0R=0x%08x FPF1R=0x%08x\n",
			LTDC_LAYER(&hltdc, 0)->PFCR, LTDC_LAYER(&hltdc, 0)->FPF0R,
			LTDC_LAYER(&hltdc, 0)->FPF1R);

	/* 2. RIF: LTDC をバスマスタとして通すための属性 (NPU と同じ考え方) */
	rimc = RIFSC->RIMC_ATTRx[LTDC_RIMC_INDEX];
	sec  = RIFSC->RISC_SECCFGRx[LTDC_RISC_REG];
	priv = RIFSC->RISC_PRIVCFGRx[LTDC_RISC_REG];
	log_printf("  [rif] RIMC_ATTRx[%d] (LTDC1) = 0x%08x: MCID=%u MSEC=%u MPRIV=%u (want 1/1/1)\n",
			LTDC_RIMC_INDEX, rimc,
			(UW)((rimc & RIFSC_RIMC_ATTRx_MCID_Msk) >> RIFSC_RIMC_ATTRx_MCID_Pos),
			(UW)(rimc & RIFSC_RIMC_ATTRx_MSEC_Msk ? 1U : 0U),
			(UW)(rimc & RIFSC_RIMC_ATTRx_MPRIV_Msk ? 1U : 0U));
	log_printf("  [rif] RISC reg%d bit7 (LTDCL1): SEC=%u PRIV=%u (want 1/1)\n", LTDC_RISC_REG,
			(UW)(sec & LTDC_RISC_SEC ? 1U : 0U), (UW)(priv & LTDC_RISC_PRIV ? 1U : 0U));
	diag_risaf("RISAF6 AXISRAM3-6", RISAF6_S, 11);

	/* 3. 電源ドメイン: LTDC のサンプルは HAL_MspInit で VDDIO4 を有効にしている */
	svmcr1 = PWR->SVMCR1;
	log_printf("  [pwr] SVMCR1=0x%08x VDDIO4SV=%u VDDIO4VMEN=%u (sample enables VDDIO4SV)\n",
			svmcr1, (UW)(svmcr1 & PWR_SVMCR1_VDDIO4SV_Msk ? 1U : 0U),
			(UW)(svmcr1 & PWR_SVMCR1_VDDIO4VMEN_Msk ? 1U : 0U));

	/* 4. パネルの制御線が本当に H になっているか */
	diag_pin("PQ3  LCD_ONOFF  ", GPIOQ, 3);
	diag_pin("PQ6  LCD_BL_CTRL", GPIOQ, 6);
	diag_pin("PG13 LCD_DE     ", GPIOG, 13);
	diag_pin("PA1  LTDC sig   ", GPIOA, 1);	/* AF14 なら MODER=2 */
}
#endif	/* LCD_DIAG */

/* ---------------------------------------------------------------- */

EXPORT ER lcd_init(void)
{
	UW			pclk, total_w, total_h, refresh_mhz;
	HAL_StatusTypeDef	st;

	/* 1. 画素クロック (カーネル起動前に main.c の MX_LTDC_Clock_Init が作っている) */
	pclk    = (UW)g_ltdc_kerclk;
	total_w = RK050HR18_HSYNC + LCD_WIDTH + RK050HR18_HBP + RK050HR18_HFP;
	total_h = RK050HR18_VSYNC + LCD_HEIGHT + RK050HR18_VBP + RK050HR18_VFP;
	log_printf("lcd: pixel clock %u Hz (PLL4 -> IC16, MX_LTDC_Clock_Init st=%d)\n",
			pclk, (INT)g_ltdc_clk_status);
	if(g_ltdc_clk_status != HAL_OK || pclk == 0) {
		log_printf("lcd: [FAIL] no pixel clock\n");
		return E_SYS;
	}
	/* リフレッシュレートを 0.01Hz 単位の整数で出す (log_printf は浮動小数点を出せない) */
	refresh_mhz = (UW)(((uint64_t)pclk * 100ULL) / ((uint64_t)total_w * total_h));
	log_printf("lcd: %ux%u, total %ux%u -> refresh %u.%02u Hz\n",
			LCD_WIDTH, LCD_HEIGHT, total_w, total_h,
			refresh_mhz / 100U, refresh_mhz % 100U);

	/* 2. RIF: LTDC がフレームバッファ (AXISRAM3) を読めるようにする。LTDC を動かす前に */
	rif_config();

	/* 3. GPIO とパネルの電源・バックライト */
	gpio_config();
	log_printf("lcd: [ OK ] gpio + panel power (PQ3 on, PQ6 backlight, PG13 DE)\n");

	/* 4. LTDC のタイミング */
	st = ltdc_config();
	log_printf("lcd: [%s] ltdc init (hs=%u hbp=%u hfp=%u vs=%u vbp=%u vfp=%u)\n",
			(st == HAL_OK) ? " OK " : "FAIL",
			(UW)RK050HR18_HSYNC, (UW)RK050HR18_HBP, (UW)RK050HR18_HFP,
			(UW)RK050HR18_VSYNC, (UW)RK050HR18_VBP, (UW)RK050HR18_VFP);
	if(st != HAL_OK) return E_IO;

	/* 5. レイヤ (L8) と CLUT */
	st = layer_config();
	log_printf("lcd: [%s] layer0 L8 + CLUT %d colors, framebuffer 0x%08x (%u B)\n",
			(st == HAL_OK) ? " OK " : "FAIL", LCD_CLUT_LEN, LCD_FB_ADDR, LCD_FB_SIZE);
	if(st != HAL_OK) return E_IO;

	ready = TRUE;

#if LCD_DIAG
	lcd_diag();
#endif
	return E_OK;
}

EXPORT BOOL lcd_ready(void)
{
	return ready;
}

/* ---------------------------------------------------------------- */
/* 描画 (フレームバッファへ書くだけ。clean は lcd_flush_rows)           */
/* ---------------------------------------------------------------- */

EXPORT void lcd_clear(UB color)
{
	memset(fb, color, LCD_FB_SIZE);
}

EXPORT void lcd_fill_rows(UINT y, UINT rows, UB color)
{
	if(y >= LCD_HEIGHT) return;
	if(rows > LCD_HEIGHT - y) rows = LCD_HEIGHT - y;

	memset(&fb[y * LCD_WIDTH], color, rows * LCD_WIDTH);
}

LOCAL void put_px(UINT x, UINT y, UB color)
{
	if(x >= LCD_WIDTH || y >= LCD_HEIGHT) return;
	fb[y * LCD_WIDTH + x] = color;
}

/*
 * Font24 は 1 画素 1 ビット、1行あたり (17+7)/8 = 3 バイト、24 行で1文字。
 * 先頭は空白 (0x20) で、左端が最上位ビット (fonts.h と font24.c の並び)
 */
#define FONT_BPR	(((UINT)3))	/* (Font24.Width + 7) / 8 */

LOCAL void draw_char(UINT x, UINT y, char c, UB color, UINT scale)
{
	const UB	*g;
	UINT		row, col, i, j;

	if(c < ' ' || c > '~') return;

	g = &Font24.table[((UINT)(UB)c - ' ') * Font24.Height * FONT_BPR];
	for(row = 0; row < Font24.Height; row++) {
		for(col = 0; col < Font24.Width; col++) {
			if((g[row * FONT_BPR + (col >> 3)] & (0x80U >> (col & 7U))) == 0) continue;
			for(i = 0; i < scale; i++) {
				for(j = 0; j < scale; j++) {
					put_px(x + col * scale + j, y + row * scale + i, color);
				}
			}
		}
	}
}

EXPORT void lcd_text(UINT x, UINT y, const char *s, UB color, UINT scale)
{
	if(scale == 0) scale = 1;

	for(; *s != '\0'; s++) {
		draw_char(x, y, *s, color, scale);
		x += Font24.Width * scale;
	}
}

EXPORT UINT lcd_text_width(const char *s, UINT scale)
{
	if(scale == 0) scale = 1;
	return (UINT)strlen(s) * Font24.Width * scale;
}

EXPORT UINT lcd_text_height(UINT scale)
{
	if(scale == 0) scale = 1;
	return Font24.Height * scale;
}

EXPORT void lcd_flush_rows(UINT y, UINT rows)
{
	if(y >= LCD_HEIGHT) return;
	if(rows > LCD_HEIGHT - y) rows = LCD_HEIGHT - y;

	/*
	 * CPU が書いた画素を RAM に出す。LTDC は AXI から直接読むので、clean しないと
	 * キャッシュに残ったままになる (npu_rt.c の入力と同じ考え方。あちらは領域が
	 * 再利用されるので clean+invalidate だが、ここは CPU が持ち続けるので clean だけ)。
	 * 1行 800B は 32B の倍数なので、行の境界はキャッシュラインの境界に揃う
	 */
	SCB_CleanDCache_by_Addr((uint32_t *)&fb[y * LCD_WIDTH], (int32_t)(rows * LCD_WIDTH));
}
