# 報告に使った UART ログ

各報告書の数字の元になった UART ログと対照試験の再生ログです（`logs/` は git 管理外なので、使ったものだけをここに写しました）。
UART ログは `powershell scripts/log.ps1 -Timestamp` の出力で、各行の先頭 `HH:mm:ss.fff` は PC の時計です（9 本すべてに付いている）。
`play_*.txt` は `scripts/aed_play_test.py` の再生ログで、同じ PC の時計を持つので UART ログと突き合わせられます。

数字の出し方（`<log>` はこのフォルダのファイル）:

```bash
uv run scripts/analyze_31.py docs/logs/<log> --out -          # 走行集計（READY→FINAL の時間、通知の件数と件/時、クラス別）と FINAL の本文
uv run scripts/analyze_31.py                                   # docs/3-1_results.md の表を作り直す（PINNED の 3 本を logs/ から読む）
uv run scripts/notify_replay.py docs/logs/<log>                # 現行の通知規則（p>0.7、peak −30 dBFS、相対 +10 dB、対象クラス）を PC で再生
uv run scripts/notify_replay.py docs/logs/<log> --model fsd50k # FSD50K 版の対象クラスと Speech 30 秒のクールダウンで
```

FINAL ブロックはログの末尾近くの `FINAL` 見出しの後の 8 行（tap final / tap lag / audio / preproc・infer / notify / log / disp）。
下の「行」はその見出しの行番号です。

## 一覧

