TRONプログラミングコンテスト2026 — 屋内音お知らせ機

聴覚障害者向けの屋内音お知らせ機。オンボードPDMマイクの音声を NPU で 音響イベント検出（AED）し、LCD・LED・UART(JSON) で通知する。 推論中も音声パススルーを途切れさせず、通知遅延の上限を実測で示す。

部門: RTOSアプリケーション部門・学生部門
締切: 2026-09-30 18:00（応募フォーム提出。ボード送付は不要＝規則の3-b）
規則 1.3: OSのAPI仕様を変えない限り μT-Kernel 3.0 の改変は許容される。 他者の既存ソフトウェアを使う場合は、名称・権利者・入手方法・機能・ 権利処理の保証をドキュメントに記載する義務がある（README に一覧を置く）
編集してよい場所
原則: mtk3bsp2_stm32n657/Appli/Application/ 配下のみ
触らない: Appli/mtk3_bsp2/（μT-Kernel 本体）, Drivers/, Middlewares/, Secure_nsclib/
例外として変更した場所（増やしたら必ずここに追記する）
場所	内容	理由
Appli/Core/Src/main.c	MX_I2C2_Init, MX_SAI1_Init, MX_MDF1_Init, MPU_Config を追加	音声経路（WM8904制御・SAI出力・PDM入力）に必要
Appli/Core/Src/main.c, Core/Inc/main.h	MX_LTDC_Clock_Init を追加（g_ltdc_clk_status / g_ltdc_kerclk を main.h に extern）	LCD の画素クロック（PLL4→IC16→LTDC 25MHz）。PLL のロック待ちはカーネル起動後だと HAL_GetTick の分解能 10ms で誤タイムアウトし得るので起動前に置く
Appli/Core/Inc/stm32n6xx_hal_conf.h	HAL_XSPI_MODULE_ENABLED を有効化。Appli/.project に stm32n6xx_hal_xspi.c の link を追加	アプリ側で XSPI2 を初期化して外部フラッシュをメモリマップする（Application/extflash/）ため
Appli/Core/Inc/stm32n6xx_hal_conf.h	HAL_LTDC_MODULE_ENABLED を有効化。Appli/.project に stm32n6xx_hal_ltdc.c / _ltdc_ex.c の link を追加	LCD（Application/lcd/）で LTDC を使うため
Drivers/STM32N6xx_HAL_Driver/{Src,Inc}/	stm32n6xx_hal_ltdc.c/.h と _ltdc_ex.c/.h を追加（無改変）	手元の HAL に LTDC ドライバが無かった。STM32CubeN6 v1.3.0 から、版が手元と同じ v1.3.0 であることを確認して入れた（README のソフトウェア一覧に記載）
Appli/.cproject（Debug 構成）	プリプロセッサ定義 LL_ATON_PLATFORM=LL_ATON_PLAT_STM32N6 / LL_ATON_OSAL=LL_ATON_OSAL_BARE_METAL / LL_ATON_RT_MODE=LL_ATON_RT_POLLING / LL_ATON_SW_FALLBACK / LL_ATON_DBG_BUFFER_INFO_EXCLUDED=1、インクルードパス ../Application/npu/st/{ll_aton,device,inc,model}、ライブラリ :NetworkRuntime1200_CM55_GCC.a（検索パス ../Application/npu/st/lib）	NPU 推論ランタイム（Application/npu/st/）のビルドに必要
Appli/.cproject（Debug 構成）	sourceEntries の Application に npu/model_esc10|npu/model_fsd50k を excluding、インクルードパス ../Application/npu/st/model を削除（Debug/ の mk 系も同じに）	モデルの生成物は Application/npu/model_<name>/ に置き、aed_model_network.c / aed_model_stai.c が AED_MODEL で選んだ 1 組だけを #include する（2 つの network.c は同じシンボルを定義するので同時にコンパイルできない）
Appli/STM32N657X0HXQ_LRUN.ld	MEMORY に AXISRAM4（0x34270000、448K）と出力セクション .npu_weights を追加（無ければ空）	FSD50K 版の重みを外部フラッシュでなく AXISRAM4 に置く（scripts/stedgeai/raw2c.py が作る model_fsd50k/network_weights.c。network.c が絶対番地で参照するメモリプールと同じ番地。Debug 起動でデバッガが elf と一緒に載せる。FSBL 起動では連続した像しか複写されないので Debug 専用）
変更は可能な限り USER CODE BEGIN/END 区画の中に書く（CubeMX 再生成で消えない）
コマンド
ビルド: bash scripts/build.sh
書き込み・実行: CubeIDE で mtk3bsp2_stm32n657_FSBL Debug 構成を Debug 実行
Startup タブに mtk3bsp2_stm32n657_Appli が追加されていること
Appli 単体の構成で起動すると usermain() に到達しない
直前に FSBL をビルドしておく
ログ: powershell scripts/log.ps1 → logs/uart_<日時>.log（走るたびに別ファイル。固定名に追記すると試験の行が混ざるため。名前を決めたいときや追記したいときは -Out で渡す）。-Timestamp で各行の先頭に PC の時計 HH:mm:ss.fff を付ける（aed_play_test.py のログと同じ形式で、対照試験の突き合わせに使う。既定はオフで出力は従来どおり。時刻はその行の先頭が届いた読み取りのもの。ReadExisting は行の途中で返るので、改行が来るまで溜めてから1行として書いている）。SerialPort の Encoding は UTF-8 にしている（既定は ASCII で、ボードが出す日本語＝READY の区切りが ? に化ける。ASCII は UTF-8 の一部なので英数字だけの出力は変わらない）
COM ポートは1プロセスしか開けない。他のターミナルを閉じてから
重みの書き込み（外部フラッシュ 0x70180000、署名不要）:
  STM32_Programmer_CLI -c port=SWD mode=HOTPLUG -el <ExternalLoader>/MX66UW1G45G_STM32N6570-DK.stldr -hardRst -w aed_weights.hex

CubeProgrammer は CubeIDE 同梱: C:\ST\STM32CubeIDE_*\STM32CubeIDE\plugins\*cubeprogrammer*\tools\bin 重み hex はリポジトリに入れない（ST ライセンス・容量）。取得元は README に記載

