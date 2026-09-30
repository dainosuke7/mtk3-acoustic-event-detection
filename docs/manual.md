# 手順書（ビルド・実行・評価）

音響イベント検出基盤（STM32N6570-DK + μT-Kernel 3.0）を、クローン直後の状態から動かして評価するまでの手順です。
上から順に実行すればビルドと起動ができます。コマンドはすべてこのリポジトリに実在するものです。
既存ソフトウェアの一覧は [third_party.md](third_party.md)、設計の詳細は [../CLAUDE.md](../CLAUDE.md)（開発者向けメモ）。

審査用に要点をまとめた手順書は [手順書_音響イベント検出基盤.docx](手順書_音響イベント検出基盤.docx)（PDF 版あり）。

## 1. 必要なもの

| もの | 版・条件 | 用途 |
|---|---|---|
| STM32N6570-DK | 1 台。外部ハードは不要 | 本体 |
| USB Type-C ケーブル | 1 本（ST-LINK/電源/仮想 COM を兼ねる） | 書き込みとログ |
| Windows PC | 本手順は Windows 11 で確認 | 開発・ログ取得 |
| STM32CubeIDE | 2.2.0（GNU Tools for STM32 14.3.rel1 同梱） | ビルド、デバッガ経由の実行 |
| STM32CubeProgrammer | 2.23.0（CubeIDE 2.2.0 に同梱。`C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_*\tools\bin`） | ESC-10 版のモデル重みを外部フラッシュに書くときだけ |
| ST Edge AI Core | 4.0.1（`C:\ST\STEdgeAI\4.0\`） | NPU 向けモデルを生成し直すときだけ（生成物は同梱済み） |
| uv | 0.12 系（Python 3.12 は uv が用意する） | PC 側スクリプト（ログ集計、参照値の生成、対照試験） |
| STM32N6-GettingStarted-Audio | v2.3.0（https://github.com/STMicroelectronics/STM32N6-GettingStarted-Audio） | ESC-10 版の重み hex の入手（3.3 節）と、参照値の生成（9.3 節） |
| ESC-50 | `git clone https://github.com/karolpiczak/ESC-50.git`（`meta/` と `audio/`） | 対照試験 1-Ex の再現と、前処理の参照ヘッダの生成だけ |
| ヘッドホン | 任意 | パススルー音のモニタ（CN15） |

Git Bash（CubeIDE 付属ではなく Git for Windows のもの）で `bash scripts/build.sh` を動かします。

## 2. 構成

### 2.1 信号の流れ

```
PDM マイク U13/U14 ──MDF1 (16 kHz)──GPDMA1 ch0──▶ task_pcm ──▶ pcm_fifo ──GPDMA1 ch2── SAI1 ──▶ WM8904 ──▶ ヘッドホン CN15
                                                    │                                   (パススルー。推論中も途切れない)
                                                    └──▶ tap ring (int16 x 32768, 約 2 秒)
                                                              │ 窓 975 ms (15,600 サンプル) を 960 ms ごと
                                                              ▼
                                       task_infer: log-mel 64x96 (約 69 ms) → NPU 推論 (約 41 ms) → 判定 notify_gate
                                                              │
                                              ┌───────────────┼──────────────────┐
                                              ▼               ▼                  ▼
                                      reporter → UART    赤 LED PG10 (1 秒)   task_lcd → LCD 800x480
                                      (JSON 1 行)                              (クラス名を 3 秒表示)
```

### 2.2 タスク（優先度は小さいほど高い。設計意図は「聞こえる ＞ 知らせる ＞ 見せる」）

| 優先度 | タスク | 役割 | 起動要因・周期 | スタック |
|---|---|---|---|---|
| 1 | usermain（初期タスク） | 初期化とタスク生成。以後は 100 ms 周期で待つだけ | — | 1 KB（BSP2 既定） |
| 5 | task_pcm | DMA 通知を受けて入力変換・リング操作・出力充填 | DMA コールバック → イベントフラグ 4 ビット（MDF 半分 16 ms、SAI 半分 25 ms） | 2 KB |
| 10 | task_audio | パススルーの制御と 500 ms ごとの統計 | 500 ms 周期（`PT_REPORT_INTERVAL_MS`）を `PT_REPORT_COUNT` 回 | 1 KB |
| 10 | task_1 | 生存表示（緑 LED PO1 の点滅） | 500 ms 周期 | 1 KB |
| 15 | task_infer | 前処理 → NPU 推論 → 判定 → 通知。ll_aton を呼ぶ唯一のタスク | 窓の到着（セマフォ。約 960 ms ごと） | 8 KB |
| 20 | reporter | UART 出力の唯一の書き手 | メッセージバッファ（送り手は TMO_POL、満杯なら捨てて数える）。1 秒のレート行は周期ハンドラ | 1.5 KB |
| 25 | task_lcd | LCD 描画 | メッセージバッファ（8 件）。1 秒の生存表示、検出は 3 秒保持 | 1.5 KB |
| 32 | dump | トレースの CSV ダンプ | 20 ms ポーリング | 1 KB |
| — | 周期ハンドラ | HAL_IncTick の代行（10 ms）、レート表示（1000 ms） | — | — |
| — | アラームハンドラ | 赤 LED の消灯（点灯から 1 秒） | — | — |