| ファイル | 走行 | 報告と数字 | 該当行・スクリプト |
|---|---|---|---|
| `uart_20260924_180849.log` + `play_20260924_181041.txt` | 対照試験 1-Ex の最初の走行（2026-09-24）。ESC-10、対策前の規則（通知 4 クラス dog / crying_baby / crackling_fire / sneezing、しきい値 0.5、音量の門なし）。CONFIG 行を入れる前。10 分 | [1-Ex_対照試験結果.docx](../1-Ex_対照試験結果.docx): 無音 60 秒で誤報 0、対象クリップ 8 本中 7 本を検出（3 クラスでは 6/6。1 本目の crackling_fire だけ出ない）、対象外の clock_tick 2 本は通知なし、遅延 1.1〜1.9 s（平均 1.6 s）、生活音 12 回で誤報 1（2 回目の椅子 18:16:46 → sneezing 0.57。咳ばらいは別枠、crackling_fire は後で対象から外したので数えない）。紹介資料 6-3 の「対策前は 12 回中 1」。CLAUDE.md「実測の基準（2026-09-24）」も同じログ | FINAL 行 8371（窓 641/641、audio 0/0/0、`preproc avg=69815us`・`infer avg=40990us`、`notify out=82 held=316 offlist=243 gated=0`、log dropped 0）。クリップと生活音ごとの結果は、play ログの時刻から 3 秒以内の JSON 行で見る |
| `uart_20260927_005956.log` | 対照実験 3-1 条件 A（本番）。ESC-10、10 分。しきい値 0.5・音量の門なしの時点 | [3-1_results.md](../3-1_results.md) の A 列: 窓 642/642、audio under/over/late 0/0/0、tap overrun 0、JSON lat_ms 中央値 112 ms、log dropped 0。README「10 分…音声の取りこぼし 0」 | FINAL（末尾）。`scripts/analyze_31.py` の `PINNED["A"]` |
| `uart_20260927_011552.log` | 3-1 条件 B（推論タスクを task_pcm より上に）。10 分 | 3-1_results.md の B 列: audio under=2078 over=1200 late=1200、窓 600/600、in Hz 中央値 14848、tap write max 414 ms。ログ・LCD は止まらない | FINAL（末尾）。`PINNED["B"]` |
| `uart_20260927_013950.log` | 3-1 条件 D（前処理+推論を 10 倍に）。10 分 | 3-1_results.md の D 列と [3-1D_対照試験_結果報告.docx](../3-1D_対照試験_結果報告.docx): 窓 558/642、overrun 66、audio 0/0/0、log dropped 3851、disp dropped 167、JSON lat_ms 中央値 1640 ms、`slow x10:` 行 | FINAL（末尾。`slow x10` の行を含む）。`PINNED["D"]` |
| `uart_20260927_130250.log` | 誤報対策（3.5）の前の静かな部屋 10 分。ESC-10、しきい値 0.5、門なし。CONFIG 行なし（起動後に記録を始めた） | CLAUDE.md「3.5」の根拠: 通知 271 件 / 9:57、うち sneezing 199 件、p の分布（0.5 台 63 / 0.6 台 63 / 0.7 台 49 / 0.8 台 18 / 0.9 台 6）。現行の規則で再生すると 3 件 | FINAL 行 4573（`notify out=271 held=189 offlist=182 gated=0`）。`analyze_31.py --out -` の走行集計（271 件 = 1632 件/時（unknown を除く検出は 202 件 = 1,216 件/時。報告書と紹介資料はこちら）、sneezing 199 / unknown 68 / dog 2 / crying_baby 1）。`notify_replay.py` → 3 |
| `uart_20260927_142118.log` | 対照試験 1-Ex の再走行。ESC-10、しきい値 0.7 + 絶対ゲート（peak −30 dBFS）の実装確認。途中で止めたので FINAL なし | CLAUDE.md「3.5」「3.5-2」: 起動ログの `notify:` 行を実機で確認、READY 以降の検出 24 件が相対ゲートを足すと 19 件（落ちる 5 窓は Δ3.0〜7.3 dB、クリップの検出は残る） | `analyze_31.py --out -`（JSON 行 36: crying_baby 10 / sneezing 10 / dog 4 / unknown 12）。`notify_replay.py` → 19（ボードの印との照合 一致 45 / 不一致 0） |
| `uart_20260927_161828.log` | 1 時間の連続走行（3-5）。ESC-10、絶対ゲートあり・相対ゲートなし | [3-5_1時間連続走行_結果報告.docx](../3-5_1時間連続走行_結果報告.docx)、README「1 時間の連続走行で取りこぼし 0」: 窓 3854/3854、audio 0/0/0、log dropped 0、disp dropped 0。CLAUDE.md「3.5-2」の根拠: 検出 82 件/61 分（80 件/時）が全部暗騒音の窓で、相対ゲートで 82 → 4 | FINAL 行 28328（`notify out=157 held=3436 offlist=261 gated=427`）。走行集計（検出 82 件 = 80 件/時: sneezing 76 / crying_baby 6）。`notify_replay.py` → 4 |
| `uart_20260928_002702.log` + `play_20260928_002821.txt` | ESC-10、本番の規則（相対ゲート込み）。前半は 1-Ex の再生試験（play ログ。クリップ 10 本 + 生活音 12 回）、後半は夜の生活空間（00:35〜01:08）。41 分（`PT_REPORT_COUNT` 4800） | README「FSD50K 版について」の比較値: 再生試験の後（00:35:00 〜 FINAL 01:08:54 の 33.9 分）の検出 2 件 = 3.5 件/時（sneezing 2、00:49:10 と 01:07:18）。生活音 12 回のうち通知は咳ばらい 2 回（sneezing、00:32:44 / 00:34:14）だけ | FINAL 行 20317（窓 2567/2567、audio 0/0/0、`notify out=28 gated_abs=40 gated_rel=20`、log dropped 0、disp dropped 0）。走行集計（試験区間込みで検出 18 件 = 26 件/時: crying_baby 7 / sneezing 7 / dog 4）。`notify_replay.py` → 19。区間の件数は `awk '$1 >= "00:35:00"' <log> \| grep '{"win"' \| grep -v unknown` |
| `uart_20260928_064258.log` + `play_20260928_064446.txt` | FSD50K 版、同じ規則。前半は 1-Ex（`--model fsd50k`。クリップ 8 本 + 生活音 14 回）、後半は朝の生活空間。41 分 | README「FSD50K 版について」: 推論 3.5 ms（`infer max=3626us avg=3553us`）、重み 148 KB を AXISRAM4（起動ログ `aed model:` 行）、同じ検証手順（NPU 自己テスト・前処理の突き合わせ・1-Ex・連続走行で audio 0/0/0）。再生試験の後（06:51:30 〜 FINAL 07:25:31 の 34.0 分）の検出 142 件 = 250 件/時（Glass 138 / Knock 4）。生活音 14 回のうち対象外の 10 回（手拍子・紙・咳ばらい・椅子・マグ × 2）で 9 回を Knock か Glass として通知（止まったのは 2 回目の紙 06:50:06 だけ、相対ゲート）。対象外のクリップ clock_tick 2 本も Glass / Knock（06:46:38〜43、06:47:37〜42） | FINAL 行 20831（窓 2569/2569、audio 0/0/0、`notify out=345 gated_abs=780 gated_rel=614 cooldown=17`、log dropped 0、disp dropped 0）。走行集計（試験区間込みで検出 208 件 = 306 件/時: Glass 167 / Knock 31 / Crying_and_sobbing 7 / Speech 3）。`notify_replay.py --model fsd50k` → 207（照合 一致 2549 / 不一致 1）。区間の件数は `awk '$1 >= "06:51:30"' <log> \| grep '{"win"' \| grep -v unknown` |

## 補足

- 3-1 の表（`docs/3-1_results.md`）は `scripts/analyze_31.py` が `logs/` の PINNED の 3 本から作ります。このフォルダの写しで作り直すときは引数に 3 本を渡します（`uv run scripts/analyze_31.py docs/logs/uart_20260927_005956.log docs/logs/uart_20260927_011552.log docs/logs/uart_20260927_013950.log --out -`）。
- 1-Ex の最初の走行（`docs/1-Ex_対照試験結果.docx`、2026-09-24）は `uart_20260924_180849.log` で、対策前の判定規則（しきい値 0.5、門なし、通知 4 クラス）のものです。`play_20260924_181041.txt` は 3 行目のコメント（クリップの置き場所）だけ、開発 PC の絶対パスを同じ場所の相対パス `..\ref\ESC-50` に書き換えました（ほかの 2 本の play ログと同じ書き方。ほかの行は元のまま）。対策後の同じ試験が `uart_20260927_142118.log`（絶対ゲート）と `uart_20260928_002702.log`（相対ゲート込み）です。
- play ログと UART ログは同じ PC の時計ですが、通知は窓（0.96 秒）がそろってから出るので、音の開始から JSON まで 1.1〜1.9 秒ずれます。3 秒前に予告が出る生活音は人が早めに出すこともあります。