NPU 比較用の参照値（PC）: uv run scripts/aed_ref.py <GettingStarted-Audio>/Projects/X-CUBE-AI/models/yamnet_1024_64x96_tl_qdq_int8.onnx → Application/npu/model_esc10/aed_test_input.h を上書き生成（seed 固定なので同じ内容になる。softmax 後の値と、softmax 直前の int8 ロジット・scale・zero_point）。FSD50K 版は uv run --with tensorflow scripts/aed_clips.py --model fsd50k <ESC-50> → model_fsd50k/aed_test_input.h（乱数入力の期待値。コミットする）と model_fsd50k/aed_ref_clips.h（knock / glass の 2 本。コミットしない）。tflite を TensorFlow Lite の最適化カーネル（BUILTIN、デリゲートなし）と参照カーネル（BUILTIN_REF）で回し、ESC-50 の knock / glass / crying_baby でクラスの並び（昇順）を毎回確かめる。ヘッダの書式は aed_ref.py の write_test_input_header で共通
対照試験: uv run scripts/aed_play_test.py <ESC-50>（--dry-run でクリップを鳴らさず進行だけ、--skip-manual で生活音の区間を飛ばす、--silence/--interval で短縮）。区間は 0〜5秒 同期用の手拍子 → 5〜65秒 無音（誤報の基準）→ 65〜215秒 ESC-50 のクリップ5秒を15秒間隔で10本（dog / crying_baby / crackling_fire / sneezing / clock_tick を2巡）→ 215〜395秒 クラス外の生活音を15秒間隔で12回（ノック / 手拍子 / 紙 / 咳ばらい / 椅子 / マグ を2巡。3秒前に予告が出るので人が出す）。記録は logs/play_<日時>.txt に [+215.0s 03:14:35.210] MANUAL knock の形式（SYNC / PHASE / CLIP / MANUAL。経過秒と PC の時計 HH:MM:SS.mmm の両方。時刻の桁で空白の数が変わるのは種別の位置を揃えているため）。区間の切れ目に PHASE silence / PHASE clips / PHASE manual が入る（PHASE manual は最初の合図と同じ時刻に出す。ループの前に出すと最後のクリップの間隔ぶん早くなるため）。UART ログ側にも同じ形式の時計を付けて突き合わせる（別時計なので、最初の手拍子が両方のログに残るのを 0 点にする。窓の番号 x 0.96 秒も目安になる）。--model fsd50k で FSD50K 版: クリップは door_wood_knock / glass_breaking / crying_baby 各 2 本 + 対象外 clock_tick 2 本（aed_clips.py の CLIPS_FSD50K。fold 5 の ESC-10 外の 4 本は ESC-50 の audio/ に無ければ github の raw から取る）、生活音に「一言しゃべる」（Speech）を足して 7 種 x 2 巡。ノックは FSD50K では通知対象なので生活音区間の knock は検出の確認になる
  クリップはピークを -3dBFS にそろえてから鳴らす（ESC-50 は録音ごとに音量が違い、そろえないと「小さくて出なかった」のか「判定が外れた」のか切り分けられない）。正規化はメモリ上で行い winsound の SND_MEMORY | SND_NODEFAULT で同期再生する。SND_ASYNC は付けられない（winsound は SND_MEMORY と SND_ASYNC を同時に使うと RuntimeError "Cannot play asynchronously from memory" を投げる。CPython が参照カウントの面倒を避けるために禁じている）。同期なので PlaySound は再生時間＋α 戻らない（実測 5.00 秒の音で 5.5 秒）。--interval をクリップ長ぎりぎりにすると毎回わずかに遅れる。元の RMS と掛けたゲインは CLIP 行に残す。時刻は PlaySound の直前に取り、ログは鳴らし終わってから書く（時刻と再生開始の間に I/O を挟まない）。起動時に無音 0.1 秒で鳴らせるか試し、鳴らせなければ始める前に止める（1本も鳴っていないまま試験が進むのを防ぐ）。起動時に使うクリップ10本の一覧（RMS とゲイン）を表示し、ログにも # 行で残す。再生は winsound なので Windows のみ、OS の音量はスクリプトからは変えない
対照実験の集計（3-1）: uv run scripts/analyze_31.py → docs/3-1_results.md（logs/ の uart_*.log を先頭の CONFIG: 行で A/B/D に振り分け、FINAL ブロック・READY 以降の毎秒の in=Hz 行・JSON の lat_ms を表にする。表に使うログはスクリプトの PINNED で条件ごとに 1 本に固定してある（ボードを走らせるたびに表が黙って入れ替わらないため。--scan で最新、引数で指定もできる）。使わなかったログは理由と要点の数字を表の下に書く。表の数字は手で直さず、スクリプトで作り直す。同じ表を標準出力にも出す）。走行集計（3.5）: 同じスクリプトが読んだ走行ごとに通知件数（FINAL の notify out とクラス別の JSON 行数）・gated / held / offlist・READY〜FINAL の時間と件/時を出す（表の後。docs にも書く）。CONFIG: 行の無いログ（起動後に記録を始めたもの）は表には使わないが走行集計は出る。対策前は uv run scripts/analyze_31.py logs/uart_20260927_130250.log --out - で 271 件 / 9:57（sneezing 199）
通知規則の再生（3.5-2）: uv run scripts/notify_replay.py <uart log>...（ログの win 行と -> 行から notify_gate と同じ規則を PC で再生し、通知件数とクラス別・止めた理由ごとの数・PASS の窓の一覧・ボードが付けた印との一致を出す。既定は READY 以降、--all で READY 前も、-v で閾値を超えた窓を全部。規則の値は --rel-db / --windows / --percentile / --thr / --peak-dbfs / --classes で変えて試せる。実装の確認: logs/uart_20260927_161828.log → 4、logs/uart_20260927_142118.log → 19、logs/uart_20260927_130250.log → 3）。--model fsd50k で FSD50K の対象クラス（Knock / Glass / Crying_and_sobbing / Speech）と Speech 30 秒のクールダウン（--cooldown Speech=30 のように個別指定も可）
実録音の判定用（PC）: uv run scripts/aed_clips.py <GettingStarted-Audio> <ESC-50> → Application/npu/model_esc10/aed_test_clips.h（30 本の int8 入力）と Application/npu/model_esc10/aed_ref_clips.h（そのうち2本の生 PCM も入れた前処理の突き合わせ用）。どちらもコミットしない。ESC-50 は git clone github.com/karolpiczak/ESC-50（使うのは meta/ と audio/ の 30 本。ファイル名はスクリプトに固定）