## 3. 取得・ビルド・重みの書き込み

動作を確認したのは STM32CubeIDE 2.2.0（Windows 11）です。この節のコマンドはすべて Git Bash（Git for Windows）で実行します。

### 3.1 取得

```bash
git clone https://github.com/dainosuke7/mtk3-acoustic-event-detection
cd mtk3-acoustic-event-detection
```

### 3.2 ビルド

使うのは Debug 構成だけです（Release 構成には μT-Kernel と NPU ランタイムのインクルードパス・ライブラリが入っていないので対象外）。

1. STM32CubeIDE 2.2.0 で File > Import > General > Existing Projects into Workspace を開く
2. Select root directory に、clone した中の `mtk3bsp2_stm32n657` フォルダを指定する。Options の
   **Search for nested projects** にチェックを入れ、**Copy projects into workspace** は外す
   （コピーすると `../../Drivers` などの相対パスが切れる）
3. 一覧に出る 3 つのプロジェクト（`mtk3bsp2_stm32n657`、`mtk3bsp2_stm32n657_FSBL`、`mtk3bsp2_stm32n657_Appli`）を
   すべて選んで Finish
4. `mtk3bsp2_stm32n657_FSBL`、`mtk3bsp2_stm32n657_Appli` の順に、Debug 構成でビルドする
   （構成は Project > Build Configurations > Set Active > Debug、ビルドは Project > Build Project）

`Debug/`（makefile・`objects.list`・成果物）はリポジトリに入っていません。CubeIDE がビルドのときに各自の環境のパスで作ります。

コマンドでビルドする場合は、上の 4 まで済ませて（CubeIDE で一度ビルドして `Debug/` の makefile と `objects.list` を
作ってから）、次を実行します。`scripts/build.sh` は CubeIDE が `C:\ST\STM32CubeIDE_2.2.0` にある前提で、同梱の
コンパイラと make を使い、FSBL と Appli の Debug をビルドします。

```bash
bash scripts/build.sh
```

`scripts/build.sh` は `Debug/` にある makefile を使うだけで `.cproject` を読みません。設定を変えたときや
ファイルを足したときは、CubeIDE でもう一度ビルドして makefile を作り直してから使ってください。

### 3.3 ESC-10 版の重みを外部フラッシュに書く（最初の 1 回だけ）

既定の ESC-10 版は、重み（3,282,785 B）を外部フラッシュの `0x70180000` から読むので、一度書いておきます
（FSD50K 版に切り替えたときは重みがアプリの像に含まれる＝AXISRAM4 に置かれるので、この書き込みは要りません）。
hex はリポジトリに含めていません（ST のライセンス配布物）。

1. hex を取得する。`STM32N6-GettingStarted-Audio` v2.3.0 の `Projects/X-CUBE-AI/models/aed_weights.hex` です
   ```bash
   curl -L -o aed_weights.hex https://raw.githubusercontent.com/STMicroelectronics/STM32N6-GettingStarted-Audio/v2.3.0/Projects/X-CUBE-AI/models/aed_weights.hex
   ```
   `bash scripts/stedgeai/generate_esc10.sh <STM32N6-GettingStarted-Audio>` の出力
   `scripts/stedgeai/st_ai_output_esc10/aed_weights.hex` も同じ内容です。
2. 取れたものを確かめる。サイズ 9,233,746 バイト、SHA-256 は次のとおりです（Intel HEX のテキストで、改行は CRLF）
   ```bash
   wc -c aed_weights.hex      # 9233746 aed_weights.hex
   sha256sum aed_weights.hex  # b6e3b06b3c140150e7d58203dd7662b8112aba5e5f3dd9a0d3a4be4eb0a9efb8
   ```
