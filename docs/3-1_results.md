# 3-1 対照実験の結果（A 本番 / B 優先度逆転 / D 推論を遅くする）

`uv run scripts/analyze_31.py` が `logs/` の UART ログから生成した表。数字は手で書き換えない。
条件の切替と期待結果は CLAUDE.md「対照実験のスイッチ」、切替は `npu/infer_task.h` の
`INFER_PRIO_INVERT` / `INFER_SLOW_X`。時間の単位はログのまま（us、ns）。

使ったログ（scripts/analyze_31.py の PINNED で条件ごとに 1 本に固定）:

- A 本番: `uart_20260927_005956.log`（production）
- B 優先度逆転: `uart_20260927_011552.log`（prio-invert）
- D 推論を遅くする: `uart_20260927_013950.log`（slow-infer）

| 項目 | A 本番 | B 優先度逆転 | D 推論を遅くする |
|---|---|---|---|
| ログ | uart_20260927_005956.log | uart_20260927_011552.log | uart_20260927_013950.log |
| CONFIG | A production (INFER_PRIO_INVERT=0 INFER_SLOW_X=1) | B prio-invert (INFER_PRIO_INVERT=1 INFER_SLOW_X=1) | D slow-infer (INFER_PRIO_INVERT=0 INFER_SLOW_X=10) |
| CONFIG → READY / CONFIG → FINAL | 0:23 / 10:21 | 0:24 / 10:29 | 0:23 / 10:23 |
| 窓 取り出し / 期待 | 642 / 642 | 600 / 600 | 558 / 642 |
| tap overrun / torn / skipped | 0 / 0 / 0 | 0 / 0 / 0 | 66 / 0 / 1283344 |
| tap seam ok / NG | 641 / 0 | 599 / 0 | 491 / 0 |
| tap lag max (下限値の数) | 380us (0) | 340us (0) | 1077361us (67) |
| tap write max / avg (回数) | 7768ns / 5325ns (38548) | 413862398ns / 85735ns (36003) | 8730ns / 5300ns (38536) |
| audio under / over / late | 0 / 0 / 0 | 2078 / 1200 / 1200 | 0 / 0 / 0 |
| aed run / err | 642 / 0 | 600 / 0 | 558 / 0 |
| preproc max / avg | 69056us / 69012us | 68643us / 68627us | 69205us / 69122us |
| infer max / avg | 41116us / 41046us | 41003us / 40979us | 41178us / 41046us |
| 窓 1 つの実測 max / avg (前処理+推論+空回し) | - | - | 1102930us / 1064393us |
| 空回し n / max / avg / skipped | - | - | 537 / 992637us / 991539us / 21 |
| notify out / held / offlist / gated | 199 / 302 / 141 / 0 | 293 / 178 / 129 / 0 | 264 / 226 / 68 / 0 |
| notify lat max (下限値の数) | 112179us (0) | 111679us (0) | 2177243us (32) |
| log sent / dropped | 4347 / 0 | 4403 / 0 | 271 / 3851 |
| log lag max | 127547us | 127797us | 7030418us |
| disp sent / dropped | 129 / 0 | 208 / 0 | 12 / 167 |
| disp max / avg | 101903us / 31181us | 542712us / 44134us | 7011249us / 2187859us |
| JSON lat_ms min / median / max (行数、READY 以降) | 112 / 112 / 112 (188) | 112 / 112 / 112 (278) | 1104 / 1640.5 / 2157 (8) |
| 画面 通知から描画まで median / max (行数、READY 以降) | 22850us / 101903us (122) | 33045us / 542712us (198) | 4262165us / 7011249us (6) |
| in Hz 中央値 / out Hz 中央値 (行数、READY 以降) | 16128 / 16000 (591) | 14848 / 15200 (599) | 16128 / 16000 (17) |
| PASSTHROUGH STOPPED / tap: window lost の行数 (ログ全体) | 1 / 0 | 1 / 0 | 0 / 1 |

読み方:

- 「窓 取り出し / 期待」の期待は tap リングに書いたサンプル数から見た窓の数。取り出しが少ない分は上書きで読み飛ばした窓（overrun）。
- audio の under は出力側で pcm_fifo が足りず無音を埋めた回数、over は入力側で捨てた回数（HALF と FULL が同時に立った分を含む）、late は同じ側の HALF と FULL が同時に立っていた＝1 周期分間に合わなかった回数（audio_task.c）。JSON 行の under / over / late と同じカウンタ。
- D の log lag max と disp max / avg は reporter・表示タスクが 10 分間動けなかった間の待ちで、DWT の差分が 7.16 秒で折り返すので目安にしかならない（CLAUDE.md）。飢えの証拠は log dropped と disp dropped。
- disp max は READY 直後の 1 件で立ちやすい（起動確認の直後は表示タスクが後回しになる）。定常の描画時間は「画面 通知から描画まで」の中央値で見る。
- D では PASSTHROUGH STOPPED ブロックは満杯のキューに入らず出ない（audio の値は FINAL の行にある）。tap: window lost も大半が捨てられるので、上書きの回数は tap overrun で見る。
- JSON の lat_ms は窓がそろってから JSON をレポータに渡すまで。UART の待ちは含まない（それは log lag max）。
- tap write max は壁時計。B では tap_ring_write の中の tk_sig_sem で推論タスクに横取りされるので、memcpy の時間ではなく横取りの長さが出る。

