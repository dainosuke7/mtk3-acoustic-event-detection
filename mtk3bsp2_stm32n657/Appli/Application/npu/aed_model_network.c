/*
 * AED_MODEL で選んだモデルの network.c (stedgeai の生成物) をビルドに入れる (aed_model.h)。
 * 生成物そのものは model_<name>/ に無改変で置き、このファイルだけがコンパイルされる。
 * 重みを C の配列で持つモデル (FSD50K: raw2c.py が作る network_weights.c、.npu_weights セクション) は
 * それも一緒に入れる (cfg の AED_MODEL_WEIGHTS_C。ESC10 は外部フラッシュなので無い)
 */
#include "aed_model.h"
#include AED_MODEL_NETWORK_C
#ifdef AED_MODEL_WEIGHTS_C
#include AED_MODEL_WEIGHTS_C
#endif
