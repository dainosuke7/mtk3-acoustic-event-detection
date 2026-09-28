#include <tk/tkernel.h>
#include <math.h>		// cos, log10, pow, sqrtf, logf, roundf
#include <float.h>		// FLT_MIN
#include <string.h>		// memset
#include "preproc.h"
#include "../trace/trace.h"	// NOW(), trace_cyc_to_us()

/*
 * AED の前処理 (log-mel スペクトログラム)。仕様と呼び出しの制約は preproc.h。
 *
 * ST の実装との対応 (GettingStarted-Audio v2.3.0):
 *   列のループと転置		Projects/Dpu/preproc_dpu.c:135-143
 *   ゼロ詰め・Ref・TopdB	Projects/Dpu/preproc_dpu.c:41,52-53,69-70
 *   ゼロ詰めと窓掛け		Middlewares/.../audio_din_f16.c:30 audio_is16of16_pad と
 *				feature_extraction_f16.c:238 audio_is16of16_fft (詰めてから中央 400 に窓)
 *   振幅スペクトル		feature_extraction_f16.c:73 SpectrogramColumn_f16 (MAGNITUDE = |X|)
 *   メルフィルタ		mel_filterbank_f16.c:214 MelFilterbank_f16 (start〜stop の内積)
 *   対数と量子化		feature_extraction_f16.c:264 LogMelSpectrogramColumn_q15_f16_Q8
 *				(:334-337 が SSAT(roundf(x * inv_scale + offset), 8))
 *
 * ST は FP16 で計算する (app_config.h の PREPROC_FLOAT_16)。ここは float32 で、
 * PC 側の参照実装 (scripts/aed_clips.py の logmel_q8、numpy は FFT を float64 で行う) に
 * 寄せている。float32 と float64 の差は log を取ると相対 1e-6 程度で、量子化 1 LSB が
 * 1/32.75 なので int8 では丸めの境界に当たった要素だけが 1 違う。
 *
 * 表 (窓・ツイドル・メルフィルタ) は preproc_init() が double で作って float32 で持つ。
 * PC 側も float64 で作って float32 に落としているので同じ値になる
 * (PC の表と ST の表の差は窓 2.8e-8・メル 5e-12 で、scripts/aed_clips.py が毎回照合する)。
 */

#define PI_D			(3.14159265358979323846)	/* numpy の np.pi と同じ double */

#define N_FFT			AED_PREPROC_NFFT		/* 512 */
#define N_BINS			(N_FFT / 2 + 1)			/* 257 */
#define N_STAGES		(9)				/* 512 = 2^9 */
#define PAD_L			((N_FFT - AED_PREPROC_WIN) / 2)			/* 56 */
#define PAD_R			((N_FFT - AED_PREPROC_WIN) / 2 + ((N_FFT - AED_PREPROC_WIN) % 2))	/* 56 */

#define SR			(16000.0)	/* CTRL_X_CUBE_AI_SENSOR_ODR */
#define FMIN			(125.0)
#define FMAX			(7500.0)

/* メルフィルタの係数表の上限。ST・PC と同じなら AED_PREPROC_MEL_COEFS (461) 個入る */
#define MEL_LUT_MAX		(512)

/*
 * 量子化 (preproc_set_quant が設定。モデルの入力の scale / zero_point)。
 * ST の output_Q_inv_scale / output_Q_offset (ai_dpu.c:170 の 1 / scale) にあたる
 */
LOCAL float	inv_scale;
LOCAL float	q_scale;
LOCAL INT	q_zp;
LOCAL BOOL	quant_set = FALSE;

/* ---------------------------------------------------------------- */
/* 表 (preproc_init が作る)                                           */
/* ---------------------------------------------------------------- */

LOCAL float	win_tbl[AED_PREPROC_WIN];	/* 周期ハン窓 400 */
LOCAL float	tw_re[N_FFT / 2];		/* ツイドル cos(2pi k / 512) */
LOCAL float	tw_im[N_FFT / 2];		/* 同 -sin(2pi k / 512) */
LOCAL UH	brev[N_FFT];			/* ビット反転の並べ替え先 */

LOCAL float	mel_lut[MEL_LUT_MAX];		/* メルフィルタの係数 (start〜stop を詰めて並べる) */
LOCAL UH	mel_start[AED_PREPROC_MELS];	/* メル j の最初の bin */
LOCAL UH	mel_stop[AED_PREPROC_MELS];	/* 同、最後の bin */
LOCAL UH	mel_off[AED_PREPROC_MELS];	/* 同、mel_lut の中の位置 */
LOCAL UINT	mel_n;				/* mel_lut に入れた係数の数 */

LOCAL BOOL	ready = FALSE;

/* 作業域 (タスクのスタックに置かない。1列分) */
LOCAL float	fft_re[N_FFT];
LOCAL float	fft_im[N_FFT];
LOCAL float	mag[N_BINS];

