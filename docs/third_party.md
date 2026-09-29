# 既存ソフトウェアの一覧（コンテスト規則 1.3 対応）

本応募プログラムが利用する、他者が開発した既存ソフトウェアの一覧です。規則 1.3 の求める
名称・権利者・入手方法・機能に加え、ライセンスと、改変の有無と箇所を書きます。
「ライセンス（確認元）」はリポジトリ内のファイルか、入手元リポジトリの LICENSE で確認した値です。
版はソースの版定義（`__STM32N6xx_HAL_VERSION_*` など）とリリースノートから取りました。

μT-Kernel 3.0 本体には改変を加えていません（`mtk3bsp2_stm32n657/Appli/mtk3_bsp2/` は初回コミット以降変更なし）。
CubeMX 生成コードなどに手で足した箇所は 2 節に列挙します。

## 1. 一覧

### 1.1 同梱しているもの（ボードで動く部分）

| 名称 | 権利者 | 入手方法 (URL) | 機能 | ライセンス（確認元） | 改変の有無と箇所 |
|---|---|---|---|---|---|
| μT-Kernel 3.0 BSP2（カーネル μT-Kernel 3.0、`VER_MAJOR 3` / `VER_MINOR 0`） | Ken Sakamura / TRON Forum | https://github.com/tron-forum/mtk3_bsp2 の develop ブランチ、コミット 85b88ce（2026-07-03。同梱の README.md・doc/ 4 本が一致し、カーネルは同コミットのサブモジュール mtkernel_3 435096c = ソースの版表示 3.00.07.B0。リリースタグでは v1.00.04（2026-05-13）より後で、doc/bsp2_stm32_cube_jp.md の版は 01.00.B8 / 2026.06.23）。取得日は 2026-08-28 以前（本リポジトリの初回コミット） | リアルタイム OS 本体と STM32N6570-DK 向け BSP（CubeIDE プロジェクト、リンカスクリプト、tm_printf のシリアル出力）。`mtk3bsp2_stm32n657/Appli/mtk3_bsp2/` | T-License 2.2（カーネルの各ソースのヘッダ。BSP 部には T-License 2.1 のヘッダも混在） | 無改変（コンフィグも既定）。Appli プロジェクトのリンカスクリプトだけ 2 節のとおり追記 |
| STM32N6xx HAL / LL ドライバ v1.3.0 | STMicroelectronics | https://github.com/STMicroelectronics/stm32n6xx-hal-driver （STM32CubeN6 v1.3.0 https://github.com/STMicroelectronics/STM32CubeN6 の一部） | ペリフェラルのドライバ（RCC, GPIO, GPDMA, MDF, SAI, I2C, XSPI, LTDC ほか）。`mtk3bsp2_stm32n657/Drivers/STM32N6xx_HAL_Driver/` | BSD-3-Clause（`Drivers/STM32N6xx_HAL_Driver/LICENSE.txt`） | 無改変。LTDC ドライバ 4 本（`stm32n6xx_hal_ltdc.c/.h`, `stm32n6xx_hal_ltdc_ex.c/.h`）は手元の配布物に無かったので同じ v1.3.0 から追加（無改変）。使うドライバの有効化は 2 節の `stm32n6xx_hal_conf.h` |
| CMSIS-Core(M) 6.1 | Arm Limited | https://github.com/ARM-software/CMSIS_6 | Cortex-M55 のコア定義・キャッシュ操作（`Drivers/CMSIS/Include/`） | Apache-2.0（`Drivers/CMSIS/LICENSE`） | 無改変 |
| CMSIS Device STM32N6xx v1.3.0 | STMicroelectronics | https://github.com/STMicroelectronics/cmsis-device-n6 | レジスタ定義、スタートアップコード、システム初期化（`Drivers/CMSIS/Device/ST/STM32N6xx/`） | Apache-2.0（`Drivers/CMSIS/Device/ST/STM32N6xx/LICENSE.txt`） | 無改変 |
| STM32 ExtMem Manager v1.5.0 | STMicroelectronics | https://github.com/STMicroelectronics/stm32-mw-extmem-mgr （STM32CubeN6 v1.3.0 同梱の版） | FSBL の外部フラッシュ初期化とアプリケーションの起動（`mtk3bsp2_stm32n657/Middlewares/ST/STM32_ExtMem_Manager/`。FSBL からのみ使用） | SLA0044（同梱の `mtk3bsp2_stm32n657/Middlewares/ST/STM32_ExtMem_Manager/LICENSE.md`。入手元リポジトリ v1.5.0 の `LICENSE.md` の写し） | 無改変 |
| MX66UW1G45G コンポーネントドライバ v1.1.0 | STMicroelectronics | https://github.com/STMicroelectronics/stm32-mx66uw1g45g | 外部 NOR フラッシュへのコマンド（リセット、DTR-OPI 設定、メモリマップ）。`Appli/Application/extflash/mx66uw1g45g/` | BSD-3-Clause（同梱の `LICENSE.txt`） | `mx66uw1g45g.c/.h` は無改変。`mx66uw1g45g_conf.h` は ST のテンプレート（同名で複製して使う前提のファイル）に本機の設定値を書いたもの |
| RK050HR18 コンポーネントドライバ v1.0.1 | STMicroelectronics | https://github.com/STMicroelectronics/stm32-rk050hr18 | LCD パネル（800x480、LTDC 直結）の解像度と同期タイミングの定義。`Appli/Application/lcd/st/rk050hr18.h` | BSD-3-Clause（同梱の `Appli/Application/lcd/st/LICENSE_rk050hr18.md`。入手元の `LICENSE.md` の写し） | 無改変 |
| STM32 Utilities Fonts | STMicroelectronics | https://github.com/STMicroelectronics/STM32CubeN6/tree/main/Utilities/Fonts | LCD に描く 17x24 のビットマップフォント。`Appli/Application/lcd/st/font24.c`, `fonts.h` | BSD-3-Clause（同梱の `Appli/Application/lcd/st/LICENSE_fonts.md`。入手元の `LICENSE.md` の写し、Copyright 2014(-2019)） | 無改変 |
| ST Edge AI Core 4.0.1 ランタイム: ll_aton（atonn-v1.1.3-275-g6770925d）、`NetworkRuntime1201_CM55_GCC.a`、`Inc/` のヘッダ、`Devices/STM32N6xx/`（ATON レジスタ定義、mcu_cache） | STMicroelectronics | https://www.st.com/en/development-tools/stedgeai-core.html （インストーラの `C:\ST\STEdgeAI\4.0\Middlewares\ST\AI\`。Npu/ll_aton、Npu/Devices/STM32N6xx、Inc、Lib/GCC/ARMCortexM55） | Neural-ART NPU の推論ランタイム（`Appli/Application/npu/st/`） | SLA0104（同梱の `npu/st/LICENSE.md` = 同ツールの `Middlewares/ST/AI/LICENSE.txt`） | 無改変。同梱しなかったもの: RTOS 用 OSAL（FreeRTOS / ThreadX / Zephyr）とそのテンプレート、HAL_CACHEAXI に依存する `npu_cache.c`（自作の `npu_cache_port.c` で置き換え）、`Inc/` のうちビルドで参照しない 37 本 |
| 生成済み NPU ネットワーク ESC-10 版（`Appli/Application/npu/model_esc10/network.c/.h`, `stai_network.c/.h`） | STMicroelectronics（ST Edge AI Core の生成物） | `stedgeai generate`（4.0.1）で STM32N6-GettingStarted-Audio v2.3.0 の `yamnet_1024_64x96_tl_qdq_int8.onnx` から生成（`scripts/stedgeai/generate_esc10.sh`） | ESC-10 の 10 クラス（YAMNet 1024 派生）の NPU 実行コード | SLA0104（生成物のヘッダは「コンポーネント直下の LICENSE に従う」＝ランタイムと同じ）。元モデルと重み hex は SLA0044（GettingStarted-Audio） | 無改変。重み hex（3,282,785 B）は同梱せず、[manual.md](manual.md) 4.1 節の手順で取得して外部フラッシュに書く |
| 生成済み NPU ネットワーク FSD50K 版（`Appli/Application/npu/model_fsd50k/network.c/.h`, `stai_network.c/.h`, `network_weights.c`） | 同上 | `bash scripts/stedgeai/generate_fsd50k.sh`（下の tflite から生成） | FSD50K 5 クラス（YAMNet 256 派生）の NPU 実行コードと重み（AXISRAM4 に置く配列） | SLA0104 | 無改変（`network_weights.c` は生成された raw を `scripts/stedgeai/raw2c.py` で配列にしただけ） |
| YAMNet 256 FSD50K 転移学習モデル（`yamnet_e256_64x96_tl_int8.tflite`, `yamnet_e256_64x96_tl_config.yaml`） | STMicroelectronics（元は Google の YAMNet。学習データは FSD50K） | https://github.com/STMicroelectronics/stm32ai-modelzoo の `audio_event_detection/yamnet/ST_pretrainedmodel_public_dataset/fsd50k/yamnet_e256_64x96_tl/without_unknown_class/`（Git LFS） | 学習済みモデル。PC 側の参照値の生成と NPU 向け生成の元。`Appli/Application/npu/model_fsd50k/` | Apache-2.0（同梱の `model_fsd50k/LICENSE.md` = 同リポジトリ `audio_event_detection/yamnet/ST_pretrainedmodel_public_dataset/LICENSE.md`） | 無改変 |