ハード
STM32N6570-DK / Cortex-M55 600MHz + Neural-ART NPU
内蔵ユーザFlashなし。外部フラッシュ + 署名必須
SW1(BOOT1) は 1-3 側（Development boot）
シリアル: COM3 / 115200 / 8N1 / フロー制御なし
メモリマップ
領域	アドレス	用途
アプリ（内部RAM）	ROM 0x34000400 +511K / RAM 0x34080000 +1536K（〜0x34200000）	BSP2 リンカ定義。コード領域 511K を超えないか監視
AXISRAM3	0x34200000〜（448KB。stm32n657xx.h の SRAM3_AXI_BASE_S のコメント）	LCD のフレームバッファ。L8 800x480 = 384,000B を先頭に置く（終端 0x3425DC00。NPU の AXISRAM6 とは約 969KB 離れていて重ならない）
AXISRAM4〜5	0x34270000〜	未使用
AXISRAM6	0x34350000〜（NPU は 144KB 使用）	NPU activations。番地は network.c に固定。入力 int8 6144B は 0x34350000〜、出力 float32 x10 は 0x34350410〜（入力の領域の中。推論中に入力の領域は中間結果・出力の置き場として上書きされる）
NPU キャッシュ RAM	0x343C0000〜（256KB）	CACHEAXI 用。NPU キャッシュを有効にしている間は SRAM として使わない
外部フラッシュ: アプリ	0x70100000〜（FSBL ソース上。起動ログは 0x71000400 と表示、未解決）	署名済みアプリ
外部フラッシュ: 重み	0x70180000〜（3,282,785 B）	AED モデル重み
オーディオ経路（MB1939 回路図より）
入力: PDMマイク U13/U14 → MDF1 Filter0（CCK0=PE2, DATIN0=PE8, AF4）→ GPDMA1 ch0。 256サンプル/16ms。32bit 出力を /256 して16bitに飽和変換
出力: SAI1 Block A（マスタTX, I2S）→ WM8904（制御は I2C2）→ CN15。GPDMA1 ch2。400フレーム/25ms
MCKDIV は HAL の自動計算を使わず直接 12 を指定（自動計算が半分の値を返した）
起動ログの SAI1 ... Fs=7998Hz は表示式の誤り。実測は約16kHz
MDF の実レートは約16128Hz（+0.8%、分周設定由来）。実害なし
MIC_DET で外部マイクボード装着時はオンボードマイクがバイパスされる
タスク構成と優先度（小さいほど高い）
優先度	タスク	役割
5	task_pcm	DMA通知（イベントフラグ4ビット）を受けて入力変換・リング操作・出力充填
10	task_audio, task_1	パススルー制御・統計表示 / 生存表示の LED 点滅（緑 PO1）
15	task_infer	NPU ランタイムの初期化・自己テスト・窓ごとの 前処理→推論→判定→通知（infer_task.c。ll_aton を呼ぶのはこのタスクだけ）
20	reporter	UART 出力の唯一の書き手（メッセージバッファから受けて出す）＋1秒レート表示
25	task_lcd	LCD の初期化と描画（lcd_task.c。検出をクラス名で出し、3秒後に待機表示へ戻す）
32	dump	トレース状態機械・CSV ダンプ
task_infer の優先度と窓あたりの処理時間は対照実験のスイッチ（infer_task.h の INFER_PRIO_INVERT / INFER_SLOW_X）で変えられる。上の表は既定（条件 A ＝本番）。infer_task.c・tap_ring.c・lcd_task.h・audio_task.c の優先度の順序に関するコメントも A の記述。詳細は下の「対照実験のスイッチ」
task_2（赤 LED の点滅と "task 2" 表示）はタスク9 で削除した。赤 LED（PG10）は検出の通知に使う（Application/aed/notify.c）
DMA コールバックは TRACE → カウンタ更新 → tk_set_flg だけ行い即 return
UART に書くのは reporter だけ（タスク9）。他のタスクは log_printf（Application/trace/log.c）で1行を文字列にしてメッセージバッファへ送る。tk_snd_mbf は必ず TMO_POL で、いっぱいなら捨てて数える（音声・推論を UART の速さで待たせない。ミューテックスは使わない）。log_init 前（起動時の初期化・npu_selftest・extflash）はその場で直接出る。CSV ダンプ中（trace_muted）は reporter が捨てる
log_printf の書式は tm_printf の部分集合を自前で実装したもの（newlib の snprintf は malloc を起こすので使えない）。1行 160 文字で切り捨てるので、長い行は分けて出す。書式と引数の数が合っているかは、log.h の宣言に一時的に __attribute__((format(printf,1,2))) を付けてビルドし -Wformat-extra-args だけを見る（UW が unsigned long なので %u は常に警告になる。付けたままにしない）
出力バッファが不足したら無音を詰めて必ず全体を埋める
PCM リング（pcm_fifo）は SPSC。消費者を増やせない。推論用は音声タスクが別リング（tap）にもコピーする
タップリング（audio/tap_ring.c）: int16 x 32768（約2秒）。書き手は task_pcm の mic_process_half（pcm_fifo へ入れたのと同じ値、待たない）、読み手は task_infer だけ。head は累計サンプル数で単調増加。窓は 15600 サンプル、次の窓は 15360 後（ST と同じ、重なり 240）。読み手が待つ位置を書き手が越えたときだけセマフォを1回 signal する。上書きは取り出し前（E_OBJ）とコピー中（E_IO）で別に数え、読み位置を最新の窓へ飛ばす。書き手・読み手の順序は __DMB() で保つ（根拠は tap_ring.c の先頭）
実装規約
タスク間のデータ受け渡しは mbx / mbf。ISR→タスクの通知はイベントフラグかセマフォ
複数文脈から触るカウンタは DI/EI で最小区間を保護（BASEPRI=1 で PendSV もマスクされるのでタスク間排他にも効く）
共有データの排他が要るときは tk_cre_mtx に TA_INHERIT（優先度継承）
時間計測は DWT CYCCNT（NOW()）。差分は必ず UW 同士で引く。600MHz で約7.16秒で折り返す
タスクのスタック溢れはフォルト機構で捕まらない（USE_SPMON 無効）。大きなローカル配列を置かず静的領域を使う
ライセンスヘッダ（T-License 2.2、ST の各ライセンス）を消さない
デバッグ基盤
フォルト可視化（Application/fault/）: 起動時にベクタテーブルを RAM にコピーし、未実装 IRQ 180本とフォルト例外5本を差し替え。 CFSR/BFAR/スタック上の PC を UART 直叩きで出してから停止。デバッガ接続時は __BKPT で止まるので F8 で続行
FAULT_TEST（fault.h）: 0=無効 / 1=BusFault / 2=ゼロ除算 / 3=未実装IRQ / 4=STKOF。コミット時は必ず 0
トレース（Application/trace/）: TRACE(id,arg) で (CYCCNT, id, arg) を記録。trace_start(ms) で区間記録し、終了後に CSV ダンプ。 ダンプ中は trace_muted() で他の出力を抑制
テスト用スイッチ: AUDIO_PRIO_TEST（音声タスクを最低優先度にして負荷タスクを回す）、FLASH_PROBE（0x70180000 読み出し確認）。 コミット時は 0 に戻す。 NPU_RISAF_DUMP（npu_hw.c、RISAF の状態表示。読むだけ）と NPU_PT_TEST（infer_task.c、パススルー中に乱数入力の推論10回。タスク7 の予行）はタスク9 で本番の推論が入ったので 0。 PREPROC_TEST（infer_task.c、前処理を PC と突き合わせる。タスク8）は 1、AED_USE_TEST_CLIPS（npu_selftest.c、30 本で NPU を判定）は ROM の都合で 0。 LCD_DIAG（lcd.c、LTDC / RIF / PWR / GPIO のレジスタを表示。読むだけ）はコミット時は 0。 INFER_PRIO_INVERT / INFER_SLOW_X（infer_task.h、対照実験の条件 B / D。既定 0 / 1 ＝本番 A。コミット時は既定に戻す。下の「対照実験のスイッチ」を参照） AED_MODEL（npu/aed_model.h、モデルの切替。AED_MODEL_ESC10 / AED_MODEL_FSD50K。既定は FSD50K。model_fsd50k/network.c が無い環境では __has_include で ESC10 に倒れる）
既知の罠
tm_printf はカーネル起動前（knl_start_mtkernel より前）に使えない。 起動前の初期化関数は Error_Handler() を呼ばず、結果を変数に記録してカーネル起動まで到達させる
FSBL がペリフェラルを触った状態でアプリが起動する。 HAL_xxx_Init が HAL_ERROR を返したら __HAL_RCC_xxx_FORCE_RESET()/RELEASE_RESET() で戻してから初期化（MDF1 で発生。XSPI2 は最初からリセットしてから初期化している）
カーネル起動後は SysTick がカーネル（knl_systim_inthdr）に渡り HAL_IncTick() が呼ばれない。 そのままだと HAL_GetTick() が止まり、HAL のタイムアウトが効かない（失敗時に永久待ち）・HAL_Delay() が戻らない。 usermain の周期ハンドラ（10ms ごとに HAL_IncTick() を10回）で補っている。カーネル起動後に HAL のタイムアウト付き API を使うのはこれを起動してから。 分解能は 10ms なので、タイムアウトが 10ms 未満の HAL 待ち（RCC の PLL/HSI 起動 1ms など）をカーネル起動後に呼ぶと誤タイムアウトし得る。PLL の設定はカーネル起動前（main.c）で行う
STM32N6 に DMA_CIRCULAR は無い。 循環DMAは HAL_DMAEx_List_* で組み、ノードは .noncacheable に置く
キャッシュ: CPU が書いて DMA が読む前は SCB_CleanDCache_by_Addr、DMA が書いて CPU が読む前は SCB_InvalidateDCache_by_Addr
割り込みを有効化したらハンドラを必ず用意する。 HAL_MDF_AcqStart_DMA は飽和/overrun 割り込みを自動で有効化する
BSP2 FSBL の EXTMEM(SFDP) 初期化はこのボードで失敗している (SFDP ヘッダ読み出しでタイムアウト、ManuID=0。IR=0x5A のまま SR.BUSY が残る)。FSBL は XSPI2 のカーネルクロックに HCLK を選んでおり、アプリで HCLK を選んだときと同じ症状なので、原因も同じと見ている (FSBL 側は未検証)。Debug 起動では問題にならないが、外部フラッシュをアプリから読むにはアプリ側で XSPI2 を初期化する必要がある
外部フラッシュ（MX66UW1G45G）は Application/extflash/ で DTR-OPI メモリマップにする。 OTP の HSLV_VDDIO3 を焼いていないので ST 版の 200MHz は使わない。 SCLK = IC3 200MHz（PLL1 1200MHz / 6）/ (EXTFLASH_PRESCALER + 1) = 50MHz。fuse_vddio() は移植しない
XSPI2 のカーネルクロックは IC3 にする（EXTFLASH_KERCLK_IC3=1。ST 版と同じ選択）。 HCLK（FSBL と同じ選択）だと周波数は同じ 200MHz でも、命令だけのコマンドで SR.BUSY が落ちず全コマンドが TIMEOUT する（プリスケーラ 255 でも同じなので速度起因ではない）。 IC3 に替えて動作した（2026-09-21 実機）。 同時に入れた EXTFLASH_FIX_*（VDDIO3 1.8V レンジ・XSPIM 明示設定・XSPI PHY クロック）が要るかは未確認
NPU の初期化は Application/npu/npu_hw.c（usermain から extflash_init の後に1回）。 RIF は RIMC（NPU マスタを CID1・セキュア・特権）と RISC（NPU レジスタをセキュア・特権）を両方設定する。RISC がセキュアでないと RIMC の MSEC は無視され、NPU のアクセスは非セキュアに強制されて既定の RISAF で拒否される。RISAF は設定しない（ST 版も呼んでいない）
NPU のクロックは FSBL の設定のまま IC6 = PLL1/4 = 300MHz、NPU RAM（AXISRAM3〜6・NPU キャッシュ）は IC11 = PLL1/3 = 400MHz。ST 版（800/800MHz）より遅いので、ST の推論時間の数値は流用できない。アプリからは変えない
ST 版はブートで MEMSYSCTL MSCR.DCACTIVE が 0 になるとして、キャッシュ有効化の前に立てている。本アプリは立てていない。0 なら CCR.DC=1 でも D キャッシュは効いていない。実機の値は npu_hw_init の表示で確認する（未確認）
ST の GettingStarted-Audio を実機でそのまま動かさない。 起動時に OTP ヒューズを不可逆に焼き、外部フラッシュの FSBL・アプリも上書きする
ST の FreeRTOS 版コードを持ち込まない。 全 IRQ の優先度を上書きする処理がある
NPU 推論ランタイムは Application/npu/st/（ST のファイルを無改変で同梱。SLA0104。ST Edge AI Core 4.0.1 の Middlewares/ST/AI から。ll_aton atonn-v1.1.3-275、NetworkRuntime1201_CM55_GCC.a。2026-09-27 に GettingStarted 同梱の 4.0.0 版（1.1.3-262、1200、SLA0044）から入れ替えた。生成した network.c は冒頭の #if で ll_aton の版を要求するので、ランタイムとモデルの生成は同じツール版でそろえる。両モデルとも 4.0.1 で生成済み）。つなぎは npu_rt.c、ST の npu_cache.c（HAL_CACHEAXI 依存）の代わりが npu_cache_port.c。st/ 以下は編集しない。models/network_generate_report.txt は SE モデルのレポートで AED のものではない
モデルの切替（5-1、ブランチ model-fsd50k）: Application/npu/aed_model.h の AED_MODEL。モデル固有物は Application/npu/model_esc10/（network.c / network.h / stai_network.c / stai_network.h の生成物、aed_model_cfg.h、aed_test_input.h、生成する aed_ref_clips.h / aed_test_clips.h）と model_fsd50k/（tflite・config yaml・LICENSE.md（Apache-2.0）・aed_model_cfg.h・aed_test_input.h・README.md。network.c / network_weights.c は ST Edge AI Core 4.0.1 の stedgeai で生成済み＝再生成は bash scripts/stedgeai/generate_fsd50k.sh、手順は model_fsd50k/README.md。ESC10 版の再生成は generate_esc10.sh <GettingStarted-Audio>）。ビルドに入る生成物は aed_model_network.c / aed_model_stai.c が #include した 1 組だけ（model_<name>/ は .cproject で除外。Debug/ の mk 系からも st/model を外した）。aed_model_cfg.h が持つもの: AED_MODEL_NAME / AED_MODEL_DESC / AED_CLASSES / AED_CLASS_NAMES / AED_CLASS_LIST_STR / AED_DISP_NAMES（LCD）/ NOTIFY_CLASSES / NOTIFY_CLASS_NAMES / NOTIFY_COOLDOWN_S / 生成物のパス（AED_MODEL_NETWORK_C 等。npu/ からの相対）/ セルフテストのロジット番地（AED_LOGIT_HOOK。FSD50K は生成後に入れるまで 0）。入力の量子化 scale/zp は preproc.h の定数をやめ、npu_rt_init が stai_network_get_info から読んで（npu_rt_input_quant）推論タスクが preproc_set_quant で渡す（ESC10 0.0305305421/33、FSD50K 0.0555472746/38。NPU が無いときは前処理テストだけ aed_ref_clips.h の値で動く）。生成ヘッダの AED_TEST_SCALE/ZP・AED_REF_SCALE/ZP と実行時の値の一致はセルフテストが表示・判定する。起動ログ: CONFIG: 行の model=、npu_rt_init の aed model: / aed classes: / aed input quant: の 3 行、notify: の cooldown 行。FSD50K のクラス順（昇順 Crying_and_sobbing / Glass / Gunshot_and_gunfire / Knock / Speech）は ESC-50 のクリップで実測して確認済み（README.md）。重みは AXISRAM4 0x34270000〜（144.9KB、network_weights.c の .npu_weights セクション。上の例外一覧の .ld）、作業域は AXISRAM6（144KB）。FSD50K の SW epoch は 15 整数 softmax と 16 DequantizeLinear で、int8 ロジットは 0x34350200（aed_model_cfg.h の AED_LOGIT_*。float の softmax 入力は無いので AED_LOGIT_F_ADDR 0 で逆算の確認は飛ばす）
ll_aton は POLLING でも stai_runtime_init() の中で NPU0_IRQn を NVIC 有効にする（エラー通知用、優先度 0 = DI で止まらない）。npu_rt_init() が直後に無効へ戻している。NPU 割り込みは使わない
ll_aton の LL_ATON_Init は NPU のバージョンが 0 の間読み直し続けるので、NPU にクロックが無いと戻らない。npu_rt_init() は npu_hw_ready() のときだけランタイムを初期化する
ll_aton のエラー経路は newlib の printf / puts / assert を使う。newlib の malloc のヒープ（sysmem.c の _sbrk、_end から）は μT-Kernel のシステムメモリ（_end から）と重なるので使えない。npu_rt.c で __io_putchar（fault の UART 直接出力）と __assert_func（表示して fault_halt）を定義し、stdout を無バッファにして malloc を起こさない。newlib の malloc / バッファ付き stdio を使うコードを入れない
推論のタイムアウトは ll_aton の弱いシンボル checkWatchdog() を npu_rt.c で定義して DWT で判定し、超えたら longjmp で npu_rt_run() に戻す（ll_aton は推論の途中の状態のまま。後始末は未実装で、以降の npu_rt_run は E_IO）。タイムアウト時は実行中の epoch block・ストリームエンジン・INTREG を表示する。LL_ATON_ASSERT の中で呼ばれるので NDEBUG を定義すると効かなくなる（npu_rt.c で #error）
POLLING の推論中は、呼び出したタスクが LL_Streng_Wait で CPU を回し続ける。それより低い優先度のタスクは推論のあいだ動けない
usermain は μT-Kernel の初期タスクで、スタックが 1KB（INITTASK_STKSZ、mtkernel/include/sys/inittask.h）しかなく優先度は 1。スタックを多く使う処理（NPU ランタイムは 1KB 超）や時間のかかる処理は専用タスクで行う。npu_rt_init / npu_rt_run は task_infer（スタック 8KB）からだけ呼ぶ
全タスクに TA_FPU が付く（config.h の ALWAYS_FPU_ATR=1）。float を使うタスクに属性を足す必要は無い
推論の入力を書いたら SCB_CleanInvalidateDCache_by_Addr（clean だけでは不足。入力の領域は推論中に中間結果と出力の置き場に再利用される）。出力は最後の SW epoch が CPU で書くので invalidate 不要（npu_selftest.c の run_once）
AED の末尾: epoch 29（NPU、Gemm）→ int8 x10 を 0x34350000 → epoch 30（SW、DequantizeLinear。scale/zp は外部フラッシュ 0x704a1590 / 0x704a1760）→ float x10 を 0x34350440 → epoch 31（SW、Softmax）→ 0x34350410。epoch 31 が 0x34350000〜 を作業域に使うので、推論後に int8 ロジットは残らない。途中の値は npu_rt_set_epoch_hook で取る（npu_selftest.c の logit_hook）
PC の参照（aed_ref.py）の int8 ロジットは、ORT の最適化あり（int8 演算）となし（float 演算）で最大 15 LSB 違う（上位クラスでは 2 LSB）。NPU との比較の許容幅はこれを踏まえて決める
NPU の正しさの判定は ESC-10 の実録音 30 本の1位を PC と比べる（npu_selftest.c の clips_check、入力は scripts/aed_clips.py が生成する aed_test_clips.h）。乱数入力は分布外で上位2クラスが拮抗し、丸めの積み重ねで確率が動くので参考値だけ
aed_test_clips.h・aed_ref_clips.h は ESC-50 由来なのでコミットしない（.gitignore）。無ければボード側は __has_include でその判定を飛ばす（NOT JUDGED / SKIP）。ヘッダ無しでビルドした後は .d にヘッダが載らず make が作り直さないので、aed_clips.py が npu_selftest.c と infer_task.c の更新時刻を進める
ROM が足りないので、30 本の判定（aed_test_clips.h、入力だけで 184,320B）と前処理の突き合わせ（aed_ref_clips.h、2 本の生 PCM + テンソルで 74,688B）は同時に載せない。切り替えは npu_selftest.c の AED_USE_TEST_CLIPS と infer_task.c の PREPROC_TEST。前処理側だけを 1 にした状態でコード領域 348,988B / 511KB（66.7%。タスク9 まで込み）、30 本側だけなら +90KB 程度
epoch フック（npu_rt_set_epoch_hook）は今は呼ばれない。stai_network_run（ll_aton_stai_internal.c:417-425）が推論のたびに epoch コールバックを NULL か stai 自身のものに設定し直すため。直すなら stai_network_set_callback() で登録する（未実施）
前処理 log-mel の仕様（ボードの実装は Application/aed/preproc.c＝タスク8。PC 実装は scripts/aed_clips.py の logmel_q8。ST の値は GenHeader/user_config_aed.yaml → Dpu/ai_model_config.h.aed・user_mel_tables.c.aed）
  入力: int16 16kHz の先頭 15600 サンプル。列 i（0〜95）はサンプル [160i, 160i+400)
  列ごと: x/32768（arm_q15_to_f16）→ 周期ハン窓 400（0.5-0.5cos(2πn/400)）→ 左右 56 ずつゼロ詰めして 512 点 rfft → 振幅 |X| 257 本（MAGNITUDE。2乗しない）→ メルフィルタ 64 本（librosa の mel、htk=True・norm=None・125〜7500Hz。非ゼロ係数 461 個を start/stop 番号で持つ）→ ref=1.0 で割る → 0 以下は FLT_MIN → 自然対数（dB ではない。TopdB の切り捨て無し）
  量子化: int8 = SSAT(roundf(logmel × (1/0.0305305421) + 33), 8)。roundf は 0.5 を 0 から遠い側へ丸める（numpy の rint は偶数丸めなので違う）
  並び: p_spectro[i + 96×j] = 列 i・メル j（preproc_dpu.c:135-143 の転置）= NPU 入力 1x64x96x1 の [メル][列]
  ST の実装箇所: preproc_dpu.c:33-80（初期化。ゼロ詰め :52-53、Ref/TopdB :69-70）、feature_extraction_f16.c:264 LogMelSpectrogramColumn_q15_f16_Q8（量子化 :334-337）、audio_din_f16.c:30、mel_filterbank_f16.c:214。窓とメルの表は aed_clips.py が毎回 ST の表と照合する（差は窓 2.8e-8、メル 5e-12）
  ST は FP16 で計算する（app_config.h の PREPROC_FLOAT_16。inv_scale も FP16 に丸めて 32.75）。PC 実装とボードへの移植は float32 の想定で、int8 で 1 LSB 程度ずれ得る
  ESC-10 の 30 本で PC の1位が正解と一致 29/30（最適化あり・なし同じ）。前処理・並び・クラス順が正しい裏付け（外れは静かな crackling_fire 1本が clock_tick）
  ボードの実装（preproc.c）は CMSIS-DSP を使わず、窓・ツイドル・メルフィルタの表を preproc_init() が double で作って float32 で持つ（ROM を使わない。表の係数は 461 個で ST・PC と同じ）。FFT は 512 点の複素 radix-2（実部に信号、虚部 0）。量子化は ST と同じ順序（roundf(v × inv_scale + zp) → SSAT）
  移植の確認は infer_task.c の preproc_test（PREPROC_TEST）。aed_ref_clips.h の生 PCM 2 本をボードで log-mel にして PC のテンソルと 6144 要素すべて比べ、そのテンソルで推論して1位を PC と並べる。前処理と NPU の切り分けのため PC のテンソルでの推論も行う。差が 1 を超える要素があれば移植が違う（float32 と float64 の差では出ない）
  preproc.c と同じ計算を float32 で書き直した PC 版（使い捨て。未コミット）を PC のテンソルと比べたところ、2 本とも 6144 要素すべて一致した＝表の作り方・FFT・メル・量子化・並びは合っている。実機は未確認（GCC が積和を VFMA にまとめる・newlib の logf が numpy と 1ulp 違うと、丸めの境界の要素が 1 LSB 動き得る）
