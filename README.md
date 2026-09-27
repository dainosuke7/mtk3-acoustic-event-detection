# 屋内音お知らせ機

STM32N6570-DK + μT-Kernel 3.0 / TRONプログラミングコンテスト2026 応募作品
（RTOSアプリケーション部門・学生部門）

聴覚障害のある人に向けた、屋内の音のお知らせ機です。
オンボードの PDM マイクで拾った音を Neural-ART NPU で音響イベント検出（AED）し、
結果を LCD・LED・UART(JSON) で知らせます。
推論中も音声のパススルーを途切れさせず、通知遅延の上限を実測で示すことを目標にしています。
外付けハードは使わず、STM32N6570-DK 単体で完結します。

検出クラスは現時点では ST 公開の AED モデル（YAMNet 1024 派生、ESC-10 の10クラス:
赤ちゃんの泣き声・犬・くしゃみ・時計の秒針音 など）に従います。

## 現在の状態

| 段階 | 内容 | 状態 |
|---|---|---|
| Phase 0 | PDMマイク → MDF1 → SAI1 → WM8904 のパススルー、時間計測基盤、フォルト可視化 | 完了 |
| Phase 1 | 外部フラッシュ上のモデル重み領域へのアクセス確認、NPU 用メモリと NPU 周辺の初期化、NPU ランタイムと AED モデルの組み込み、固定入力での推論の自己テスト（PC の ONNX Runtime の結果と比較） | 進行中 |
| Phase 2 | Neural-ART NPU での音響イベント検出 | 未着手 |

## ハードウェア

- STM32N6570-DK (MB1939) / Cortex-M55 + Neural-ART NPU
- 入力: オンボード PDM MEMS マイク → MDF1_Filter0 + GPDMA
- 出力: SAI1 → WM8904 コーデック → ヘッドホンジャック (CN15)
- シリアル: 115200 / 8N1 / フロー制御なし

## ビルドと実行

### 必要なもの

- STM32CubeIDE 2.2.0
- STM32CubeProgrammer（外部フラッシュへの書き込みに使用）
- STM32N6570-DK 実機

### ビルド

```bash
bash scripts/build.sh
```

FSBL と Appli の両方をビルドします。CubeIDE の GUI からビルドしても同じです。

### 実行（デバッグ起動）

1. SW1 (BOOT1) を **1-3 側（Development boot）** にする
2. CubeIDE で **`mtk3bsp2_stm32n657_FSBL Debug`** 構成を実行する

   この起動構成は `mtk3bsp2_stm32n657/FSBL/mtk3bsp2_stm32n657_FSBL Debug.launch`
   としてリポジトリに含まれており、Startup の loadList に Appli 側の .elf も
   登録済みです。クローン直後にそのまま実行できます。

   Appli 単体で起動するとブートシーケンスが成立せず `usermain()` に到達しません。

3. ログの取得

   ```bash
   powershell scripts/log.ps1              # → logs/uart_<日時>.log
   powershell scripts/log.ps1 -Timestamp   # 各行の先頭に PC の時計 (HH:mm:ss.fff) を付ける
   ```

4. 対照実験（3-1）のログ集計: `uv run scripts/analyze_31.py` → `docs/3-1_results.md`（`logs/` の UART ログを先頭の `CONFIG:` 行で A/B/D に振り分け、FINAL・毎秒の in=Hz 行・JSON の lat_ms を表にする）

### モデル重みの書き込み（Phase 2 以降）

モデルの重みファイルはリポジトリに含めていません（ST のライセンス下の
配布物であり、3MB を超えるため）。下記から取得して外部フラッシュに
書き込んでください。

```bash
git clone https://github.com/STMicroelectronics/STM32N6-GettingStarted-Audio.git
cd STM32N6-GettingStarted-Audio
# 取得したコミット: 46f1f976442cd1697fedf98f3b5875d998c61980 (v2.3.0)

export DKEL="<STM32CubeProgrammer_N6>/bin/ExternalLoader/MX66UW1G45G_STM32N6570-DK.stldr"
STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el $DKEL -hardRst \
  -w Projects/X-CUBE-AI/models/aed_weights.hex
```