### 1.2 参照して書き直したもの（ファイルは同梱していない）

| 名称 | 権利者 | 入手方法 (URL) | 機能 | ライセンス（確認元） | 改変の有無と箇所 |
|---|---|---|---|---|---|
| STM32N6570-DK BSP | STMicroelectronics | https://github.com/STMicroelectronics/stm32n6570-dk-bsp （STM32CubeN6 v1.3.0 の `Drivers/BSP/STM32N6570-DK`） | 参照元: `extflash.c` の XSPI 初期化手順（`stm32n6570_discovery_xspi.c`）、`lcd.c` の LTDC タイミング・クロック経路・GPIO とパネル制御線（`stm32n6570_discovery_lcd.c`）、WM8904 の初期化シーケンス（`stm32n6570_discovery_audio.c`） | BSD-3-Clause（STM32N6-GettingStarted-Audio の `LICENSE.md` の一覧、入手元リポジトリの `LICENSE.md`） | ファイルはコピーせず、手順を参照して自作コードに書き直した（移植）。参照箇所は各ソースのコメントに記載 |
| WM8904 コンポーネントドライバ | STMicroelectronics | https://github.com/STMicroelectronics/stm32-wm8904 | 参照元: `Appli/Application/audio/wm8904.c/.h` のレジスタアドレスと初期化手順 | BSD-3-Clause（入手元リポジトリの `LICENSE.md`） | ファイルはコピーせず、レジスタ定義と初期化手順を参照して移植（`wm8904.h` の冒頭に明記） |
| STM32N6xx HAL（RAMCFG / RIF / CACHEAXI 部分） | STMicroelectronics | 上の HAL と同じ（v1.3.0 の `stm32n6xx_hal_ramcfg.c` / `_rif.c` / `_cacheaxi.c`） | 参照元: `npu_hw.c` と `npu_cache_port.c` のレジスタ操作の手順 | BSD-3-Clause | 該当ドライバは手元の配布物に無く、手順を参照してレジスタを直接操作するコードを書いた |
| STM32N6-GettingStarted-Audio v2.3.0（commit 46f1f976） | STMicroelectronics | https://github.com/STMicroelectronics/STM32N6-GettingStarted-Audio | 参照元: `npu_hw.c` の初期化手順（`Int_Mem_Config()` / `NPU_Config()`）、前処理の設定と表（`Projects/Dpu/ai_model_config.h.aed`, `user_mel_tables.c.aed`。`scripts/aed_clips.py` が毎回照合）、ESC-10 版の ONNX モデルと重み hex、Neural-ART の設定（`stm32n6.mpool`, `user_neural_art.json`） | SLA0044（同リポジトリの `LICENSE.md`。Projects と AI Runtime の区分） | 設定ファイル 2 本を `scripts/stedgeai/stm32n6_esc10.mpool`, `user_neural_art_esc10.json` として写した（`.mpool` のパスだけ変更）。それ以外は同梱せず、手順を参照して書き直した |