窓ごとの本番動作（タスク9。infer_task.c の task_infer）
  tap_ring_get_window → preproc_run（log-mel）→ npu_rt_infer → notify_decide → notify_window（JSON と LED）。推論の入力は静的な in_tensor（6144B）で、前処理セルフテストと共用する
  判定は ST と同じ向き（audio_bm.c:488）: 最大確率 > AED_OOD_THR（notify.h。0.7）なら そのクラス、そうでなければ unknown。ST の CTRL_X_CUBE_AI_OOD_THR は 0.5 だが 3.5（2026-09-27）で 0.7 に上げた。根拠: 静かな部屋 10 分（logs/uart_20260927_130250.log、A・3 クラス・642 窓）で通知 271 件、うち sneezing 199 件（p は 0.5 台 63 / 0.6 台 63 / 0.7 台 49 / 0.8 台 18 / 0.9 台 6）。peak gate -30 dBFS と組み合わせた残数はしきい値 0.5 で 17、0.7 で 6、0.8 で 1。残る 6 件は peak -10〜-25 dBFS で実際に音があった窓。0.8 にしないのは 1-Ex で sneezing のクリップ 1 本が p=0.76 だったため
  通知は1行の JSON: {"win":123,"cls":"dog","p":0.87,"lat_ms":53,"under":0,"over":0,"late":0}。検出は毎窓出し、unknown は状態が変わったときだけ出す（連続する unknown は数えるだけ。notify_stats の held）
  通知するクラスは notify.h の NOTIFY_CLASSES で絞る（既定 dog 4・crying_baby 3・sneezing 9 の 3 クラス。crackling_fire 2 は 1-Ex の対照試験の結果で 2026-09-27 に外した。起動ログは notify: 3 target classes (dog crying_baby sneezing), threshold p>0.70, peak gate -30 dBFS (peak >= 1036 of 32768) と notify: rel gate rms >= floor + 10 dB, floor = p10 of the last 60 windows' rms (not applied until 60 windows) の 2 行。logs/uart_20260927_142118.log で 1 行目を実機で確認済み）。対象外のクラスが1位のときは LED も JSON も出さず、数だけ集計行の offlist に出す。番号が出力順とずれていないかは NOTIFY_CLASS_NAMES との照合で notify_init が確かめる
  音量の門は NOTIFY_GATE_PEAK_DBFS（既定 -30。-99 で切）。窓のピーク（win 行の peak と同じ値。RMS ではなくピークなのは犬の1声やくしゃみのような短く鋭い音を落とさないため）がこの dBFS 未満なら unknown 扱いにする。しきい値は notify_init が dBFS から int16 の振幅に直して持つ（毎窓 log を取らない）。止めた数は集計行の gated_abs。止めた窓は win 行の次の行が -> sneezing p=0.75 (gated abs) のように印付き（JSON も LED も出ない。notify_last_note の文字列）。-30 の根拠: 上の 10 分ログの誤報の窓は rms -48 dBFS 前後・peak -37〜-35 dBFS で 10 分間一定（冷蔵庫の定常音と推定）。ゲートなし 202 窓 → -30 dBFS で 17 窓（しきい値 0.5 のまま）。限界: 音量で切るので、冷蔵庫に近い設置や換気扇では -30 dBFS を超えて通る。根本対策は背景音クラスを持つモデル。その限界がそのまま出たのが次の相対の門の背景
  暗騒音に対する相対の門（3.5-2、2026-09-27）: 窓ごとの rms_db（20log10(rms/32768)。rms は win 行の rms= と同じ整数）を直近 NOTIFY_FLOOR_WINDOWS（60）窓ぶんリングに持ち、その NOTIFY_FLOOR_PERCENTILE（10）パーセンタイル（昇順 6 番目）を floor として、rms_db >= floor + NOTIFY_GATE_REL_DB（10）でなければ止める（集計行の gated_rel）。履歴が 60 窓に満たない間（起動から約 1 分）はこの条件を省く。floor は判定のあとに更新する（現在の窓は自分の floor に入らない）。win 行の次の行の印: 通した窓 (floor -42.6 Δ+17.0)、止めた窓 (gated abs) / (gated rel floor -42.6 Δ+6.2)。Δ = rms_db - floor。門は notify.c の notify_gate（純粋関数。順に BELOW_THR → OFFLIST → GATED_ABS → GATED_REL → PASS。対象外のクラスを門より先に外すので gated_abs + gated_rel + PASS ＝ 対象クラスで閾値を超えた窓、offlist ＝ 対象外のクラスが 1位だった窓。3.5 の gated（門が先で対象外クラスの窓も含む）とは数え方が違う）、floor の履歴は notify_floor / notify_floor_update。FINAL の notify 行は gated= が gated_abs= gated_rel= に分かれた（analyze_31.py は両方読む）。根拠: 1 時間走行 logs/uart_20260927_161828.log で通知 82 件/61 分（80 件/時）が全部 rms -44〜-33 dBFS の暗騒音の窓で、peak が -30 dBFS をわずかに超えたもの（54 件が -30〜-27）。この条件を足すと 82 → 4（残る 4 件は Δ10〜18 dB の実際の音）。1-Ex 再走行 logs/uart_20260927_142118.log（READY 以降）は 24 → 19（落ちる 5 窓は Δ3.0〜7.3 dB。クリップの検出は残る）、対策前 10 分ログ 130250 は 6 → 3。限界: 54 窓（約 52 秒）を超えて鳴り続ける音は floor を押し上げ、以降その音は相対の門で止まる
  「通知しない状態」（unknown・対象外・門で止めた）が続くあいだ JSON は出ない。状態が変わって unknown になったときだけ1行出る
  同じクラスの連続を条件にする、はまだ入れていない。クールダウン（5-1）: クラスごとの NOTIFY_COOLDOWN_S（秒。model_<name>/aed_model_cfg.h。FSD50K の Speech 30、他は 0）。通知してから (S*16000+15359)/15360 窓（30 秒 → 32 窓 = 30.7 秒）は同じクラスが門を通っても通知しない（verdict NOTIFY_COOLDOWN。JSON / LED / 画面なし、集計行の cooldown、win 行の印 (cooldown N win left)。「通知している状態」は続くので prev_cls はそのままで unknown の行も出ない）。notify_gate（純粋関数）の外で notify_window が見る。FINAL の notify 行に cooldown= が増えた（analyze_31.py は無くても読む。notify_replay.py は --model fsd50k で同じ規則）
  lat_ms は「窓の最後のサンプルを tap_ring に書いた時刻（TAP_WIN_INFO の t_ready）」から「JSON をレポータに渡す直前」まで。窓の 975ms 自体は含まないので数十 ms になる。UART に出るまでの待ちは含まず、その分は reporter の行の loglag=（log_lag_max_us）に出る。両方足したものが実測の通知遅延
  loglag は CSV ダンプのあと「レポータがキューを出し切った時点」で 0 に戻す。ダンプ中はレポータが行を捨てるだけで測らず、ダンプの前後に溜まった行はダンプ直後にまとめて出るので、終わった瞬間に 0 にしてもその行たちでまた最大値が立つ（実機のログ 4402 行目で確認）。dump_all の末尾で log_lag_reset_when_idle() で予約し、レポータが TMO_POL で空を見つけたときに log_lag_note_idle() で下ろす。前処理セルフテスト（と NPU_PT_TEST の予行）の直後も同じ予約をする。優先度15 の推論タスクがセルフテストで 300ms ほど CPU を離さず、その間に溜まった行の待ちで最大値が立つため（ダンプ直後のリセット自体は実機で効いている＝8432us を確認）
  LED は赤（PG10）。検出で点灯し、1秒後にアラームハンドラで消す。続けて検出したらアラームを張り直す。unknown では点けない
  起動時の確認（トレースの記録と CSV ダンプ・前処理セルフテスト・予行）が全部終わって本番の窓ループだけになったところで READY のブロックを1回出す（infer_task.c の show_ready_banner。win 行が再開する前の位置）。ログを後から読むとき、ここより後だけを見ればよいと分かるようにするため
