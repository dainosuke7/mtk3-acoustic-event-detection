# 屋内音お知らせ機

STM32N6570-DK + μT-Kernel 3.0 / TRONプログラミングコンテスト2026 応募作品（RTOSアプリケーション部門・学生部門）

聴覚障害のある人に向けた、屋内の音のお知らせ機です。オンボードの PDM マイクの音を Neural-ART NPU で
音響イベント検出（AED）し、ノック・ガラスの割れる音・泣き声・話し声を LCD・LED・UART(JSON) で知らせます。
推論を毎窓（960 ms ごと）回しながら、マイクからヘッドホンへの音声パススルーを途切れさせないことを
μT-Kernel の固定優先度で守り、10 分と 1 時間の連続走行で音声の取りこぼし 0 を実測しました。
外付けハードは使わず、STM32N6570-DK 単体で完結します。

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
- AI: ST model zoo の YAMNet 派生モデルを ST Edge AI Core で NPU 向けにコンパイル。既定は FSD50K 版（Knock / Glass / Gunshot / Crying / Speech の 5 クラス）、ESC-10 版（10 クラス）に切替可能
- 判定: 確率 0.7、ピーク −30 dBFS、暗騒音 +10 dB の相対ゲート、クラスごとのクールダウン。誤報対策は実ログの再生で設計
- 通知の処理時間は 112 ms（窓がそろってから JSON まで）。音の開始からは 1.1〜1.9 s（窓 975 ms の切り出しによる）

## ドキュメント

| 文書 | 内容 |
|---|---|
| [docs/manual.md](docs/manual.md) | 手順書: 必要なもの、構成図とタスク表、ビルド、書き込みと起動、起動ログの読み方、通知の見方、ログ取得、集計、試験の再現、設定値 |
| [docs/third_party.md](docs/third_party.md) | 既存ソフトウェアの一覧（名称・権利者・入手方法・機能・ライセンス・改変箇所）とコンテスト規則 1.3 への対応 |
| [docs/1-Ex_対照試験結果.docx](docs/1-Ex_対照試験結果.docx) | 検出率・遅延・誤報率の実測（ESC-50 のクリップと生活音） |
| [docs/3-1_results.md](docs/3-1_results.md) | 対照実験 A / B / D の表（スクリプトが UART ログから生成） |
| [docs/3-1D_対照試験_結果報告.docx](docs/3-1D_対照試験_結果報告.docx) | 推論を 10 倍重くしたとき μT-Kernel の優先度が何を守るか |
| [docs/3-5_1時間連続走行_結果報告.docx](docs/3-5_1時間連続走行_結果報告.docx) | 1 時間の連続走行（音声の取りこぼし 0、誤報の集計） |
| [mtk3bsp2_stm32n657/Appli/Application/npu/model_fsd50k/README.md](mtk3bsp2_stm32n657/Appli/Application/npu/model_fsd50k/README.md) | FSD50K 版モデルの出典・クラス・生成手順・重みの置き場 |
| [CLAUDE.md](CLAUDE.md) | 開発中の設計メモ（タスクと優先度、メモリマップ、判定規則、既知の罠） |

## 使い方（要約）

1. STM32CubeIDE 2.2.0 でプロジェクト `mtk3bsp2_stm32n657/FSBL` と `mtk3bsp2_stm32n657/Appli` を開く
2. `bash scripts/build.sh` でビルド
3. SW1（BOOT1）を 1-3 側にし、起動構成 `mtk3bsp2_stm32n657_FSBL Debug` でデバッガから実行
4. `powershell -ExecutionPolicy Bypass -File scripts/log.ps1` で UART ログを取り、`READY` の後の JSON 行と LCD を見る

詳細と評価の手順は [docs/manual.md](docs/manual.md)。

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
docs/                    手順書、既存ソフト一覧、試験報告
```

## オープンソースとしての公開

本作品はオープンソースとして公開します。本プロジェクトで新規に作成した部分（`Appli/Application/` 配下のうち
ST 由来のファイルを除くすべて、`Appli/Core/` に手で足した箇所、`scripts/`、`docs/`）のライセンスは
**MIT License** とします。同梱している既存ソフトウェアはそれぞれのライセンスに従い、本プロジェクトの
ライセンスの対象外です（一覧と条件は [docs/third_party.md](docs/third_party.md)）。特に
`Appli/Application/npu/st/` と生成済みモデル（`model_esc10/`・`model_fsd50k/` の `network*.c/.h`、
`stai_network*.c/.h`）は ST の SLA0104 に従い、オープンソースライセンスの条件下には置きません。
`Appli/mtk3_bsp2/` は T-License 2.1 / 2.2 に従います。
