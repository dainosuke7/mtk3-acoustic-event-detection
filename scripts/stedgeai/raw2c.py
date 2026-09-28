#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# ///
"""stedgeai が出した重みの raw ファイル (network_atonbuf.<pool>.raw) を、決まった番地に置く C の配列にする。

    uv run python scripts/stedgeai/raw2c.py <raw> <out.c> <address>
    例: uv run python scripts/stedgeai/raw2c.py st_ai_output/network_atonbuf.AXISRAM4.raw \\
            mtk3bsp2_stm32n657/Appli/Application/npu/model_fsd50k/network_weights.c 0x34270000

network.c は absolute_mode (メモリプールの番地を直接参照する) なので、重みはその番地に無ければならない。
外部フラッシュ (ESC10 版。書き込みは STM32_Programmer_CLI) の代わりに AXISRAM4 (0x34270000) を使うとき、
配列を .npu_weights セクション (Appli の STM32N657X0HXQ_LRUN.ld で AXISRAM4 に置く) に入れ、
デバッガ (CubeIDE の Debug 起動) が elf と一緒に RAM へ載せる。FSBL からの起動 (外部フラッシュのアプリ) では
BOOT_Application が連続した 1 つの像しか複写しないので、この方法は Debug 起動専用 (README)。
配列の先頭が番地と一致するかは network_weights.c の中の _Static_assert では確かめられない (リンク後の番地)
ので、ボードの起動時に npu_rt_init が表示する番地で確かめる。
"""

import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    raw = Path(sys.argv[1])
    out = Path(sys.argv[2])
    addr = int(sys.argv[3], 0)
    data = raw.read_bytes()
    rows = []
    for i in range(0, len(data), 16):
        rows.append("\t" + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]) + ",")
    text = f"""/* 自動生成: scripts/stedgeai/raw2c.py {raw.name} ({len(data)} B) → 0x{addr:08x}。手で編集しない */
#include <stdint.h>

/*
 * NPU 向けにコンパイルしたモデルの重み (stedgeai の {raw.name})。
 * network.c が絶対番地 0x{addr:08x} (メモリプール) で参照するので、.npu_weights セクション
 * (STM32N657X0HXQ_LRUN.ld で AXISRAM4 0x34270000 に置く) の先頭に入れる。
 * const だが、リンカが番地を決めるのは .npu_weights の先頭なので、このファイル以外を同じセクションに入れないこと
 */
const uint8_t network_weights_axisram4[{len(data)}] __attribute__((section(".npu_weights"), aligned(64), used)) = {{
{chr(10).join(rows)}
}};

const uint32_t network_weights_axisram4_size = {len(data)}U;
const uint32_t network_weights_axisram4_addr = 0x{addr:08x}U;
"""
    out.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {out} ({len(data)} B at 0x{addr:08x})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