計測にかかわる出力は ==== の2行で囲む（log.h の log_block_begin / log_block_end。区切り2行＋見出し＋区切り2行、本文、区切り2行）。見出しは READY / NPU SELFTEST / PREPROC TEST / TAP TOTAL win=N / PASSTHROUGH STOPPED / FINAL。READY は本文が無いので begin だけ呼ぶ。本文の書式と数値は囲む前と同じまま（既存のパーサを壊さない）。区切りのぶん1ブロックで 4〜5 行増えるので、キューの深さ（log.c の LOG_DEPTH）は 48 にしてある（前処理セルフテストが本文 20 行ほど＋区切りを一度に出す）
  tap の見張り（no window for 2000ms → tap final）は最初の窓を取れてから働かせる。起動直後はビープとプリフィルでサンプルだけが溜まり窓がまだそろわないので、head>0 だけを見ると偽の final が出ていた
対照実験のスイッチ（フェーズ3 タスク3-1。npu/infer_task.h）
  条件 A / B / D は #define で切り替える。既定値は本番と同じ（INFER_PRIO_INVERT=0、INFER_SLOW_X=1）。条件ごとのビルドで書き換えるのはどちらか 1 行だけ。共通のコードは変えない（B は INFER_TASK_PRI の値だけ、D は #if INFER_SLOW_X > 1 で囲った空回しと集計 1 行だけが増える）。両方立てると #error。FSBL は触らない
  どの条件で走ったかは usermain が CONFIG: A production (INFER_PRIO_INVERT=0 INFER_SLOW_X=1 task_infer pri 15) の形で 1 行出す（usermain が出す最初の行。ログの物理的な先頭は app_fault_init の [FAULT] 5 行と [TRACE] 1 行で、その直後。log_init 前なので直接 UART に出て、後で reporter が飢えても消えない）。B は CONFIG: B prio-invert (... task_infer pri 3)、D は CONFIG: D slow-infer (... INFER_SLOW_X=10 ...)
  実測の基準（2026-09-24、logs/uart_20260924_180849.log の FINAL、条件 A 相当）: preproc max 69884us avg 69815us、infer max 41096us avg 40990us、10 分で windows=641 expected 641、under/over/late=0、log dropped=0。実測（2026-09-27、各 10 分、logs/uart_20260927_005956 / 011552 / 013950.log）は docs/3-1_results.md（scripts/analyze_31.py で再生成）。要点: B は audio under=2078 over=1200 late=1200（late/over は窓 600 × 2 ＝ 毎窓 +2 の予測どおり）、windows 600/600、in Hz 中央値 14848、tap write max 414ms、ログは止まらない。D は windows 558/642、overrun=66（8.5 窓に 1 回）、audio under/over/late=0、log dropped=3851、disp dropped=167、JSON lat_ms 中央値 1640ms、窓 1 つの実測 avg 1064ms、spin n=537 skipped=21、PASSTHROUGH STOPPED なし。D の 2 回目（uart_20260927_020751.log、表には使わない）は windows 557/642、overrun=67、log dropped=3846、disp max 7015440us で再現。下の期待結果はコードから導いた予測で、実測と大筋で一致した（B の窓数は予測の 570 より多い 600）
  条件	変更する行	ねらい	期待する結果
  A 本番	なし（音声 5/10、推論 15、reporter 20、表示 25、dump 32）	基準	10 分で audio under=0 over=0 late=0、tap overrun=0 skipped=0、logdrop なし、PASSTHROUGH STOPPED と FINAL が出る。CONFIG 行以外は従来のログと同じ形
  B	INFER_PRIO_INVERT (1)（推論タスクを優先度 3 ＝ task_pcm 5 より上）	優先度設計をしなかった場合の再現	窓ごとの 前処理+推論 約 110ms のあいだ task_pcm が動けない。MDF 半分 16ms・SAI 半分 25ms なので入出力とも HALF+FULL が同時に立ち、late と over が毎窓 +2 ずつ増える（「両方立っていた」1 回しか数えないので実際に失った量の下限）。sai_fill_half が止まる間は古いバッファが再生されて音が途切れる。復帰の 1 回で push 512・pop 800 なので FIFO（起動時の実測 ≈1280。PT_PREFILL_SAMPLES=800 はポーリングの閾値）が毎窓 288 減り、パススルー開始から 5 窓前後で under も出始めて以降毎窓増える。reporter の in=Hz 行の ring= は起動時の 1200 前後から 0〜数百の帯に落ちて揺れ（実測 0〜976、0 が 9 回）、in=/out= も 16128/16400 より下がる（実測の中央値 14848/15200）。tap の overrun/torn/skipped は 0 のまま（読み手が書き手より速い）、windows==expected。tap write max は 110ms 級になる（tap_ring_write の中の tk_sig_sem で推論に横取りされる壁時計。memcpy が遅いのではない。実測は典型 112ms、最大 414ms が 1 回＝約 4 窓ぶん横取りが続いた）。窓の間隔は約 1.05 秒に伸びる（毎窓 1250 サンプルほど取りこぼす）。ログ・LCD・JSON は止まらない（推論は 110ms で窓待ちに入り reporter が動ける）。PASSTHROUGH STOPPED と FINAL の両方が出る。under/over/late は READY 時点で既に非 0（リセットは passthrough_test の 1 回だけで、トレース中・前処理セルフテスト中も止まる。区間ごとの値は 500ms の pt[n] 行か PREPROC TEST の before/after 行で見る）。NPU_PT_TEST=1 とは併用しない（pt_report の合否が under/over/late 不変を要求する）
  D	INFER_SLOW_X (10)（推論のあとに DWT で（前処理+推論の実測）×(X-1) だけ空回し。infer_task.c の slow_spin）	推論が間に合わなくても音声は無傷、の証拠	窓 1 つが 69+41=110ms → 約 1.1 秒 > 窓の周期 960ms。次の窓が常にそろっているので tap_ring_get_window はセマフォで待たず（tap_ring.c の head の確認が先）、推論タスクは CPU を離さない。reporter・task_lcd・dump は READY の次の次の窓から 1 回も動けない（READY 直後の 1 窓だけはまだ待つので、その分の行は出る）。UART は READY 後の win 行 1〜2 つで止まり、LCD 左上の生存表示の数字も止まる。緑 LED（task_1、優先度 10）と赤 LED（アラームハンドラ）は動く。tap の読み手は毎窓約 2400 サンプル遅れ、8 窓ごとに E_OBJ（実測 8.3〜8.5 窓に 1 回。overrun +1、skipped は 1 回あたり約 19400 サンプル＝実測 19216〜19445、tap: window lost の行）。win 行の lag= が窓ごとに 150ms ずつ伸び（実測 +150.4ms）、tap lag max は約 1.08 秒（8 窓で上書きが来て戻るので 2 秒には届かない）。JSON の lat_ms は 1.1〜2.2 秒（中央値 1.6 秒）。audio の under/over/late は 0 のまま（task_pcm は推論より上）。FINAL は無改造で出る（停止後に残る 1〜2 窓を処理してから tap_ring_get_window が 2000ms 待つ間に reporter がキューを出し切り、そこへ FINAL が入る。停止から約 2 秒後＝tap の 2000ms の見張りぶんで A/B と同じ。停止後に残る窓の分は見えない）。FINAL に slow x10: window max= avg= | spin n= max= avg= skipped= の行が加わる（D のビルドだけ。window avg ≈ 1100000us が窓 1 つの実測、spin n ≈ windows、spin avg ≈ 990000us、skipped ≈ READY 前の窓数 20 前後）
  D の基準を「推論だけ」でなく「前処理+推論」にした理由: 推論 41ms × 10 = 410ms では前処理 69ms を足しても 960ms に届かず何も飢えない。窓ごとの推論パイプラインは preproc_run と npu_rt_infer で 1 つなので、その和 110ms を推論 1 回の時間とみなす（値は process_window の実測 pp_us+inf_us を毎窓使う）。空回しは npu_rt_infer の直後・notify_decide の前なので、JSON の lat_ms と notify の lat max に遅い推論の分がそのまま載る（1〜2 秒。7.16 秒の折り返しより十分短い）。npu_rt_run の外なので推論の見張り（100ms の壁時計）には掛からない
  D の空回しは READY の後（ready_shown）かつ trace_state()==TRACE_IDLE かつ trace_cyccnt_valid() のときだけ。READY 前に回すと 20 秒の起動確認（トレース 10 秒 + CSV ダンプ + 前処理セルフテスト）が伸び、dump（32）が動けなくなる。READY は FULL→DUMPING の隙間（trace_busy は FULL で FALSE。task_dump の 20ms ポーリング）にも出得るので IDLE も見る。1 回の上限は 3 秒（SLOW_SPIN_MAX_US。UW の差分は 7.16 秒で折り返す。X≥28 は頭打ち）。中で tk_dly_tsk 等は使わず DI/EI も掛けない（音声 ISR と周期ハンドラは動き続ける）
  D で失われるもの（仕様どおりの副作用。見て驚かないこと）: log のキュー（LOG_DEPTH 48 = 8064B）は空回し開始から 20〜30 秒で満杯になり、以降の行（win 行、JSON、TAP TOTAL、pt[] 行、1 秒の in=Hz、tap: window lost）はすべて捨てて log dropped= に数える。パススルー停止直後に task_audio（10）が出す PASSTHROUGH STOPPED ブロックも満杯のキューに入れられず出ない（同じ under/over/late は FINAL の audio 行にある。mdf_cb は FINAL の tap write の calls と同じ。失うのは sai_cb half/cplt・mdf_err・最後の pt[] 行だけ）。10 分の沈黙の後、まず空回し開始直後に溜まった古い行がまとめて出て（PC の時刻は 10 分後）、その in=Hz 行に logdrop= が最終値で付く（report_rate が出す時点の log_dropped() を読むため）。次に FINAL。loglag（log lag max）は無意味（10 分待った行の差分が 2^32 サイクルで折り返す）。tap lag max と notify lat max は 1〜2 秒で有効。disp は 6 件で満杯（bufsz 128B、1 件 4+16=20B）になり以降 dropped が増える。tap final の windows は expected より 1/8 ほど少ない（overrun の回数ぶん）
  B では tap_ring.c の前提（読み手は書き手を横取りしない）が崩れるが、データ経路は壊れない（横取りで古くなる want は TRACE の引数にしか使わない）。tap_ring_stats / audio_pt_counts の DI/EI のスナップショットは書き手が横取りされ得るので統計が 1 ずれ得る（実害なし。ロックは足さない）
  各条件の実験が終わったら infer_task.h を既定（0 / 1）に戻し、git diff で差分が無いことを確かめてからコミットする