3. ボードの SW1（BOOT1）を **1-3 側（Development boot）** にし、USB（CN6 STLK）で PC につなぐ。
   CubeIDE のデバッグは止めておく（ST-LINK は 1 つのプログラムからしか使えない）
4. CubeIDE 2.2.0 に同梱の STM32CubeProgrammer 2.23.0 の CLI で書く（Git Bash では `C:\` を `C:/` と書く）
   ```bash
   "C:/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.500.202603051304/tools/bin/STM32_Programmer_CLI.exe" \
     -c port=SWD mode=HOTPLUG \
     -el "C:/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins/com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.500.202603051304/tools/bin/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr" \
     -hardRst -w aed_weights.hex
   ```
   Windows の表記では `C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.cubeprogrammer.win32_2.2.500.202603051304\tools\bin\STM32_Programmer_CLI.exe` です。
   `win32_` の後ろは CubeIDE 2.2.0 に同梱の版で、ほかの版の CubeIDE では変わります（`plugins` の下で確かめて置き換える）。

書き込み先は hex に埋め込まれた `0x70180000` なので、番地は指定しません（署名も不要）。書けたかは、起動ログの
`[probe] FNV-1a ... MATCH` の行で分かります。

## 4. 書き込みと起動

検証した方法は「デバッガ経由で内部 RAM に載せて実行する」だけです。外部フラッシュからの単体起動は
未検証なので書きません。

1. ボードの SW1（BOOT1）を **1-3 側（Development boot）** にする
2. USB（CN6 STLK）で PC につなぐ
3. CubeIDE で起動構成 **`mtk3bsp2_stm32n657_FSBL Debug`** を実行する（Run > Debug Configurations）。
   この構成は `mtk3bsp2_stm32n657/FSBL/mtk3bsp2_stm32n657_FSBL Debug.launch` としてリポジトリに含まれ、
   Startup タブの loadList に Appli の `Debug/mtk3bsp2_stm32n657_Appli.elf`、FSBL の
   `Debug/mtk3bsp2_stm32n657_FSBL.elf`（Main タブの項目）の順に登録済みです（どちらもプロジェクト相対）。
   デバッガはこの順に Appli → FSBL と RAM に書き込み、Main タブの FSBL から実行を始め、FSBL がアプリに飛びます。
   Appli 単体の構成で起動すると `usermain()` に到達しません
4. デバッガで一時停止していたら F8（Resume）で続行する

FSD50K 版に切り替えたとき（10 節の `AED_MODEL`）、重みは AXISRAM4（0x34270000）に置かれますが、この RAM はリセット直後は電源が切れていて
（RAMCFG の SRAMSD = 1。入れるのはアプリの `npu_hw_init`）、そのままではデバッガの load が書けず
"Load failed" になります。そこで同じ起動構成の Startup タブ「Initialization Commands」（load より前に GDB が
実行する）に、`npu_hw_init` と同じ操作を入れてあります（`.launch` の `org.eclipse.cdt.debug.gdbjtag.core.initCommands`）:

```
set {unsigned int}0x56028A54 = 0x00001000                                   # RCC AHB2ENSR: RAMCFG のクロック
set {unsigned int}0x56028A4C = 0x0000040F                                   # RCC MEMENSR: AXISRAM3〜6 と NPU キャッシュ RAM のクロック
set {unsigned int}0x52023100 = {unsigned int}0x52023100 & ~0x00100000       # RAMCFG AXISRAM3 CR: SRAMSD を落とす
set {unsigned int}0x52023180 = {unsigned int}0x52023180 & ~0x00100000       # RAMCFG AXISRAM4 CR
set {unsigned int}0x52023200 = {unsigned int}0x52023200 & ~0x00100000       # RAMCFG AXISRAM5 CR
set {unsigned int}0x52023280 = {unsigned int}0x52023280 & ~0x00100000       # RAMCFG AXISRAM6 CR
```

番地はセキュア側のエイリアス（アプリと同じ RCC_S 0x56028000、RAMCFG_S 0x52023000）。ENSR はセット専用
レジスタなので他のビットには影響しません。効いていれば起動ログの `npu_hw_init` の行が
`RCC MEMENR ... already enabled` と `RAMCFG AXISRAM4 CR ... (already powered)` になります。
自分で起動構成を作るときはこの 6 行を Initialization Commands に貼ってください。

## 5. 起動ログの読み方

シリアルは ST-LINK の仮想 COM（既定 COM3）、115200 / 8N1 / フロー制御なし。取得は 7 節の `scripts/log.ps1`
（起動前に立ち上げておくと起動行から残る）。起動から本番までは約 25 秒で、次の順に出ます。

| 行 | 意味 |
|---|---|
| `[FAULT] vector table installed` / `[TRACE] CYCCNT=OK ...` | フォルト可視化と時間計測の準備 |
| `CONFIG: A production (INFER_PRIO_INVERT=0 INFER_SLOW_X=1 task_infer pri 15 model=esc10)` | この起動の条件。`model=` が載っているモデル |
| `extflash_init: ret=0` / `npu_hw_init: ret=0` / `tap_ring_init: ret=0` | 外部フラッシュのメモリマップ、NPU の電源・クロック・RIF・キャッシュ、推論用リング |
| `notify: 3 target classes (dog crying_baby sneezing), threshold p>0.70, peak gate -30 dBFS ...` `notify: rel gate ...` `notify: cooldown ...` | 判定の規則（10 節の値がそのまま出る。FSD50K 版なら `4 target classes (Knock Glass Crying_and_sobbing Speech)`） |
| `npu rt init (ll_aton atonn-v1.1.3-275-..., model ...)` → `aed model:` `aed classes:` `aed input quant:` | NPU ランタイムの起動と、モデル名・クラス表・入力の量子化 |
| `npu random-input test ...` / `npu logits ...` / `npu selftest ...` | 乱数入力での自己テスト（PC の参照値との比較、10 回の同一性） |
| `Speak into the onboard mics; ...` と `pt[n]: ... under=0 over=0 late=0 ...` | パススルー開始。ヘッドホンにマイクの音が出る。500 ms ごとの統計 |
| `preproc test ... PASS` | 前処理の自己テスト（実録音 2 本で PC と int8 の差 0。参照ヘッダが無いと SKIP） |
| `READY  起動確認おわり。ここから本番` | ここより後が本番。評価に使う行はこの後だけ |

READY の後は、窓ごとに次の 2 行が出ます（`->` の行はゲートの結果付き）。

```
win   20 pos=  307200  -49dBFS [###.................] rms=  113 peak=  510 seam=ok lag=336us
  -> unknown         p=0.42  preproc=69094us infer=41025us
```

10 窓ごとの `TAP TOTAL` ブロックに累計、`PT_REPORT_COUNT` × 500 ms（既定 10 分）でパススルーが止まり
`PASSTHROUGH STOPPED` と `FINAL` ブロックが出ます。FINAL の `audio under=0 over=0 late=0` が
「音声の取りこぼし 0」の判定行です。

## 6. 通知の見方

| 経路 | 内容 |
|---|---|
| LCD | 検出で画面全体がクラス色になり、中央に英字（ESC-10: DOG 黄 / BABY 橙 / SNEEZE 青、FSD50K: KNOCK 黄 / GLASS 赤 / CRY 橙 / VOICE 青）が 3 秒出る。3 秒経つと黒の `READY` に戻る。保持中に別の検出が来れば塗り替えて 3 秒を測り直す。下部の 3 行は履歴 `+MM:SS  KNOCK p=0.86`（起動からの経過時間、新しいものが上。黒地に白）。左上の数字は 1 秒ごとに 0〜9 と変わる生存表示（止まれば表示タスクが動いていない）。1 回の全面描画の時間は UART の `lcd: win ...` 行の `paint=Nus` に出る |
| LED | 赤（PG10）: 検出で 1 秒点灯。緑（PO1）: 500 ms で点滅し続ける（生存表示） |
| UART JSON | 検出のたびに 1 行。`{"win":123,"cls":"Knock","p":0.87,"lat_ms":112,"under":0,"over":0,"late":0}`。`win` 窓番号、`cls` クラス、`p` 確率、`lat_ms` 窓がそろってからこの行を出すまで、`under/over/late` 音声の取りこぼしの累計。検出が途切れて unknown になったときだけ `"cls":"unknown"` を 1 行出す |

試し方: ESC-10 版（既定）は犬の鳴き声・赤ちゃんの泣き声・くしゃみの音源を鳴らします（咳ばらいも sneezing になりやすい）。
FSD50K 版はドアをノックする（Knock）・話しかける（Speech。30 秒に 1 回だけ通知）。

## 7. ログの取得

```powershell
powershell -ExecutionPolicy Bypass -File scripts/log.ps1                 # → logs/uart_<日時>.log
powershell -ExecutionPolicy Bypass -File scripts/log.ps1 -Timestamp      # 各行の先頭に PC の時計 HH:mm:ss.fff
powershell -ExecutionPolicy Bypass -File scripts/log.ps1 -Port COM5 -Out logs/run1.log
```

COM ポートは 1 プロセスしか開けないので、他のターミナルを閉じてから実行します。
起動行（CONFIG: など）を残したいときは、log.ps1 を先に立ち上げてからリセット（デバッガ起動）します。

## 8. 集計

```bash
uv run scripts/analyze_31.py                          # PINNED のログで docs/3-1_results.md を作り直す（A/B/D の表）
uv run scripts/analyze_31.py logs/uart_x.log --out -  # 指定したログの走行集計（通知件数・件/時）だけ表示
uv run scripts/notify_replay.py logs/uart_x.log                    # 判定規則を PC で再生し、通知件数とボードの印を照合
uv run scripts/notify_replay.py logs/uart_x.log --model fsd50k     # FSD50K 版の対象クラスとクールダウンで
```

走行集計は READY〜FINAL の時間から件/時を出します。READY と FINAL の無いログでは時間は出ません。

## 9. 試験の再現

### 9.1 対照実験 3-1（優先度が守るもの）

`mtk3bsp2_stm32n657/Appli/Application/npu/infer_task.h` の 1 行を変えてビルドし直し、10 分走らせます。

| 条件 | 変更 | 期待 |
|---|---|---|
| A 本番 | 既定（`INFER_PRIO_INVERT (0)`、`INFER_SLOW_X (1)`） | audio under/over/late = 0/0/0、窓 642/642 |
| B 優先度逆転 | `INFER_PRIO_INVERT (1)`（推論を優先度 3 = 音声より上） | 音が壊れる（under/over/late が毎窓増える）、表示は無事 |
| D 推論を 10 倍重く | `INFER_SLOW_X (10)` | 音は無傷、窓の約 13% が消え、ログと LCD が止まる |

走行ごとに `CONFIG:` 行に条件が出るので、`uv run scripts/analyze_31.py --scan` で条件ごとの最新のログを表にできます。
結果は [3-1_results.md](3-1_results.md) と [3-1D_対照試験_結果報告.docx](3-1D_対照試験_結果報告.docx)。

### 9.2 対照試験 1-Ex（検出率・遅延・誤報率）

PC のスピーカーで ESC-50 のクリップを決まった順で鳴らし、UART ログと突き合わせます。

```bash
powershell -ExecutionPolicy Bypass -File scripts/log.ps1 -Timestamp     # 別ターミナルで先に
uv run scripts/aed_play_test.py <ESC-50>                                 # ESC-10 版: 無音 60 s → クリップ 10 本 → 生活音 12 回
uv run scripts/aed_play_test.py <ESC-50> --model fsd50k                  # FSD50K 版: knock / glass / crying 各 2 本 + clock_tick 2 本、生活音に「一言しゃべる」
uv run scripts/aed_play_test.py <ESC-50> --dry-run --silence 5 --interval 6   # 進行だけ確かめる
```

記録は `logs/play_<日時>.txt`。両方のログに PC の時計が付くので、最初の手拍子を 0 点にして合わせます。
FSD50K 版で使う door_wood_knock / glass_breaking の 4 本は ESC-10 の外なので、`audio/` に無ければ
`https://github.com/karolpiczak/ESC-50/raw/master/audio/<ファイル名>` から取ります（`scripts/aed_clips.py` の `CLIPS_FSD50K`）。
結果は [1-Ex_対照試験結果.docx](1-Ex_対照試験結果.docx)、1 時間の連続走行は [3-5_1時間連続走行_結果報告.docx](3-5_1時間連続走行_結果報告.docx)。

### 9.3 PC 側の参照値（自己テストのヘッダ）

自己テストの参照ヘッダはコミット済みです（`aed_test_input.h`）。前処理の参照（`aed_ref_clips.h`）は ESC-50 由来なので
コミットしておらず、無ければボードは `preproc test: SKIP` と出します。作るには:

```bash
uv run scripts/aed_ref.py <STM32N6-GettingStarted-Audio>/Projects/X-CUBE-AI/models/yamnet_1024_64x96_tl_qdq_int8.onnx   # ESC-10 版の乱数入力
uv run scripts/aed_clips.py <STM32N6-GettingStarted-Audio> <ESC-50>          # ESC-10 版の実録音（前処理の参照 2 本 + 判定用 30 本）
uv run --with tensorflow scripts/aed_clips.py --model fsd50k <ESC-50>         # FSD50K 版（乱数入力 + 前処理の参照 2 本）
```

### 9.4 NPU 向けモデルの生成し直し（通常は不要）

```bash
bash scripts/stedgeai/generate_fsd50k.sh                                   # FSD50K 版 → model_fsd50k/network.c と network_weights.c
bash scripts/stedgeai/generate_esc10.sh <STM32N6-GettingStarted-Audio>     # ESC-10 版 → model_esc10/network.c と重みの hex
```

ランタイム（`npu/st/`）とモデルの生成は同じツール版（ST Edge AI Core 4.0.1）でそろえます。
生成物の冒頭の `#if LL_ATON_VERSION_*` が合わないとビルドで止まります。詳細は
[../mtk3bsp2_stm32n657/Appli/Application/npu/model_fsd50k/README.md](../mtk3bsp2_stm32n657/Appli/Application/npu/model_fsd50k/README.md)。

## 10. 設定値と変え方

| 設定 | 既定 | 場所 | 意味 |
|---|---|---|---|
| `AED_MODEL` | ESC10 | `Appli/Application/npu/aed_model.h` | モデルの選択。FSD50K にするには `#define AED_MODEL AED_MODEL_ESC10` の行を `AED_MODEL_FSD50K` に書き換える（`-DAED_MODEL=AED_MODEL_FSD50K` でも可）。切り替えたら必ず Clean してからビルド（CubeIDE: Project > Clean。コマンドなら CubeIDE 同梱の make（`C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.make.*\tools\bin`）を PATH に通して `make -C mtk3bsp2_stm32n657/Appli/Debug clean`。Debug/ に別設定の .o が残るため） |
| `AED_OOD_THR` | 0.7 | `Appli/Application/aed/notify.h` | 1 位の確率がこれを超えたときだけクラスを名乗る |
| `NOTIFY_GATE_PEAK_DBFS` | −30 | 同上 | 窓のピークがこの dBFS 未満なら通知しない（−99 で切） |
| `NOTIFY_FLOOR_WINDOWS` / `NOTIFY_FLOOR_PERCENTILE` / `NOTIFY_GATE_REL_DB` | 60 / 10 / 10 | 同上 | 直近 60 窓の rms の 10 パーセンタイルを暗騒音とし、それ +10 dB 未満なら通知しない |
| `NOTIFY_CLASSES` / `NOTIFY_CLASS_NAMES` | FSD50K: Knock / Glass / Crying_and_sobbing / Speech、ESC-10: dog / crying_baby / sneezing | `Appli/Application/npu/model_<name>/aed_model_cfg.h` | 通知するクラス（番号はモデルの出力順。名前と照合される） |
| `NOTIFY_COOLDOWN_S` | FSD50K: Speech 30、他 0 | 同上 | 同じクラスを通知してからこの秒数は再通知しない |
| `PT_REPORT_COUNT` | 1200（500 ms × 1200 = 10 分） | `Appli/Application/audio/audio_task.c` | パススルー（＝計測）を続ける長さ。1 時間なら 7200 |
| `INFER_PRIO_INVERT` / `INFER_SLOW_X` | 0 / 1 | `Appli/Application/npu/infer_task.h` | 対照実験 B / D（9.1 節）。コミット時は既定に戻す |
| `PREPROC_TEST` | 1 | `Appli/Application/npu/infer_task.c` | 起動時の前処理自己テスト |
| `AED_USE_TEST_CLIPS` | 0 | `Appli/Application/npu/npu_selftest.c` | ESC-10 の 30 本で NPU を判定する（ROM の都合で `PREPROC_TEST` と同時には載せない） |
| `LCD_HOLD_MS` / `NOTIFY_LED_MS` | 3000 / 1000 | `lcd/lcd_task.h` / `aed/notify.h` | 画面の保持時間、赤 LED の点灯時間 |

FSD50K 版の重みは `model_fsd50k/network_weights.c` の配列としてリンカスクリプトの AXISRAM4 領域（0x34270000）に置かれ、
デバッガが elf と一緒に RAM へ載せます。この置き方はデバッガ起動専用です（外部フラッシュからの起動では
連続した像しか複写されないため）。デバッガ起動時は AXISRAM4 を先に有効化する必要があり、起動構成の
Initialization Commands に含めてあります（4 節）。`.bin` は 0x34270000 までの隙間を埋めて約 2.7 MB になりますが使いません。