/* ---------------------------------------------------------------- */
/* 表作り                                                             */
/* ---------------------------------------------------------------- */

/* librosa の hz_to_mel / mel_to_hz (htk=True)。ST の MelScale_f16 / InverseMelScale_f16 と同じ式 */
LOCAL double hz_to_mel(double f)
{
	return 2595.0 * log10(1.0 + f / 700.0);
}

LOCAL double mel_to_hz(double m)
{
	return 700.0 * (pow(10.0, m / 2595.0) - 1.0);
}

/*
 * librosa.filters.mel が使う三角形の頂点。
 * np.linspace(hz_to_mel(FMIN), hz_to_mel(FMAX), N_MELS + 2) を mel_to_hz したもの。
 * numpy の linspace は start + k * ((stop - start) / (num - 1)) を計算し、最後の要素だけ
 * stop を入れ直す。丸めまで合わせるため同じ順序で計算する
 */
LOCAL double mel_vertex(INT k)
{
	double	lo = hz_to_mel(FMIN), hi = hz_to_mel(FMAX), m;

	if(k >= AED_PREPROC_MELS + 1) {
		m = hi;
	} else {
		m = lo + (double)k * ((hi - lo) / (double)(AED_PREPROC_MELS + 1));
	}
	return mel_to_hz(m);
}

/* rfft の bin b の中心周波数。np.linspace(0, SR/2, 257)[b] (31.25Hz 刻み。2進で厳密) */
LOCAL double fft_freq(INT b)
{
	return (double)b * ((SR / 2.0) / (double)(N_FFT / 2));
}

/*
 * メルフィルタ 64 本を、ST と同じ疎な形 (開始 bin・終了 bin・係数の並び) で作る。
 * 係数は librosa.filters.mel(htk=True, norm=None) と同じ
 *   lower = (f - mel_f[j])     / (mel_f[j+1] - mel_f[j])
 *   upper = (mel_f[j+2] - f)   / (mel_f[j+2] - mel_f[j+1])
 *   w     = max(0, min(lower, upper))
 * librosa は float64 で作って float32 の配列に入れるので、非ゼロの判定は float32 で行う
 */
LOCAL ER mel_init(void)
{
	double	f0, f1, f2, f, lower, upper, w;
	float	wf;
	INT	j, b, start, stop;

	mel_n = 0;
	for(j = 0; j < AED_PREPROC_MELS; j++) {
		f0    = mel_vertex(j);
		f1    = mel_vertex(j + 1);
		f2    = mel_vertex(j + 2);
		start = -1;
		stop  = -1;
		mel_off[j] = (UH)mel_n;

		/*
		 * 非ゼロの範囲 (start〜stop) だけを mel_lut へ詰める。三角形なので途中に 0 は
		 * 無く、0 に戻ったらそこで抜けてよい (ST の表と同じ並びになる)
		 */
		for(b = 0; b < N_BINS; b++) {
			f     = fft_freq(b);
			lower = (f - f0) / (f1 - f0);
			upper = (f2 - f) / (f2 - f1);
			w     = (lower < upper) ? lower : upper;
			if(w < 0.0) w = 0.0;
			wf = (float)w;

			if(wf <= 0.0f) {
				if(start < 0) continue;		/* まだ三角形の前 */
				break;				/* 三角形を抜けた */
			}
			if(start < 0) start = b;
			stop = b;
			if(mel_n >= MEL_LUT_MAX) return E_NOMEM;
			mel_lut[mel_n++] = wf;
		}

		if(start < 0) {
			/* 非ゼロが無い (モデルを替えて FMIN/FMAX と bin 幅が合わなくなった場合) */
			if(mel_n >= MEL_LUT_MAX) return E_NOMEM;
			start = 0;
			stop  = 0;
			mel_lut[mel_n++] = 0.0f;
		}
		mel_start[j] = (UH)start;
		mel_stop[j]  = (UH)stop;
	}
	return E_OK;
}

EXPORT ER preproc_init(void)
{
	INT	i, j, r;
	double	th;
	ER	er;

	/* 周期ハン窓 (librosa の get_window('hann', 400) = 0.5 - 0.5 cos(2 pi n / 400)) */
	for(i = 0; i < AED_PREPROC_WIN; i++) {
		win_tbl[i] = (float)(0.5 - 0.5 * cos(2.0 * PI_D * (double)i / (double)AED_PREPROC_WIN));
	}

	/* ツイドル W = exp(-2 pi i k / 512) */
	for(i = 0; i < N_FFT / 2; i++) {
		th       = 2.0 * PI_D * (double)i / (double)N_FFT;
		tw_re[i] = (float)cos(th);
		tw_im[i] = (float)(-sin(th));
	}

	/* ビット反転 (9bit) */
	for(i = 0; i < N_FFT; i++) {
		r = 0;
		for(j = 0; j < N_STAGES; j++) {
			if((i & (1 << j)) != 0) r |= 1 << (N_STAGES - 1 - j);
		}
		brev[i] = (UH)r;
	}

	er = mel_init();
	if(er != E_OK) return er;

	ready = TRUE;
	return E_OK;
}