LCD（Phase 2 タスク2-1。Application/lcd/）
  パネルは RK050HR18 800x480、LTDC 直結。STM32N657 に DSI は無い（stm32n657xx.h に DSI_TypeDef / DSI_BASE が無い。"DSI" のヒットは全部 SPI/XSPI の DSIZE）
  タイミングは rk050hr18.h（HSYNC/HBP/HFP/VSYNC/VBP/VFP すべて 4）。LTDC への入れ方は ST BSP の stm32n6570_discovery_lcd.c:352-380 MX_LTDC_Init と同じ（HS/VS/DE=AL、PC=IPC、各値 -1）。総画素 812x492
  画素クロックは main.c の MX_LTDC_Clock_Init（PLL4 = HSI 64MHz/4×75 = 1200MHz → IC16 /48 → LTDC 25MHz。リフレッシュ 62.57Hz）。FSBL は PLL4 を使っていない（PLL_NONE）ので衝突しない。BSP も LTDC←IC16←PLL4 を選ぶが PLL4 自体は設定しない（アプリの仕事）
  PLL4 の源は HSI。HSE でも動く（FSBL の main.c:209-210 が HSE を起動していて、MX_SAI1_Init の PLL2 が HSE 源で動作実証済み）が、HSI は PLL1＝CPU の源なので FSBL の HSE 起動に依存せず、M=4・N=75 が PLL1 と同じ値で VCO の条件も実証済みになる。アプリ側は発振器を起動しない（OscillatorType = NONE。SAI1・MDF1 も同じ）
  GPIO とパネルの制御線は BSP の LTDC_MspInit と同じ（信号線は AF14。PQ3 LCD_ONOFF / PQ6 LCD_BL_CTRL / PG13 LCD_DE を H、PE1 は出力にするだけ）
  ST BSP の LCD ドライバは同梱していない。BSP_LCD_InitEx が L8（パレット）を選べず（RGB565/RGB888/ARGB8888/ARGB4444 のみ）、DMA2D と設定ヘッダ一式を引きずるため、HAL LTDC を直接使う薄い初期化を lcd.c に書いた
  LTDC も NPU と同じく RIF の設定が要る（lcd.c の rif_config。これが無いと画面が真っ黒）。RIMC_ATTR[10]（RIF_MASTER_INDEX_LTDC1）に MCID=1・MSEC=1・MPRIV=1、RISC の reg3 bit7（RIF_RISC_PERIPH_INDEX_LTDCL1）を SEC|PRIV。番号の出典は STM32CubeN6 v1.3.0 の stm32n6xx_hal_rif.h:74,183、設定値は LTDC サンプル（Examples/LTDC/LTDC_Horizontal_Mirroring/FSBL/Src/main.c:309-314）と同じ。書かないと LTDC のフレームバッファ読み出しが RISAF6 に弾かれて 0 が返る（実機で RISAF6.IASR=0x2＝IAEF を確認。2026-09-24）。LTDC を有効化する前に IACR で IASR を消しておくと、通ったかを後から判定できる
  L8 の PFCR は 7 が正しい。PF フィールドは 3bit で、L8/ARGB1555/ARGB4444/AL44/AL88 は PF=0x7（LTDC_LxPFCR_PF）を書いて実際の形式を FPF0R / FPF1R で指定する（stm32n6xx_hal_ltdc.c:4037-4048）。HAL のマクロ LTDC_PIXEL_FORMAT_L8（0x9）は HAL 内部の識別子でレジスタの値ではない
  フレームバッファは L8 + CLUT 16色。CPU で描いてから lcd_flush_rows() で D キャッシュを clean する（LTDC は AXI から直接読む。1行 800B は 32B の倍数なので行境界＝ラインの境界）
  AXISRAM3 のクロックと電源は npu_hw_init() が入れている（npu_hw.c の NPU_MEMEN に AXISRAM3EN）。LCD 単独で動かすときはそこを確認する