### 1.3 PC 側でだけ使うもの（同梱しない）

| 名称 | 権利者 | 入手方法 (URL) | 機能 | ライセンス（確認元） | 改変の有無と箇所 |
|---|---|---|---|---|---|
| ESC-50（うち ESC-10 サブセットと fold 5 の door_wood_knock / glass_breaking） | Karol J. Piczak（各クリップの元の録音は Freesound の各投稿者） | https://github.com/karolpiczak/ESC-50 | 対照試験 1-Ex の再生音源（`scripts/aed_play_test.py`）と、前処理・NPU の参照ヘッダの元（`scripts/aed_clips.py`） | CC BY-NC 3.0（ESC-10 サブセットは CC BY 3.0。同リポジトリの `LICENSE`） | 同梱しない。生成したヘッダ（`aed_ref_clips.h`, `aed_test_clips.h`）もコミットしない（`.gitignore`） |
| numpy 2.5.3 | NumPy Developers | https://pypi.org/project/numpy/ （uv が実行時に取得） | 前処理の PC 実装、ログ集計 | BSD-3-Clause | 無改変 |
| scipy 1.18.1 | SciPy Developers | https://pypi.org/project/scipy/ | 44.1 kHz → 16 kHz のリサンプリング | BSD-3-Clause | 無改変 |
| onnx 1.23.0 / onnxruntime 1.30.0 | ONNX Project / Microsoft | https://pypi.org/project/onnx/ , https://pypi.org/project/onnxruntime/ | ESC-10 版の参照値（PC の推論結果） | Apache-2.0 / MIT | 無改変 |
| TensorFlow 2.21.0（TensorFlow Lite） | Google | https://pypi.org/project/tensorflow/ | FSD50K 版の参照値（tflite の PC 実行） | Apache-2.0 | 無改変 |
| uv 0.12.15、Python 3.12 | Astral / Python Software Foundation | https://docs.astral.sh/uv/ | スクリプトの実行環境 | MIT または Apache-2.0 / PSF | — |

