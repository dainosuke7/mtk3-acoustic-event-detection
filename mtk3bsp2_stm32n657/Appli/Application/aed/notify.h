#ifndef AED_NOTIFY_H
#define AED_NOTIFY_H

#include <tk/tkernel.h>

/*
 * 判定と通知 (Phase 1 タスク9。門は 3.5 / 3.5-2)
 *
 * 推論の出力 (softmax 後の確率 x10) から1位のクラスを決め、門 (notify_gate) を通った窓だけ
 * UART に1行の JSON を出し、ボードの LED を点け、画面に出す。呼ぶのは推論タスク (infer_task.c) だけ。
 *
 * 門 (すべて AND。上から順に見て、最初に引っかかった理由が NOTIFY_VERDICT になる):
 *   p > AED_OOD_THR                          でなければ NOTIFY_BELOW_THR (unknown)
 *   NOTIFY_CLASSES に入っている              でなければ NOTIFY_OFFLIST
 *   peak >= NOTIFY_GATE_PEAK_DBFS            でなければ NOTIFY_GATED_ABS
 *   rms_db >= floor + NOTIFY_GATE_REL_DB     でなければ NOTIFY_GATED_REL (floor は下の「相対の門」)
 *   → NOTIFY_PASS
 * 対象外のクラスを門より先に外すので、gated_abs + gated_rel + PASS = 対象クラスで閾値を超えた窓、
 * offlist = 対象外のクラスが 1位だった窓 (ピークによらない)。3.5 の gated (門が先) とは数え方が違う。
 * 同じ規則を PC で再生するのが scripts/notify_replay.py (ログの win 行と -> 行から)。
 *
 * JSON (1行):
 *   {"win":123,"cls":"dog","p":0.87,"lat_ms":1053,"under":0,"over":0,"late":0}
 *     win     窓の通し番号
 *     cls     クラス名。門で止めた窓 (BELOW_THR / GATED_ABS / GATED_REL) は "unknown"
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
 * 確率がこれを超えたときだけクラスを名乗る。比較の向きは ST の CTRL_X_CUBE_AI_OOD_THR
 * (ai_model_config.h.aed = 0.5F、audio_bm.c:488 が max > THR) と同じ。値は 0.5 から 0.7 に上げた
 * (2026-09-27、3.5)。根拠: 静かな部屋 10 分 (logs/uart_20260927_130250.log、642 窓) で sneezing を
 * 199 窓通知し、その p は 0.5 台 63 / 0.6 台 63 / 0.7 台 49 / 0.8 台 18 / 0.9 台 6。下の peak gate
 * -30dBFS と組み合わせた残数は 0.5 で 17、0.7 で 6、0.8 で 1。残る 6 件は peak -10〜-25dBFS で
 * 実際に音があった窓。0.8 にしないのは 1-Ex で sneezing のクリップ 1 本が p=0.76 だったため
 */
#define AED_OOD_THR		(0.7f)

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
 * 音量の門 (絶対)。窓のピーク (int16 の絶対値の最大) がこの dBFS 未満なら通知しない
 * (unknown と同じ扱いにする)。-99 で切。
 * RMS ではなくピークで見るのは、短く鋭い音 (犬の1声、くしゃみ) を落とさないため。
 * 止めた数は notify_stats() の gated_abs に出る。
 * -30dBFS の根拠 (2026-09-27、3.5): 上の 10 分ログの誤報の窓は rms -48dBFS 前後・peak -37〜-35dBFS で
 * 10 分間一定 (冷蔵庫の定常音と推定)。ゲートなしでは 202 窓、-30dBFS でしきい値 0.5 のまま 17 窓、
 * 0.7 と組み合わせて 6 窓 (いずれも実際に音があった窓)。
 * 限界: 音量で切るので、冷蔵庫に近い設置や換気扇では -30dBFS を超えて通る。根本対策は背景音クラスを持つモデル。
 * その限界がそのまま出たのが下の相対の門の背景
 */
#define NOTIFY_GATE_PEAK_DBFS	(-30)

/*
 * 暗騒音に対する相対の門 (2026-09-27、3.5-2)。
 * 窓ごとの rms_db (= 20*log10(rms/32768)。rms は win 行の rms= と同じ整数) を直近
 * NOTIFY_FLOOR_WINDOWS 窓ぶんリングに持ち、その NOTIFY_FLOOR_PERCENTILE パーセンタイル
 * (60 個を昇順にして 6 番目) を floor とする。通すのは rms_db >= floor + NOTIFY_GATE_REL_DB のときだけ。
 * 履歴が NOTIFY_FLOOR_WINDOWS に満たない間 (起動から約 1 分) はこの条件を省く (絶対の門だけ)。
 * floor は判定のあとに更新する (現在の窓は自分の floor に入らない)。止めた数は notify_stats() の gated_rel。
 *
 * 根拠: 1 時間走行 logs/uart_20260927_161828.log で通知 82 件/61 分 = 80 件/時。全件 rms -44〜-33dBFS の
 * 暗騒音の窓で、peak が -30dBFS をわずかに超えたもの (82 件中 54 件が -30〜-27)。この部屋のこの時間帯は
 * 暗騒音の peak が -31〜-28dBFS にあり、固定の門 -30 が暗騒音の上に乗っていた。
 * 同じログにこの条件を足すと 82 → 4 (残る 4 件は Δ10〜18dB の実際の音)。1-Ex の再走行
 * (logs/uart_20260927_142118.log、READY 以降) は 24 → 19 で、落ちる 5 窓は Δ3.0〜7.3dB の暗騒音の窓
 * (クリップの検出は dog / crying_baby / sneezing とも残る)。対策前の 10 分ログは 6 → 3 (scripts/notify_replay.py)。
 * 10 パーセンタイルにするのは、鳴っている最中の窓 (60 窓のうち数窓〜十数窓) に floor が引きずられないため。
 * 60 窓 (約 58 秒) は冷蔵庫の運転/停止より短く、鳴り続ける音 (赤ん坊の泣き声) より長い。
 * 限界: 54 窓 (約 52 秒) を超えて鳴り続ける音は floor を押し上げ、以降その音は相対の門で止まる
 */