検出の表示（タスク2-2）
  受け渡しは lcd_task.c のメッセージバッファ（8件）。notify.c が通知を出すところ（LED を点けるのと同じ箇所）から lcd_post(cls, p100, win, NOW()) を呼ぶ。tk_snd_mbf は TMO_POL で、いっぱいなら捨てて数える（JSON と同じ流儀。推論タスクを表示で待たせない）。判定そのもの（notify_decide と通知するかの条件）は変えていない
  画面は中央の帯（y=192、96行）にクラス名を Font24 の4倍（68x96 画素）で中央寄せ。英字はモデルの aed_model_cfg.h の AED_DISP_NAMES（lcd_task.c の disp_name はそれ。FSD50K は Knock→KNOCK / Glass→GLASS / Crying_and_sobbing→CRY / Speech→VOICE / Gunshot_and_gunfire→GUN。ESC10 は dog→DOG、crying_baby→BABY、sneezing→SNEEZE、crackling_fire→FIRE）。待機は "READY"
  検出から LCD_HOLD_MS（3秒）は保持し、その間に別のクラスが来たら上書きする。3秒経ったら待機表示に戻す。unknown は送らない（画面は時間で戻す方式なので、送ると3秒より早く消えてしまう）
  描くのは書き換える帯だけで全画面は消さない。clean も その帯だけ（1行 800B が 32B の倍数なので帯の境界＝キャッシュラインの境界）
  生存表示は左上（y=8、24行）に1秒ごとに 0〜9 が変わる数字。止まると同じ数字のままになる
  待ちは「次にやること（生存表示の更新か保持の解除）までの残り時間」を tk_rcv_mbf のタイムアウトにする（無駄に起きない）
  通知から描き終わりまでは集計行の disp max / avg（lcd_post_stats）。フェーズ3 で「通知遅延 + 画面」と並べて書く
