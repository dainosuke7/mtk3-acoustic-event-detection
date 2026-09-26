#ifndef AED_NOTIFY_H
#define AED_NOTIFY_H

#include <tk/tkernel.h>

/*
 * 判定と通知 (Phase 1 タスク9)
 *
 * 推論の出力 (softmax 後の確率 x10) から1位のクラスを決め、UART に1行の JSON を出し、
 * ボードの LED を点ける。呼ぶのは推論タスク (infer_task.c) だけ。
 *
 * JSON (1行):
 *   {"win":123,"cls":"dog","p":0.87,"lat_ms":1053,"under":0,"over":0,"late":0}
 *     win     窓の通し番号
 *     cls     クラス名。確率が AED_OOD_THR 以下なら "unknown"
 *     p       1位の確率 (小数2桁)
 *     lat_ms  窓の最後のサンプルが tap_ring に書かれてから、この行をレポータに渡すまで
 *     under / over / late  パススルーの累計 (音が途切れていないことの裏付け)
 *
 * unknown は毎窓出さない。unknown に変わったときだけ1回出す (静かな時間に毎窓出ても
 * 情報が増えず、UART と行の待ちを食うだけなので)。検出は毎窓出す。
 */

#define AED_CLASSES		(10)
#define AED_CLS_UNKNOWN		(-1)

/*
 * 確率がこれを超えたときだけクラスを名乗る。ST の CTRL_X_CUBE_AI_OOD_THR
 * (ai_model_config.h.aed = 0.5F) と同じ値・同じ向きの比較 (audio_bm.c:488 が max > THR)
 */
#define AED_OOD_THR		(0.5f)

/*
 * 通知するクラス番号 (モデルの出力順)。屋内で知らせたい音に絞る。
 * ここに無いクラスが1位になったときは LED も JSON も出さない。推論は毎窓続けるので、
 * 落とした数は notify_stats() の offlist に出る (誤報の内訳はこの数で見る)。
 * crackling_fire (2) は 1-Ex の対照試験 (1-Ex_対照試験結果.docx 4.2) で外した (2026-09-27):
 * 紙を丸める・手拍子・マグを置く生活音を crackling_fire と通知し (誤報 7 件中 6 件)、
 * 火の音のクリップは 2 本中 1 本しか拾えなかった (p=0.55)。dog 4 / crying_baby 3 / sneezing 9 の 3 クラス
 *
 * NOTIFY_CLASS_NAMES は上の番号が指すべきクラス名 (同じ順)。モデルを差し替えて出力順が
 * 変わると番号がずれるので、notify_init() がクラス名と照合して食い違いを報告する
 */
#define NOTIFY_CLASSES		{ 4, 3, 9 }
#define NOTIFY_CLASS_NAMES	{ "dog", "crying_baby", "sneezing" }

/*
 * 音量の門。窓のピーク (int16 の絶対値の最大) がこの dBFS 未満なら通知しない
 * (unknown と同じ扱いにする)。-99 で切 (既定)。
 * RMS ではなくピークで見るのは、短く鋭い音 (犬の1声、くしゃみ) を落とさないため。
 * 止めた数は notify_stats() の gated に出る
 */
#define NOTIFY_GATE_PEAK_DBFS	(-99)

/*
 * まだ入れていないもの (対照試験 scripts/aed_play_test.py の結果を見てから決める):
 *   確率のしきい値を 0.5 より上げる / 同じクラスが続いたときだけ通知する
 */

/* 検出したとき LED を点けておく時間 */
#define NOTIFY_LED_MS		(1000)

/*
 * LED を消すためのアラームを作り、LED を消しておく。
 * 推論タスクが最初の窓を処理する前に1回 (usermain から) 呼ぶ
 */
EXPORT ER notify_init(void);

/*
 * 推論の出力から1位を決める。*p に1位の確率を返す。
 * 戻り値: クラス番号 (0〜AED_CLASSES-1) / AED_CLS_UNKNOWN 確率が閾値以下
 */
EXPORT INT notify_decide(const float *out, float *p);

/* クラス名 ("unknown" を含む。範囲外は "?") */
EXPORT const char *notify_class_name(INT cls);

/*
 * 判定の結果を通知する (JSON 1行 + LED)。
 *   peak:      窓のピーク (int16 の絶対値の最大)。音量の門に使う
 *   t_ready:   窓がそろった時刻 (DWT。TAP_WIN_INFO の t_ready)
 *   lat_exact: t_ready が実測か (FALSE なら下限値。TAP_WIN_INFO の lag_exact)
 */
EXPORT void notify_window(UW win, INT cls, float p, UW peak, UW t_ready, BOOL lat_exact);

/*
 * *emitted 出した行数 / *held unknown が続いて出さなかった窓の数 /
 * *offlist 通知対象外のクラスで出さなかった窓の数 /
 * *gated 音量の門で止めた窓の数 /
 * *lat_max_us 遅れの最大 / *lat_loose 下限値だった回数
 */
EXPORT void notify_stats(UW *emitted, UW *held, UW *offlist, UW *gated,
			UW *lat_max_us, UW *lat_loose);

#endif	/* AED_NOTIFY_H */