EXPORT BOOL preproc_ready(void)
{
	return ready && quant_set;
}

EXPORT void preproc_set_quant(float scale, INT zp)
{
	if(scale <= 0.0f) return;
	q_scale   = scale;
	q_zp      = zp;
	inv_scale = 1.0f / scale;
	quant_set = TRUE;
}

EXPORT BOOL preproc_quant(float *scale, INT *zp)
{
	if(!quant_set) return FALSE;
	*scale = q_scale;
	*zp    = q_zp;
	return TRUE;
}

EXPORT UINT preproc_mel_coefs(void)
{
	return mel_n;
}

/* ---------------------------------------------------------------- */
/* FFT                                                               */
/* ---------------------------------------------------------------- */

/*
 * 512 点の複素 FFT (radix-2、周波数間引きの入力並べ替え + バタフライ 9 段)。in-place。
 * 実信号を fft_re に入れ fft_im は 0 にしておく (実数専用の分解はしない。CMSIS-DSP は使わない)
 */
LOCAL void fft512(void)
{
	INT	i, j, k, len, half, step, a, b;
	float	wr, wi, ur, ui, vr, vi, xr, xi, t;

	for(i = 0; i < N_FFT; i++) {
		j = (INT)brev[i];
		if(j > i) {
			t = fft_re[i]; fft_re[i] = fft_re[j]; fft_re[j] = t;
			t = fft_im[i]; fft_im[i] = fft_im[j]; fft_im[j] = t;
		}
	}

	for(len = 2; len <= N_FFT; len <<= 1) {
		half = len >> 1;
		step = N_FFT / len;
		for(i = 0; i < N_FFT; i += len) {
			for(j = 0; j < half; j++) {
				k  = j * step;
				wr = tw_re[k];
				wi = tw_im[k];
				a  = i + j;
				b  = a + half;
				xr = fft_re[b];
				xi = fft_im[b];
				vr = xr * wr - xi * wi;
				vi = xr * wi + xi * wr;
				ur = fft_re[a];
				ui = fft_im[a];
				fft_re[a] = ur + vr;
				fft_im[a] = ui + vi;
				fft_re[b] = ur - vr;
				fft_im[b] = ui - vi;
			}
		}
	}
}

/* ---------------------------------------------------------------- */
/* 1列分                                                             */
/* ---------------------------------------------------------------- */

/*
 * pcm の 400 サンプルから、量子化した log-mel 64 個を out[0], out[stride], ... に書く。
 * stride を AED_PREPROC_COLS にすると ST と同じ転置 (p_spectro[i + 96*j]) になる
 */
LOCAL void column(const H *pcm, B *out, INT stride)
{
	INT	i, j, b, q;
	float	acc, v;
	const float	*c;

	/* /32768 して窓を掛け、左右にゼロを詰める (ST は詰めてから中央 400 に窓を掛ける。同じ) */
	memset(fft_re, 0, sizeof(fft_re));
	memset(fft_im, 0, sizeof(fft_im));
	for(i = 0; i < AED_PREPROC_WIN; i++) {
		fft_re[PAD_L + i] = ((float)pcm[i] / 32768.0f) * win_tbl[i];
	}

	fft512();

	/* 振幅スペクトル |X| 257 本 (2乗しない。MAGNITUDE) */
	for(b = 0; b < N_BINS; b++) {
		mag[b] = sqrtf(fft_re[b] * fft_re[b] + fft_im[b] * fft_im[b]);
	}

	for(j = 0; j < AED_PREPROC_MELS; j++) {
		/* メルフィルタ (start〜stop の内積)。Ref = 1.0 なので割り算は省く */
		c   = &mel_lut[mel_off[j]];
		acc = 0.0f;
		for(b = (INT)mel_start[j]; b <= (INT)mel_stop[j]; b++) {
			acc += (*c++) * mag[b];
		}

		/* log の前に 0 以下を FLT_MIN へ。dB ではなく自然対数 (TopdB の切り捨ては無し) */
		if(acc <= 0.0f) acc = FLT_MIN;
		v = logf(acc);

		/* int8 = SSAT(roundf(v * (1/scale) + zero_point), 8) */
		q = (INT)roundf(v * inv_scale + (float)q_zp);
		if(q > 127) q = 127;
		else if(q < -128) q = -128;
		out[j * stride] = (B)q;
	}
}

EXPORT UW preproc_run(const H *pcm, B *out)
{
	UW	t0;
	INT	i;

	if(!ready || !quant_set) return 0;

	t0 = NOW();
	for(i = 0; i < AED_PREPROC_COLS; i++) {
		column(&pcm[AED_PREPROC_HOP * i], &out[i], AED_PREPROC_COLS);
	}
	return trace_cyc_to_us((UW)(NOW() - t0));
}