PowerShell 5.1 用スクリプトは UTF-8 BOM 付きで保存する（BOM 無しだと日本語コメントで param() が壊れる）
ビルド設定
Appli プロジェクトは親の Drivers/STM32N6xx_HAL_Driver/Src/ を .project で個別参照している。新しい HAL を使う場合:
stm32n6xx_hal_conf.h の HAL_xxx_MODULE_ENABLED を有効化（例外一覧に追記）
CubeIDE で New > File > Advanced > "Link to file in the file system" から .c を追加（_ex.c も）。 追加後に .project を開き、その <link> が <locationURI>PARENT-1-PROJECT_LOC/Drivers/... になっているか確認する。 <location>C:/Users/... の絶対パスになっていたら書き直す（絶対パスだと別の場所に clone したときにビルドできない）
Clean → Build
HAL_RAMCFG / HAL_RIF / HAL_CACHEAXI は Drivers/ に .c も .h も無い。 使わずにレジスタを直接操作する（Application/npu/npu_hw.c と npu_cache_port.c。手順は ST の HAL を参照してコメントに記載）
NPU ランタイムの定義・インクルードパス・ライブラリは .cproject の Debug 構成に直接書いた（例外一覧）。Debug（-O0）のままでコード領域 225,760B / 511KB（2026-09-22、初期化まで。推論を呼ぶと数KB増える）
.cproject を CubeIDE の外で書き換えたら、CubeIDE でプロジェクトを Refresh（F5）してからビルドし、Debug/ の mk を作り直させる（scripts/build.sh は既存の mk を使うだけで .cproject を読まない）
Application/ 配下に新規ファイル・フォルダを作ったら CubeIDE でプロジェクトを Refresh（F5）
Debug/ 配下の mk 系は CubeIDE が生成する。手動編集しない。ビルド対象の追加・除外は GUI で行い .cproject に永続化する
Drivers/ に HAL のファイルを足したときも Debug/Drivers/STM32N6xx_HAL_Driver/subdir.mk に4か所（C_SRCS・OBJS・C_DEPS・個別ルール2行と clean）と objects.list が要る。LTDC を足したときは既存の xspi の行を雛形にした
CubeIDE を開けないまま新しいフォルダ（例 Application/aed/）を scripts/build.sh でビルドするには Debug/ の4か所が要る: sources.mk の SUBDIRS、makefile の -include <dir>/subdir.mk、objects.list（リンクは OBJS ではなくこの静的な一覧を使う。*.list なので git 管理外＝手元だけの変更）、<dir>/subdir.mk（既存フォルダのものをコピーしてファイル名を差し替える。行末は CRLF だが継続行とレシピ行だけ LF）。次に CubeIDE で F5 すれば同じ内容が作り直される
Git
コミットメッセージは日本語。1行目は Conventional Commits（feat:, fix:, refactor:, docs: など、スコープは audio/fault/trace/npu 等）、空行、なぜ変えたか
論理単位でステージする
基準点にタグ: phase0-baseline（10分連続 under/over/late=0、応答1〜2μs、CPU占有0.30%）
このファイルについて

開発中の制約メモ（Claude Code 向け）。人間向けの説明は README.md を参照。 記述が実態と食い違ったら、コードではなくこのファイルを直す。