### 1.4 開発ツール（同梱しない）

| 名称 | 権利者 | 入手方法 (URL) | 機能 |
|---|---|---|---|
| STM32CubeIDE 2.2.0（GNU Tools for STM32 14.3.rel1、STM32CubeProgrammer 2.23.0 を同梱） | STMicroelectronics（コンパイラは GNU/Arm） | https://www.st.com/en/development-tools/stm32cubeide.html | ビルド、デバッガ経由の実行、外部フラッシュへの書き込み |
| ST Edge AI Core 4.0.1 | STMicroelectronics | https://www.st.com/en/development-tools/stedgeai-core.html | NPU 向けのモデル生成（生成物は同梱済みなので、評価には不要） |

## 2. 改変した既存ファイル（CubeMX 生成コードなど）

| ファイル | 変更点（1 行） |
|---|---|
| `mtk3bsp2_stm32n657/Appli/Core/Src/main.c` | `MX_I2C2_Init`（WM8904 制御）、`MX_SAI1_Init`（MCKDIV=12 を直接指定）、`MX_MDF1_Init`（PDM マイク）、`MPU_Config`、`MX_LTDC_Clock_Init`（PLL4 → LTDC 25 MHz）を追加。.ioc に無い周辺の初期化で、可能な限り USER CODE 区画に置いた |
| `mtk3bsp2_stm32n657/Appli/Core/Inc/main.h` | 上の周辺のハンドル（`hi2c2`, `hsai1`, `hmdf1`, DMA ハンドル）と状態変数（`g_sai1_status`, `g_ltdc_clk_status`, `g_ltdc_kerclk`）の extern を追加 |
| `mtk3bsp2_stm32n657/Appli/Core/Src/stm32n6xx_it.c` | `GPDMA1_Channel0_IRQHandler`（MDF）、`GPDMA1_Channel2_IRQHandler`（SAI）、`MDF1_FLT0_IRQHandler` を追加 |
| `mtk3bsp2_stm32n657/Appli/Core/Inc/stm32n6xx_hal_conf.h` | `HAL_MDF` / `HAL_SAI` / `HAL_XSPI` / `HAL_LTDC` の `_MODULE_ENABLED` を有効化 |
| `mtk3bsp2_stm32n657/Appli/.project` | HAL の `stm32n6xx_hal_mdf.c`, `_sai.c`, `_sai_ex.c`, `_xspi.c`, `_ltdc.c`, `_ltdc_ex.c` を親の `Drivers/` からリンク |
| `mtk3bsp2_stm32n657/Appli/.cproject` | Debug 構成にプリプロセッサ定義（`LL_ATON_PLATFORM` など 5 つ）、インクルードパス（`npu/st/{ll_aton,device,inc}`）、ライブラリ `NetworkRuntime1201_CM55_GCC.a` を追加。`Application/npu/model_esc10`, `model_fsd50k` をビルド対象から除外 |
| `mtk3bsp2_stm32n657/Appli/STM32N657X0HXQ_LRUN.ld` | MEMORY に AXISRAM4（0x34270000、448 KB）と出力セクション `.npu_weights` を追加（FSD50K 版の重み） |
| `mtk3bsp2_stm32n657/Drivers/STM32N6xx_HAL_Driver/` | LTDC ドライバ 4 本を STM32CubeN6 v1.3.0 から追加（無改変） |
| `mtk3bsp2_stm32n657/Appli/Debug/*.mk` | CubeIDE が生成する makefile。`scripts/build.sh` が使うため追跡し、新しいフォルダの追加に合わせて更新（内容は CubeIDE の Refresh で作り直される） |

