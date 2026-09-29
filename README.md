# μT-Kernel で音響処理と NPU 推論を両立する音響イベント検出基盤

STM32N6570-DK + μT-Kernel 3.0 / TRONプログラミングコンテスト2026 応募作品（RTOSアプリケーション部門・学生部門）

オンボードの PDM マイクの音を 1 サンプルも落とさずに取り込みながら、Neural-ART NPU で音響イベント検出（AED）を毎窓（960 ms ごと）回す基盤です。μT-Kernel の固定優先度で「音 ＞ 推論 ＞ 表示」を守り、推論を音より優先にした場合・推論を 10 倍重くした場合と比べる対照試験（各 10 分）と、1 時間の連続走行で、音声の取りこぼし 0 を実測しました。動作例として、聴覚障害のある人に向けて屋内の音（犬の鳴き声・赤ちゃんの泣き声・くしゃみ）を LCD・LED・UART(JSON) で知らせます（既定の ESC-10 版。ノック・ガラスの割れる音・泣き声・話し声を対象にした FSD50K 版も同梱、後述）。外付けハードは使わず、STM32N6570-DK 単体で完結します。

## 構成

```
PDM マイク ──MDF1 (16 kHz)──GPDMA──▶ task_pcm (優先度 5) ──▶ pcm_fifo ──GPDMA── SAI1 ──▶ WM8904 ──▶ ヘッドホン
                                        │
                                        └──▶ tap ring (2 秒) ──▶ task_infer (15): log-mel 69 ms → NPU 41 ms → 判定
                                                                        ├──▶ reporter (20) → UART JSON
                                                                        ├──▶ 赤 LED 1 秒
                                                                        └──▶ task_lcd (25) → LCD 800x480
```

- OS: μT-Kernel 3.0（BSP2、無改変）。タスク間はイベントフラグ・セマフォ・メッセージバッファ、ISR → タスクはイベントフラグ
- AI: ST model zoo の YAMNet 派生モデルを ST Edge AI Core で NPU 向けにコンパイル。既定は ESC-10 版（10 クラス。通知するのは dog / crying_baby / sneezing）。FSD50K 版（Knock / Glass / Gunshot / Crying / Speech の 5 クラス）に切替可能（下の「FSD50K 版について」）
- 判定: 確率 0.7、ピーク −30 dBFS、暗騒音 +10 dB の相対ゲート、クラスごとのクールダウン。誤報対策は実ログの再生で設計
- 通知の処理時間は 112 ms（窓がそろってから JSON まで）。音の開始からは 1.1〜1.9 s（窓 975 ms の切り出しによる）

## ドキュメント

| 文書 | 内容 |
|---|---|
| [手順書](docs/手順書_音響イベント検出基盤.pdf) | 審査用の手順書: 評価の 3 段階（ログから再計算・動作確認・実験 1〜3 の再現）、必要なもの、困ったとき |
| [紹介資料](docs/紹介資料_音響イベント検出基盤.pdf) | 作品の紹介スライド（目的・設計・実験 1〜3・動作例・限界と展開・再現手順） |
| [docs/manual.md](docs/manual.md) | 手順書: 必要なもの、構成図とタスク表、ビルド、書き込みと起動、起動ログの読み方、通知の見方、ログ取得、集計、試験の再現、設定値 |
| [docs/third_party.md](docs/third_party.md) | 既存ソフトウェアの一覧（名称・権利者・入手方法・機能・ライセンス・改変箇所）とコンテスト規則 1.3 への対応 |
| [LICENSE](LICENSE) | 本プロジェクトで新規に作成した部分のライセンス（Apache License 2.0） |
| [docs/1-Ex_対照試験結果.docx](docs/1-Ex_対照試験結果.docx) | 検出率・遅延・誤報率の実測（ESC-50 のクリップと生活音） |
| [docs/3-1_results.md](docs/3-1_results.md) | 対照実験 A / B / D の表（スクリプトが UART ログから生成） |
| [docs/3-1D_対照試験_結果報告.docx](docs/3-1D_対照試験_結果報告.docx) | 推論を 10 倍重くしたとき μT-Kernel の優先度が何を守るか |
| [docs/3-5_1時間連続走行_結果報告.docx](docs/3-5_1時間連続走行_結果報告.docx) | 1 時間の連続走行（音声の取りこぼし 0、誤報の集計） |
| [docs/logs/README.md](docs/logs/README.md) | 報告に使った UART ログと再生ログの写し。各報告の数字とログの行・スクリプトの対応 |
| [mtk3bsp2_stm32n657/Appli/Application/npu/model_fsd50k/README.md](mtk3bsp2_stm32n657/Appli/Application/npu/model_fsd50k/README.md) | FSD50K 版モデルの出典・クラス・生成手順・重みの置き場 |
| [CLAUDE.md](CLAUDE.md) | 開発中の設計メモ（タスクと優先度、メモリマップ、判定規則、既知の罠） |

## 使い方（要約）

