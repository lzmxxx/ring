"""比较实际固件算法源文件的float输出与同窗Python参考值。"""
import json
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
EXPECTED = json.loads((HERE / "expected_latest_600.json").read_text(encoding="utf-8"))
CSV = Path(r"G:\5-研究生（生医工）\8-可穿戴\血氧戒指定\嵌入式代码\rt_thread_ipa1322_cw2015_ppg_ble\.mklink\ppg_capture\latest_600.csv")

actual = json.loads(subprocess.check_output(
    [str(HERE / "test_firmware_algorithm.exe"), str(CSV)], text=True
))
tolerances = {
    "time_hr_bpm": 0.2, "time_r": 0.002, "time_spo2_percent": 0.2,
    "fft_hr_bpm": 0.2, "fft_r": 0.002, "fft_spo2_percent": 0.2,
}
passed = True
for name, tolerance in tolerances.items():
    error = abs(actual[name] - EXPECTED[name])
    ok = error <= tolerance
    passed &= ok
    print(f"{'PASS' if ok else 'FAIL'} {name:24s} firmware={actual[name]:.9g} "
          f"python={EXPECTED[name]:.9g} error={error:.3g}")
pair_ok = actual["time_pair_count"] == EXPECTED["time_pair_count"]
passed &= pair_ok
print(f"{'PASS' if pair_ok else 'FAIL'} {'time_pair_count':24s} "
      f"firmware={actual['time_pair_count']} python={EXPECTED['time_pair_count']}")
print(f"RESULT {'PASS' if passed else 'FAIL'}")
raise SystemExit(0 if passed else 1)