使わなかったログ:

- `uart_20260927_020751.log`（CONFIG: D slow-infer）: 表に使うログを固定している（PINNED） ― windows 557/642, overrun 67, audio 0/0/0, log dropped 3846, disp max 7015440us, JSON lat_ms median 1714

CONFIG: 行の無いログ（スイッチを入れる前のもの）11 本は読み飛ばした。

## 走行集計（通知の件数と 1 時間あたりの件数）

3.5 の誤報対策の前後を同じ形式で比べるためのもの。通知の総数は FINAL の notify out、クラス別は JSON 行の数。

```
走行集計: uart_20260927_005956.log  CONFIG: A production (INFER_PRIO_INVERT=0 INFER_SLOW_X=1)
  走行時間 READY→FINAL: 9:57 (597.9 s)
  通知 (JSON 行、FINAL の notify out): 199 件 = 1198 件/時
    クラス別: sneezing 85 / unknown 69 / crackling_fire 37 / dog 4（ログ上の JSON 行は 195。差 4 は reporter が捨てた行 = READY 前のダンプ中、D では満杯のキュー）
    検出 (unknown を除く): 126 件 = 759 件/時 / unknown: 69 件
  gated 0 / held 302 / offlist 141  (FINAL。gated=音量の門で止めた窓、held=unknown が続いて出さなかった窓、offlist=通知対象外のクラスの窓)
走行集計: uart_20260927_011552.log  CONFIG: B prio-invert (INFER_PRIO_INVERT=1 INFER_SLOW_X=1)
  走行時間 READY→FINAL: 10:05 (605.2 s)
  通知 (JSON 行、FINAL の notify out): 293 件 = 1743 件/時
    クラス別: sneezing 127 / unknown 84 / crackling_fire 76 / crying_baby 1（ログ上の JSON 行は 288。差 5 は reporter が捨てた行 = READY 前のダンプ中、D では満杯のキュー）
    検出 (unknown を除く): 204 件 = 1213 件/時 / unknown: 84 件
  gated 0 / held 178 / offlist 129  (FINAL。gated=音量の門で止めた窓、held=unknown が続いて出さなかった窓、offlist=通知対象外のクラスの窓)
走行集計: uart_20260927_013950.log  CONFIG: D slow-infer (INFER_PRIO_INVERT=0 INFER_SLOW_X=10)
  走行時間 READY→FINAL: 9:59 (599.6 s)
  通知 (JSON 行、FINAL の notify out): 264 件 = 1585 件/時
    クラス別: sneezing 10 / unknown 4（ログ上の JSON 行は 14。差 250 は reporter が捨てた行 = READY 前のダンプ中、D では満杯のキュー）
    検出 (unknown を除く): 10 件 = 60 件/時 / unknown: 4 件
  gated 0 / held 226 / offlist 68  (FINAL。gated=音量の門で止めた窓、held=unknown が続いて出さなかった窓、offlist=通知対象外のクラスの窓)
```

## 付録: FINAL ブロックの本文（ログのまま）

### A 本番: `uart_20260927_005956.log`

```
tap: no window for 2000 ms (audio stopped?)
tap final: windows=642 (expected 642 from 9868288 samples) overrun=0 torn=0 skipped=0 seam ok=641 NG=0
  tap lag max=380us (0 of them lower bounds) | tap write max=7768ns avg=5325ns (38548 calls)
  audio under=0 over=0 late=0 | aed run=642 err=0 (last er=0)
  preproc max=69056us avg=69012us | infer max=41116us avg=41046us
  notify out=199 held=302 offlist=141 gated=0 lat max=112179us (0 lower bounds)
  log sent=4347 dropped=0 lag max=127547us
  disp sent=129 dropped=0 max=101903us avg=31181us
```

### B 優先度逆転: `uart_20260927_011552.log`

```
tap: no window for 2000 ms (audio stopped?)
tap final: windows=600 (expected 600 from 9216768 samples) overrun=0 torn=0 skipped=0 seam ok=599 NG=0
  tap lag max=340us (0 of them lower bounds) | tap write max=413862398ns avg=85735ns (36003 calls)
  audio under=2078 over=1200 late=1200 | aed run=600 err=0 (last er=0)
  preproc max=68643us avg=68627us | infer max=41003us avg=40979us
  notify out=293 held=178 offlist=129 gated=0 lat max=111679us (0 lower bounds)
  log sent=4403 dropped=0 lag max=127797us
  disp sent=208 dropped=0 max=542712us avg=44134us
```

### D 推論を遅くする: `uart_20260927_013950.log`

```
tap: no window for 2000 ms (audio stopped?)
tap final: windows=558 (expected 642 from 9865216 samples) overrun=66 torn=0 skipped=1283344 seam ok=491 NG=0
  tap lag max=1077361us (67 of them lower bounds) | tap write max=8730ns avg=5300ns (38536 calls)
  audio under=0 over=0 late=0 | aed run=558 err=0 (last er=0)
  preproc max=69205us avg=69122us | infer max=41178us avg=41046us
  slow x10: window max=1102930us avg=1064393us | spin n=537 max=992637us avg=991539us skipped=21
  notify out=264 held=226 offlist=68 gated=0 lat max=2177243us (32 lower bounds)
  log sent=271 dropped=3851 lag max=7030418us
  disp sent=12 dropped=167 max=7011249us avg=2187859us
```