1. STM32CubeIDE 2.2.0 でプロジェクト `mtk3bsp2_stm32n657/FSBL` と `mtk3bsp2_stm32n657/Appli` を開く
2. `bash scripts/build.sh` でビルド
3. ESC-10 版のモデル重み（hex、ST 配布）を外部フラッシュに一度書く（[docs/manual.md](docs/manual.md) 4.1 節）
4. SW1（BOOT1）を 1-3 側にし、起動構成 `mtk3bsp2_stm32n657_FSBL Debug` でデバッガから実行
5. `powershell -ExecutionPolicy Bypass -File scripts/log.ps1` で UART ログを取り、`READY` の後の JSON 行と LCD を見る

詳細と評価の手順は [docs/manual.md](docs/manual.md)。

## FSD50K 版について

`Appli/Application/npu/model_fsd50k/` の FSD50K 版（ST model zoo の `yamnet_e256_64x96_tl` を FSD50K の 5 クラス
Knock / Glass / Gunshot / Crying / Speech で転移学習した `without_unknown_class` 版）は実機で動作します:
推論 3.5 ms/窓（ESC-10 版は 41 ms）、重み 148 KB は AXISRAM4 に置くので外部フラッシュへの書き込みが要らず、
ESC-10 版と同じ検証手順（NPU の自己テスト、前処理の PC との突き合わせ、対照試験 1-Ex の再生、41 分の連続走行で
音声の取りこぼし 0）を通りました（[docs/logs/uart_20260928_064258.log](docs/logs/uart_20260928_064258.log)）。

ただし `without_unknown_class` 版には「その他」のクラスが無く、どの音も 5 クラスのどれかに振り分けます。屋内の生活音
（手拍子・紙・咳ばらい・椅子・マグ）の 9/10 と、対象外のクリップ（clock_tick 2 本）を Glass か Knock として通知し、
朝の生活空間 34 分では 250 件/時になりました（同じ判定規則の ESC-10 版（夜の 33.9 分）は 3.5 件/時。
[docs/logs/README.md](docs/logs/README.md)）。そのため既定にしていません。次の一手は、ST model zoo の同じモデル群にある
`with_unknown_class` 版（「その他」クラス付き）を同じ手順で生成し、生活音が unknown に落ちるかを 1-Ex で確かめることです。

FSD50K 版で動かすには `Appli/Application/npu/aed_model.h` の `#define AED_MODEL AED_MODEL_ESC10` を `AED_MODEL_FSD50K` に
書き換え、Clean してからビルドします（Debug 起動時の注意と手順は [docs/manual.md](docs/manual.md) の 4 節・10 節）。

## ディレクトリ構成

```
mtk3bsp2_stm32n657/
  Appli/Application/     本プロジェクトで新規に作成したコード
    audio/               マイク入力・音声出力・FIFO・WM8904 制御
    aed/                 前処理 (log-mel) と判定・通知
    npu/                 NPU の初期化、推論ランタイムの起動、モデルの切替 (aed_model.h)
      st/                ST Edge AI ランタイム (無改変。SLA0104)
      model_esc10/       ESC-10 版の生成済みモデルと参照ヘッダ
      model_fsd50k/      FSD50K 版の tflite・生成済みモデル・重み・参照ヘッダ
    lcd/                 LTDC の初期化と表示タスク (lcd/st/ は ST のパネル定義とフォント)
    extflash/            外部フラッシュのメモリマップ (extflash/mx66uw1g45g/ は ST のドライバ)
    trace/ fault/        時間計測・トレース・フォルト可視化
    usermain.c           タスク生成とアプリのエントリ
  Appli/Core/            CubeMX 生成コード (手で足した箇所は docs/third_party.md の 2 節)
  Appli/mtk3_bsp2/       μT-Kernel 3.0 BSP2 (無改変)
  FSBL/                  First Stage Boot Loader (CubeMX 生成、無改変)
  Drivers/               ST 提供 (HAL / CMSIS)
scripts/                 ビルド、ログ取得、集計、対照試験、参照値の生成、モデル生成 (stedgeai/)
docs/                    手順書、既存ソフト一覧、試験報告、logs/（報告に使った UART ログ）
LICENSE                  新規作成部分のライセンス（Apache License 2.0）
NOTICE                   著作権表示（Apache License 2.0 の NOTICE）
```

## オープンソースとしての公開

本作品はオープンソースとして公開します。本プロジェクトで新規に作成した部分（`Appli/Application/` 配下のうち
ST 由来のファイルを除くすべて、`Appli/Core/` に手で足した箇所、`scripts/`、`docs/`）のライセンスは
**Apache License 2.0**（全文は [LICENSE](LICENSE)）とします。同梱している既存ソフトウェアはそれぞれのライセンスに従い、本プロジェクトの
ライセンスの対象外です（一覧と条件は [docs/third_party.md](docs/third_party.md)）。特に
`Appli/Application/npu/st/` と生成済みモデル（`model_esc10/`・`model_fsd50k/` の `network*.c/.h`、
`stai_network*.c/.h`）は ST の SLA0104 に従い、オープンソースライセンスの条件下には置きません。
`Appli/mtk3_bsp2/` は T-License 2.1 / 2.2 に従います。