書き込み先は hex に埋め込まれた `0x70180000` です（署名不要）。

### NPU 出力の比較用データ（PC 側）

ボードの NPU 推論結果と比べるための固定入力と期待値を、ONNX Runtime で作ります。
[uv](https://docs.astral.sh/uv/) が必要です（依存パッケージとバージョンはスクリプト内に記載）。
ONNX モデルは上と同じリポジトリの `Projects/X-CUBE-AI/models/` にあります。

```bash
uv run scripts/aed_ref.py <STM32N6-GettingStarted-Audio>/Projects/X-CUBE-AI/models/yamnet_1024_64x96_tl_qdq_int8.onnx
```

10 クラスの出力を表示し、入力（int8 1x64x96x1、seed 固定）と期待値を
`Appli/Application/npu/aed_test_input.h` に書き出します。これは参考値で、NPU の合否には使いません。

NPU の合否は、環境音データセット ESC-50 のうち ESC-10 の実録音 30 本で、1位のクラスを
PC と比べて判定します。録音から作る入力は、元のライセンス（ESC-10 は CC BY 3.0、ESC-50 全体は
CC BY-NC 3.0）のためリポジトリに含めていません。次の手順で生成してからビルドしてください
（生成しなくてもビルドは通り、実録音の判定だけが飛ばされます）。

```bash
git clone https://github.com/karolpiczak/ESC-50.git
uv run scripts/aed_clips.py <STM32N6-GettingStarted-Audio> <ESC-50>
```

ST と同じ前処理（log-mel 64 x 96）で int8 入力を作り、ONNX Runtime の1位と一緒に
`Appli/Application/npu/model_esc10/aed_test_clips.h` に書き出します（.gitignore 済み）。
前処理の設定と表は GettingStarted-Audio のものと毎回照合します。

FSD50K 版（ブランチ model-fsd50k。モデルは `Appli/Application/npu/model_fsd50k/` に同梱、Apache-2.0）の参照ヘッダは
tflite を TensorFlow Lite で回して作ります（TensorFlow は `--with` で足します）:

```bash
uv run --with tensorflow scripts/aed_clips.py --model fsd50k <ESC-50>
```

`model_fsd50k/aed_test_input.h`（乱数入力の期待値。コミット済み）と `model_fsd50k/aed_ref_clips.h`（ESC-50 の
knock / glass の 2 本。コミットしない）を書き出し、ESC-50 のクリップでクラスの並びも確かめます。
NPU 向けの network.c の生成（ST Edge AI Core 4.0.1 の stedgeai）は `bash scripts/stedgeai/generate_fsd50k.sh`（手順と重みの置き場は `model_fsd50k/README.md`）。ESC10 版の生成し直しは `bash scripts/stedgeai/generate_esc10.sh <STM32N6-GettingStarted-Audio>`。

## ディレクトリ構成

```
mtk3bsp2_stm32n657/
  Appli/
    Application/        本プロジェクトで新規に作成したコード
      audio/            マイク入力・音声出力・FIFO・WM8904 制御
      trace/            DWT CYCCNT による時間計測とCSVダンプ
      fault/            フォルト・未実装IRQのUART可視化
      extflash/         外部フラッシュ（重み）を XSPI2 でメモリマップ
        mx66uw1g45g/    ST のフラッシュ用コンポーネントドライバ（下表）
      npu/              NPU 用内部メモリ・NPU・NPU キャッシュ・RIF の初期化、推論ランタイムの起動
        st/             ST Edge AI ランタイム（ll_aton）と生成済み AED モデル（下表）
      usermain.c        タスク生成とアプリのエントリ
    Core/               CubeMX 生成コード（一部を手で追加。CLAUDE.md 参照）
    mtk3_bsp2/          μT-Kernel 3.0 BSP2（無改変）
  FSBL/                 First Stage Boot Loader（CubeMX 生成）
  Drivers/ Middlewares/ ST 提供
scripts/                ビルド・ログ取得スクリプト
CLAUDE.md               開発中の制約メモ
```

## 本プロジェクトが新規に作成した部分

- `mtk3bsp2_stm32n657/Appli/Application/` 配下すべて（`extflash/mx66uw1g45g/` と `npu/st/` を除く。下表の ST 製ソフトウェア）
  - `npu/aed_test_input.h` は `scripts/aed_ref.py` の生成物（入力は乱数、期待値は ST のモデルを ONNX Runtime で実行した結果）
- `mtk3bsp2_stm32n657/Appli/.cproject` への追加部分（NPU ランタイムのプリプロセッサ定義・インクルードパス・ライブラリ）
- `mtk3bsp2_stm32n657/Appli/Core/` への追加部分
  - `Src/main.c`: `MX_I2C2_Init()` / `MX_SAI1_Init()` / `MX_MDF1_Init()` / `MPU_Config()`
  - `Src/stm32n6xx_it.c`: `GPDMA1_Channel0/2_IRQHandler`, `MDF1_FLT0_IRQHandler`
  - `Inc/main.h`, `Inc/stm32n6xx_hal_conf.h`: 上記に伴う宣言とモジュール有効化
- `scripts/` 配下

変更理由の詳細は [CLAUDE.md](CLAUDE.md) の「例外として変更した場所」を参照。

## 使用している既存ソフトウェア

コンテスト規則 1.3（名称・権利者・入手方法・機能・権利処理の保証）に基づく一覧です。
「ライセンス」欄はリポジトリ内の LICENSE ファイルまたはソースのヘッダで確認した値です。

### 同梱しているもの

| 名称 | 権利者 | 機能 | ライセンス（確認元） | 入手方法 | 権利処理 |
|---|---|---|---|---|---|
| μT-Kernel 3.0 BSP2 | Ken Sakamura / TRON Forum | リアルタイムOS本体と STM32N6 向け BSP | T-License 2.1 / 2.2（ファイルごとのヘッダに混在） | TRON Forum 配布物 | 無改変で同梱。ヘッダ保持 |
| STM32N6xx HAL/LL Driver | STMicroelectronics | ペリフェラルドライバ | BSD-3-Clause（`Drivers/STM32N6xx_HAL_Driver/LICENSE.txt`） | STM32CubeN6 | ヘッダ保持 |
| CMSIS Core | Arm Limited | Cortex-M55 コア定義 | Apache-2.0（`Drivers/CMSIS/LICENSE`） | STM32CubeN6 | ヘッダ保持 |
| CMSIS Device STM32N6xx | STMicroelectronics | デバイスレジスタ定義・スタートアップ | Apache-2.0（`Drivers/CMSIS/Device/ST/STM32N6xx/LICENSE.txt`） | STM32CubeN6 | ヘッダ保持 |
| STM32 ExtMem Manager | STMicroelectronics | FSBL の外部フラッシュ制御・アプリ起動 | **要確認**（ヘッダは「コンポーネント直下の LICENSE に従う、無ければ AS-IS」。LICENSE は未同梱） | STM32CubeN6 | 要確認 |
| MX66UW1G45G Component Driver V1.1.0 | STMicroelectronics | 外部 NOR フラッシュへのコマンド（リセット・DTR-OPI 設定・メモリマップ）。`Appli/Application/extflash/mx66uw1g45g/` | BSD-3-Clause（同梱の `LICENSE.txt`。GettingStarted-Audio の `LICENSE.md` でも「BSP Components」は BSD-3-Clause） | STM32N6-GettingStarted-Audio v2.3.0（commit 46f1f97）の `Drivers/BSP/Components/mx66uw1g45g/` | `.c`/`.h` は無改変。`mx66uw1g45g_conf.h` は同梱テンプレートからインクルードとダミーサイクル値だけ変更（変更点をファイル内に記載）。ヘッダ保持 |
| STM32N6570-DK BSP（XSPI NOR 部分） | STMicroelectronics | `extflash.c` の初期化手順の参照元（`stm32n6570_discovery_xspi.c`） | BSD-3-Clause（GettingStarted-Audio の `LICENSE.md` で「STM32N6570-DK BSP Drivers」） | 同上の `Drivers/BSP/STM32N6570-DK/` | ファイルは同梱せず、必要な手順だけを `extflash.c` に書き起こした。参照箇所と相違点をソースのコメントに記載 |
| STM32N6-GettingStarted-Audio アプリ部（`Int_Mem_Config()` / `NPU_Config()`）と NPU デバイス定義（`ATON.h`） | STMicroelectronics | `npu_hw.c` の初期化手順と、NPU のバージョンレジスタの番地・期待値の参照元 | SLA0044（`Projects/LICENSE.md`。GettingStarted-Audio の `LICENSE.md` で「Projects」「AI Runtime」は SLA0044） | STM32N6-GettingStarted-Audio v2.3.0（commit 46f1f97）の `Projects/GS/Src/audio_bm.c`、`Projects/Common/misc_toolbox.c`、`Middlewares/ST/AI/Npu/Devices/STM32N6xx/ATON.h` | ファイルは同梱せず、手順と定数だけを `npu_hw.c` に書き起こした。参照箇所と相違点をソースのコメントに記載。SLA0044 は ST 製デバイス上での使用に限る条件で、本機は STM32N6 上でのみ動く |
| STM32N6xx HAL（RAMCFG / RIF / CACHEAXI 部分） | STMicroelectronics | `npu_hw.c` と `npu_cache_port.c` のレジスタ操作の参照元（`stm32n6xx_hal_ramcfg.c` / `_rif.c` / `_cacheaxi.c`） | BSD-3-Clause（GettingStarted-Audio の `LICENSE.md` で「STM32N6xx HAL/LL Drivers」） | 同上の `Drivers/STM32N6xx_HAL_Driver/`（本リポジトリの `Drivers/` には含まれていない） | ファイルは同梱せず、同じレジスタ操作を `npu_hw.c` / `npu_cache_port.c` に書き起こした。参照箇所をソースのコメントに記載 |
| STM32N6xx HAL LTDC ドライバ（`stm32n6xx_hal_ltdc.c/.h`, `stm32n6xx_hal_ltdc_ex.c/.h`。HAL v1.3.0） | STMicroelectronics | LCD コントローラ（LTDC）のタイミング・レイヤ・CLUT の設定。`Drivers/STM32N6xx_HAL_Driver/` | BSD-3-Clause（`Drivers/STM32N6xx_HAL_Driver/LICENSE.md`：「licensed by STMicroelectronics under the **BSD-3-Clause** license」） | [STM32CubeN6 v1.3.0](https://github.com/STMicroelectronics/STM32CubeN6) のサブモジュール [stm32n6xx-hal-driver](https://github.com/STMicroelectronics/stm32n6xx-hal-driver)（commit `cf84d98`）の `Src/` と `Inc/` | 無改変で同梱し、ヘッダ保持。本リポジトリの HAL と同じ v1.3.0 であることを `stm32n6xx_hal.h` の版数で確認してから入れた |
| RK050HR18 Component Driver v1.0.1（`rk050hr18.h`） | STMicroelectronics | パネル（800x480、LTDC 直結）の解像度と同期タイミングの定義。`Appli/Application/lcd/st/` | BSD-3-Clause（同梱の `LICENSE.md`：3条項の本文。STM32CubeN6 の `LICENSE.md` でも「BSP Components」は BSD-3-Clause） | [STM32CubeN6 v1.3.0](https://github.com/STMicroelectronics/STM32CubeN6) のサブモジュール [stm32-rk050hr18](https://github.com/STMicroelectronics/stm32-rk050hr18) tag `v1.0.1`（commit `4ecf4fe`） | 無改変で同梱し、ヘッダ保持。ドライバ本体（`.c`）は元から存在せず、ヘッダのタイミング定義だけ（RGB 直結なのでパネル側のコマンド列が無い） |
| STM32 Utilities Fonts（`font24.c`, `fonts.h`） | STMicroelectronics | LCD に描く 17x24 のビットマップフォント。`Appli/Application/lcd/st/` | BSD-3-Clause（同梱の `LICENSE.md`：Copyright 2014(-2019) ST、3条項の本文） | [STM32CubeN6 v1.3.0](https://github.com/STMicroelectronics/STM32CubeN6) の `Utilities/Fonts/` | 無改変で同梱し、ヘッダ保持。Font24 だけを使い、font8/12/16/20 は同梱しない |
| STM32N6570-DK BSP（LCD 部分） | STMicroelectronics | `lcd.c` の LTDC タイミング・クロック経路・GPIO とパネル制御線の参照元（`stm32n6570_discovery_lcd.c`） | BSD-3-Clause（STM32CubeN6 の `LICENSE.md` で「BSP Drivers」） | [STM32CubeN6 v1.3.0](https://github.com/STMicroelectronics/STM32CubeN6) のサブモジュール [stm32n6570-dk-bsp](https://github.com/STMicroelectronics/stm32n6570-dk-bsp)（commit `f9c98f3`）の `stm32n6570_discovery_lcd.c` | ファイルは同梱せず、値と手順だけを `lcd.c` に書き起こした（参照箇所を file:line でソースのコメントに記載）。同梱しなかった理由: `BSP_LCD_InitEx` が L8（パレット）形式を選べず、DMA2D と BSP の設定ヘッダ一式を引きずるため |
| ST Edge AI ランタイム（ll_aton 1.1.3-275、`NetworkRuntime1201_CM55_GCC.a`、ヘッダ）と生成済み AED ネットワーク（ESC10 版: YAMNet 1024 派生 `npu/model_esc10/network.c/.h`, `stai_network.c/.h`。FSD50K 版: YAMNet 256 派生 `npu/model_fsd50k/`、重みの配列 `network_weights.c` を含む） | STMicroelectronics | NPU 推論ランタイムと、NPU 向けにコンパイル済みのモデル。`Appli/Application/npu/st/`（ランタイム）、`npu/model_esc10/`・`npu/model_fsd50k/`（生成物） | SLA0104（同梱の `npu/st/LICENSE.md` = ST Edge AI Core 4.0.1 の `Middlewares/ST/AI/LICENSE.txt`。生成物 network.c 等のヘッダは「LICENSE file in the root directory of this software component」を指す） | ST Edge AI Core 4.0.1（`C:\ST\STEdgeAI\4.0\Middlewares\ST\AI\` の `Npu/ll_aton/`、`Npu/Devices/STM32N6xx/`、`Inc/`、`Lib/GCC/ARMCortexM55/`）。生成物は同ツールの `stedgeai generate`（`scripts/stedgeai/`） | 無改変で同梱し、ヘッダ保持。2026-09-27 に GettingStarted-Audio v2.3.0 同梱の 4.0.0 版（ll_aton 1.1.3-262、NetworkRuntime1200、SLA0044）から入れ替えた（4.0.1 の生成物が 1.1.3-275 を要求するため。ESC10 版も 4.0.1 で生成し直し、重みは 4.0.0 と同一）。同梱しなかったもの: RTOS 用 OSAL（FreeRTOS / ThreadX / Zephyr）とそのテンプレート、HAL_CACHEAXI に依存する `npu_cache.c`（`npu_cache_port.c` で置き換え）、`Inc/` のうちビルドで参照されない 37 本。SLA0104 は ST 製デバイス上での使用に限る条件（第4項）で、本機は STM32N6 上でのみ動く。オープンソースライセンスの条件下に置くことは禁止（第5項）なので、本プロジェクトのコードに付けるライセンスの対象外とする |
| Yamnet 256 FSD50K 転移学習モデル（`yamnet_e256_64x96_tl_int8.tflite`、`yamnet_e256_64x96_tl_config.yaml`） | STMicroelectronics（元は Google の YAMNet。学習データは FSD50K） | FSD50K 版（ブランチ model-fsd50k）の AED モデル。Knock / Glass / Gunshot_and_gunfire / Crying_and_sobbing / Speech の 5 クラス。`Appli/Application/npu/model_fsd50k/` | Apache-2.0（同梱の `model_fsd50k/LICENSE.md` = model zoo の `audio_event_detection/yamnet/ST_pretrainedmodel_public_dataset/LICENSE.md`。`audio_event_detection/LICENSE.md` の表で確認） | github.com/STMicroelectronics/stm32ai-modelzoo `audio_event_detection/yamnet/ST_pretrainedmodel_public_dataset/fsd50k/yamnet_e256_64x96_tl/without_unknown_class/`（Git LFS） | 無改変で同梱、LICENSE 同梱。NPU 向けの生成物（network.c と重み）は stedgeai で作る（`model_fsd50k/README.md`） |

### Phase 2 で追加予定のもの

導入時に上の表へ移し、ライセンスを実ファイルで確認してから記載する。

| 名称 | 権利者 | 機能 | 入手方法 | 同梱 |
|---|---|---|---|---|
| STM32 AI AudioPreprocessing Library | STMicroelectronics | log-mel スペクトログラム計算 | STM32N6-GettingStarted-Audio | 検討中 |
| CMSIS-DSP | Arm Limited | FFT 等 | 同上 | 検討中 |
| AED モデル重み（YAMNet 1024 派生, aed_weights.hex） | STMicroelectronics | 学習済みモデル | 同上 | **含めない**（上記手順で取得） |

### 同梱せず、開発中の確認にだけ使うもの

| 名称 | 権利者 | 用途 | ライセンス（確認元） | 入手方法 | 扱い |
|---|---|---|---|---|---|
| ESC-50（うち ESC-10 サブセット） | Karol J. Piczak（各クリップの元の録音は Freesound の各投稿者） | NPU の推論結果を PC と比べる実録音 30 本（`scripts/aed_clips.py`） | ESC-10 は CC BY 3.0、ESC-50 全体は CC BY-NC 3.0（ESC-50 の `LICENSE`） | github.com/karolpiczak/ESC-50 | 音声も、そこから作った入力（`aed_test_clips.h`）もリポジトリに入れない。入力はヘッダを生成した手元のビルドの自己テストにだけ入る（ヘッダが無ければ入らない） |

> **TODO（提出前）**
> - ExtMem Manager のライセンスを STM32CubeN6 の配布物で確認する
> - Phase 2 分は各配布物の LICENSE を読んで再配布可否を確認する
> - 同梱するものはライセンス全文を `licenses/` に置く

## ライセンス

本プロジェクトで新規に作成したコードの扱いは提出までに確定させます。

同梱している既存ソフトウェアは、それぞれ上表のライセンスに従います。
`mtk3bsp2_stm32n657/Appli/mtk3_bsp2/` 配下は各ファイルのヘッダに記載された
T-License（2.1 または 2.2）に従い、ヘッダは削除していません。
`mtk3bsp2_stm32n657/Appli/Application/npu/st/` 配下は SLA0044（同梱の `LICENSE.md`）に従います。
SLA0044 はオープンソースライセンスの条件下に置くことを禁じているため、
本プロジェクトのコードに付けるライセンスはこのフォルダには及びません。