FSBL（`mtk3bsp2_stm32n657/FSBL/`）は BSP2 に含まれる CubeMX 生成のまま無改変です。

## 3. 規則 1.3 への対応

- 上記の既存ソフトウェアについて、応募規約に則り著作権などの権利処理を行ったことを、本コンテストの主催者および協力団体に対して保証します。各ライセンスの条件（著作権表示とライセンス文の保持、ST の SLA0044 / SLA0104 が求める ST 製デバイス上での使用とオープンソース条件を課さないこと、ESC-50 の非同梱）に従っています。
- 主催者が応募プログラムを評価するために必要な既存ソフトウェアは、リポジトリに同梱しているか無償で入手できるものだけで、本コンテストの表彰式終了後 1 週間程度まで利用可能な状態で提供します。同梱しないもの（ESC-10 版の重み hex、ESC-50、PC 側の依存パッケージ、開発ツール）の入手方法は上表のとおりです。既定の ESC-10 版の評価にはリポジトリと STM32CubeIDE に加えて重み hex（GitHub で公開されている STM32N6-GettingStarted-Audio に同梱。[manual.md](manual.md) 4.1 節）が必要で、FSD50K 版はリポジトリと STM32CubeIDE だけで動きます。
- 本応募プログラムはオープンソースとして公開します。新規作成部分のライセンスは Apache License 2.0（全文はルートの [LICENSE](../LICENSE)）とし、同梱した既存ソフトウェアはそれぞれのライセンスに従います（[README](../README.md)）。
