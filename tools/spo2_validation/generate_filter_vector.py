"""生成连续平滑/FIR的逐点C对照向量。"""

import csv
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3] / "rt_thread_ipa1322_cw2015_ppg_ble"
sys.path.insert(0, str(ROOT / "tool"))

from ppg_validator.config import AppConfig, CHANNELS  # noqa: E402
from ppg_validator.filters import StreamingFilterBank  # noqa: E402


def main():
    bank = StreamingFilterBank(AppConfig())
    output = Path(__file__).with_name("filter_vector.csv")
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(("index", "input", "output"))
        for index in range(1000):
            value = 100000.0 + 1200.0 * math.sin(2.0 * math.pi * 1.27 * index / 75.0)
            value += 250.0 * math.sin(2.0 * math.pi * 7.3 * index / 75.0)
            filtered = bank.process(CHANNELS[3], value)
            writer.writerow((index, f"{value:.12g}", "" if filtered is None else f"{filtered:.12g}"))
    print(output)


if __name__ == "__main__":
    main()