#define NOTIFY_FLOOR_WINDOWS	(60)
#define NOTIFY_FLOOR_PERCENTILE	(10)
#define NOTIFY_GATE_REL_DB	(10)
#define NOTIFY_FLOOR_NONE	(-1000.0f)	/* notify_floor() の「履歴がまだ足りない」 */

/*
 * まだ入れていないもの: 同じクラスが続いたときだけ通知する
 */

/* 検出したとき LED を点けておく時間 */
#define NOTIFY_LED_MS		(1000)

/* 門の結果 (notify_gate の戻り値)。判定の順は notify.h 冒頭 (BELOW_THR → OFFLIST → GATED_ABS → GATED_REL → PASS) */
typedef enum {
	NOTIFY_PASS = 0,	/* 通知する (JSON + LED + 画面) */
	NOTIFY_GATED_ABS,	/* ピークが NOTIFY_GATE_PEAK_DBFS 未満 */
	NOTIFY_GATED_REL,	/* rms_db が floor + NOTIFY_GATE_REL_DB 未満 */
	NOTIFY_BELOW_THR,	/* 1位の確率が AED_OOD_THR 以下 (unknown) */
	NOTIFY_OFFLIST,		/* 通知対象外のクラス */
} NOTIFY_VERDICT;

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

/* 窓の rms (int16 の実効値、整数。win 行の rms=) → dBFS。0 なら -99 (win 行の dBFS と同じ流儀) */
EXPORT float notify_rms_db(UW rms);

/*
 * 門 (純粋関数。状態を持たず、しきい値の定数 (起動時に作る絶対の門の振幅を含む) だけを読む)。
 *   rms_db:   窓の rms の dBFS (notify_rms_db)
 *   peak:     窓のピーク (int16 の絶対値の最大)
 *   p, cls:   notify_decide の結果 (cls は閾値以下なら AED_CLS_UNKNOWN)
 *   floor_db: notify_floor() の値 (NOTIFY_FLOOR_NONE なら相対の門を見ない)
 */
EXPORT NOTIFY_VERDICT notify_gate(float rms_db, UW peak, float p, INT cls, float floor_db);

/* 直近の窓の暗騒音 (dBFS)。履歴が NOTIFY_FLOOR_WINDOWS に満たなければ NOTIFY_FLOOR_NONE */
EXPORT float notify_floor(void);

/* 履歴に窓の rms_db を 1 つ足す (判定のあとに呼ぶ) */
EXPORT void notify_floor_update(float rms_db);

/*
 * 窓 1 つの判定と通知 (notify_floor → notify_gate → notify_floor_update → JSON 1行 + LED + 画面)。
 *   rms, peak: 窓の rms とピーク (int16 の値。win 行と同じ)
 *   t_ready:   窓がそろった時刻 (DWT。TAP_WIN_INFO の t_ready)
 *   lat_exact: t_ready が実測か (FALSE なら下限値。TAP_WIN_INFO の lag_exact)
 * 戻り値: 門の結果。理由の文字列は notify_last_note()
 */
EXPORT NOTIFY_VERDICT notify_window(UW win, INT cls, float p, UW rms, UW peak, UW t_ready, BOOL lat_exact);

/*
 * 直前の notify_window の結果を窓の診断行に付けるための文字列 (先頭に空白 1 つ。無印なら "")。
 *   PASS       " (floor -42.6 Δ+17.0)"  (floor がまだ無ければ " (floor n/a)")
 *   GATED_ABS  " (gated abs)"
 *   GATED_REL  " (gated rel floor -42.6 Δ+6.2)"
 *   それ以外   ""
 * Δ = rms_db - floor
 */
EXPORT const char *notify_last_note(void);

/*
 * *emitted 出した行数 / *held unknown が続いて出さなかった窓の数 /
 * *offlist 通知対象外のクラスで出さなかった窓の数 /
 * *gated_abs 絶対の門で止めた窓の数 / *gated_rel 相対の門で止めた窓の数 /
 * *lat_max_us 遅れの最大 / *lat_loose 下限値だった回数
 */
EXPORT void notify_stats(UW *emitted, UW *held, UW *offlist, UW *gated_abs, UW *gated_rel,
			UW *lat_max_us, UW *lat_loose);

#endif	/* AED_NOTIFY_H */
