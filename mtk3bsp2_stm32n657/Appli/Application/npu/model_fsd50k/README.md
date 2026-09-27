# FSD50K 版のモデル（5-1、ブランチ model-fsd50k）

脳（NPU 向けに生成した network.c）だけを差し替え、音の経路・前処理・タスク・ログ・判定規則は ESC10 版と共通のまま使う。
切替は `Application/npu/aed_model.h` の `AED_MODEL`。

## 入手元とライセンス

| ファイル | 内容 | 出典 |
|---|---|---|
| `yamnet_e256_64x96_tl_int8.tflite` | Yamnet 256 を FSD50K の 5 クラスで転移学習し TFLite で int8 量子化したモデル（183,960 B、sha256 `33649e4ff8e5c26c0733e3b85e5dc4519ba0607590cd1d716c8e1fae91b781e1`） | ST model zoo [stm32ai-modelzoo](https://github.com/STMicroelectronics/stm32ai-modelzoo) `audio_event_detection/yamnet/ST_pretrainedmodel_public_dataset/fsd50k/yamnet_e256_64x96_tl/without_unknown_class/`（Git LFS。取得は `https://media.githubusercontent.com/media/STMicroelectronics/stm32ai-modelzoo/main/<path>`） |
| `yamnet_e256_64x96_tl_config.yaml` | 学習・前処理・量子化の設定（同じ場所） | 同上 |
| `LICENSE.md` | Apache-2.0（model zoo の `audio_event_detection/yamnet/ST_pretrainedmodel_public_dataset/LICENSE.md` の写し。`audio_event_detection/LICENSE.md` の表で yamnet/ST_pretrainedmodel_public_dataset は Apache-2.0、Copyright STMicroelectronics） | 同上 |

Apache-2.0 なので tflite そのものをリポジトリに入れている（LICENSE を同梱し、README の一覧に記載）。
「with_unknown_class」（unknown を足した 6 クラス版）は使わない（model zoo の説明では PC の精度は下がる。本機は自前の門で unknown を扱う）。

## クラス

config の `class_names` は `['Speech', 'Gunshot_and_gunfire', 'Crying_and_sobbing', 'Knock', 'Glass']`。
モデルの出力の並びはこれを昇順に並べたもの（ESC10 版と同じ規則。ESC10 の config も class_names は dog, chainsaw, … の順で、
モデルの出力は chainsaw, clock_tick, … の昇順）:

| 番号 | クラス | 通知 | 画面 |
|---|---|---|---|
| 0 | Crying_and_sobbing | 対象 | CRY |
| 1 | Glass | 対象 | GLASS |
| 2 | Gunshot_and_gunfire | 対象外 | GUN |
| 3 | Knock | 対象 | KNOCK |
| 4 | Speech | 対象（30 秒のクールダウン。続いている間は 1 回） | VOICE |

並びが正しいことは `scripts/aed_clips.py --model fsd50k` が ESC-50 の door_wood_knock / glass_breaking / crying_baby のクリップで毎回確かめる
（2026-09-27: 昇順で 6 本中 5 本正解、config の順だと 2 本。外れた 1 本は crying_baby の 5-198411-B-20.wav が Speech 0.72）。

## 入出力

- 入力 int8 1x64x96x1、scale 0.0555472746、zero_point 38（ESC10 版は 0.0305305421 / 33）。ボードは `stai_network_get_info` の値を
  前処理に渡すので定数は持たない（`preproc_set_quant`）
- 出力 float32 x5（tflite の中で int8 softmax → DEQUANTIZE。config の `quantization_output_type: float`）
- softmax 直前の int8 ロジットは tflite の tensor #40（scale 0.137729645、zero_point 74）。乱数入力（seed 2026）では
  最適化カーネルと参照カーネルで 1 LSB 違う要素が 2 つ（`aed_test_input.h`）
- 前処理（`feature_extraction`: 96 列 x 64 メル、n_fft 512、hop 160、窓 400 hann、center False、power 1.0、125〜7500 Hz、
  norm None、htk True、to_db False）は ESC10 版の config と同一

## PC 側の参照ヘッダ

```bash
uv run --with tensorflow scripts/aed_clips.py --model fsd50k <ESC-50>
```

- `aed_test_input.h`: 乱数入力（seed 2026）と TFLite の 2 つの実装（最適化カーネル / 参照カーネル）の期待値、ロジット。コミットする
- `aed_ref_clips.h`: knock（5-218980-A-30.wav）と glass（5-221528-A-39.wav）の生 PCM と int8 テンソルと PC の 1 位。ESC-50 由来なのでコミットしない
- ESC-50 の fold 5 の 4 本（5-218980-A-30 / 5-235644-A-30 / 5-221528-A-39 / 5-233605-A-39）は ESC-10 の外なので、
  `audio/` に無ければ `https://github.com/karolpiczak/ESC-50/raw/master/audio/<file>` から取る

## network.c の生成（STEdgeAI Core 4.0 の stedgeai が要る。2026-09-27 時点で未実施）

```bash
bash scripts/stedgeai/generate_fsd50k.sh [C:/ST/STEdgeAI/4.0/Utilities/windows/stedgeai.exe]
```

中身（`scripts/stedgeai/`）:

1. `stedgeai generate -m yamnet_e256_64x96_tl_int8.tflite --target stm32n6 --st-neural-art axisram@user_neural_art_axisram.json --input-data-type int8 --output-data-type float32 --name network`
   - プロファイル `axisram` のオプションは ESC10 版（GettingStarted-Audio の `user_neural_art.json`）と同じ
     `--Ocache-opt -O3 --Os --native-float --Omax-ca-pipe 4 --cache-maintenance --csv-file network --all-buffers-info`
   - メモリプール `stm32n6_axisram.mpool`: 重みは AXISRAM4（0x34270000、448 KB、`constants_preferred`）、作業域は AXISRAM6
     （0x34350000、448 KB。ESC10 版と同じ）。外部フラッシュ（xSPI2）と hyperRAM（xSPI1）は持たせない
2. 生成物 `network.c / network.h / stai_network.c / stai_network.h` をこのディレクトリに写す（無改変）
3. 重み `network_atonbuf.AXISRAM4.raw` を `raw2c.py` で `network_weights.c`（`.npu_weights` セクションの const 配列）にする。
   `Appli/STM32N657X0HXQ_LRUN.ld` の AXISRAM4 領域（0x34270000）に置かれ、Debug 起動でデバッガが elf と一緒に載せる
4. `network.c` 冒頭の `#if LL_ATON_VERSION_MAJOR != 1 || ...` が repo の `npu/st/ll_aton/ll_aton_version.h`
   （atonn-v1.1.3-262-g7cc65410）と食い違えば、ツール同梱の ll_aton と NetworkRuntime ライブラリを両方まとめて入れ替える
   （片方だけ変えない。README の一覧の版も更新）。ESC10 版も同じランタイムでビルドが通ることを確かめる
5. `aed_model_cfg.h` の `AED_LOGIT_*` を生成した network.c から読んで入れる（どの epoch が SW の DequantizeLinear か、
   scale / zero_point の番地。`AED_LOGIT_HOOK` を 1 に）。入れるまでは logits_check を飛ばす
6. `bash scripts/build.sh`（network.c があれば `aed_model.h` が自動で FSD50K を選ぶ）

重みの置き場の根拠: model zoo の表で Yamnet 256 の重みは約 137 KiB、作業域 144 KiB。Appli の ROM 領域（511 KB）は
ESC10 版で 369 KB 使っていて残り 142 KB では重みを載せられない。AXISRAM4 は未使用の 448 KB で、NPU からは AXISRAM6 と
同じバス・同じ RISAF の扱い。FSBL からの起動（外部フラッシュのアプリ像を `BOOT_Application` が複写）では連続した 1 つの像しか
載らないので、この置き方は Debug 起動専用（本機の運用は Debug 起動）。

## 板でやること（生成後）

1. セルフテスト: 乱数入力の top1 が `AED_TEST_EXPECT_TOP`（Crying_and_sobbing）と一致、10 回連続で bit-identical
2. 前処理テスト（PREPROC TEST）: knock / glass の 2 本で int8 の差 0（量子化は scale 0.0555 / zp 38）、top1 が PC と一致
3. 1-Ex: `uv run scripts/aed_play_test.py <ESC-50> --model fsd50k`（knock / glass / crying 各 2 本 + clock_tick 2 本、
   生活音に「一言しゃべる」）。ログは `uv run scripts/notify_replay.py <uart log> --model fsd50k` で再生
4. 暗騒音 30 分。通れば tag v3
