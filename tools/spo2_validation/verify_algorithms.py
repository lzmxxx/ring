"""在同一真实窗口上比较上位机 Python 与独立编译的 C 算法。"""

import json
import subprocess
import sys
from pathlib import Path

import numpy as np


HERE = Path(__file__).resolve().parent
GUI_ROOT = Path(r"G:\5-研究生（生医工）\8-可穿戴\血氧戒指定\嵌入式代码\rt_thread_ipa1322_cw2015_ppg_ble")
CSV = GUI_ROOT / ".mklink" / "ppg_capture" / "latest_600.csv"
sys.path.insert(0, str(GUI_ROOT / "tool"))

from ppg_validator.algorithms import (  # noqa: E402
    analyze_dual_wavelength,
    calculate_dst_spo2,
    calculate_fft_spo2,
)
from ppg_validator.config import AppConfig  # noqa: E402


def close(name, actual, expected, tolerance):
    error = abs(float(actual) - float(expected))
    passed = error <= tolerance
    print(f"{'PASS' if passed else 'FAIL'} {name:25s} C={actual:.15g} Python={expected:.15g} error={error:.3g} tol={tolerance:g}")
    return passed


def exact(name, actual, expected):
    passed = int(actual) == int(expected)
    print(f"{'PASS' if passed else 'FAIL'} {name:25s} C={int(actual)} Python={int(expected)}")
    return passed


def main():
    data = np.genfromtxt(CSV, delimiter=",", names=True)
    cfg = AppConfig()
    time = analyze_dual_wavelength(
        data["red_fir"], data["ir_fir"], cfg.sample_rate_hz, cfg,
        red_dc_samples=data["red_raw"], infrared_dc_samples=data["ir_raw"],
    )
    fft = calculate_fft_spo2(
        data["red_raw"], data["ir_raw"], cfg.sample_rate_hz,
        cfg.hr_min_bpm, cfg.hr_max_bpm, cfg.fft_integration_half_bins,
        cfg.fft_minimum_peak_snr, cfg.fft_zero_padding_factor,
    )
    dst = calculate_dst_spo2(
        data["red_raw"], data["ir_raw"], cfg.sample_rate_hz,
        cfg.dst_spo2_min_percent, cfg.dst_spo2_max_percent,
        cfg.dst_spo2_step_percent, cfg.dst_nlms_taps,
        cfg.dst_nlms_step_size, cfg.dst_nlms_epsilon,
        cfg.dst_minimum_peak_prominence_ratio,
    )
    raw = subprocess.check_output([str(HERE / "test_algorithms.exe"), str(CSV)], text=True)
    c = json.loads(raw)
    ir_quality = time["heart_rate_result"]
    red_quality = time["channel_results"]["660 nm"]
    checks = [
        exact("MSPTD peaks", c["msptd_ir_peaks"], ir_quality["detected_peak_count"]),
        exact("MSPTD troughs", c["msptd_ir_troughs"], ir_quality["detected_trough_count"]),
        exact("MSPTD lambda max", c["lambda_max"], ir_quality["lambda_max"]),
        exact("MSPTD lambda min", c["lambda_min"], ir_quality["lambda_min"]),
        exact("red SQI valid beats", c["red_quality_valid_count"], red_quality["valid_pair_count"]),
        exact("IR SQI valid beats", c["ir_quality_valid_count"], ir_quality["valid_pair_count"]),
        exact("paired R beats", c["time_pair_count"], time["paired_r_count"]),
        close("time HR bpm", c["time_hr_bpm"], ir_quality["bpm"], 0.2),
        close("time R", c["time_r"], time["r_value"], 0.002),
        close("time SpO2 percent", c["time_spo2_percent"], time["spo2_percent"], 0.2),
        close("FFT SNR", c["fft_snr"], fft.infrared_peak_snr, 1e-9),
        close("FFT red amplitude", c["fft_red_amplitude"], fft.red_amplitude, 1e-9),
        close("FFT IR amplitude", c["fft_ir_amplitude"], fft.infrared_amplitude, 1e-9),
        close("FFT HR bpm", c["fft_hr_bpm"], fft.heart_rate_bpm, 0.2),
        close("FFT R", c["fft_r"], fft.r_value, 0.002),
        close("FFT SpO2 percent", c["fft_spo2_percent"], fft.spo2_percent, 0.2),
        exact("DST selected index", c["dst_selected_index"], int(round((dst.spo2_percent - 70.0) / 0.5))),
        close("DST prominence", c["dst_prominence"], dst.peak_prominence_ratio, 1e-8),
        close("DST R", c["dst_r"], dst.r_value, 0.002),
        close("DST SpO2 percent", c["dst_spo2_percent"], dst.spo2_percent, 0.2),
    ]
    print(f"RESULT {'PASS' if all(checks) else 'FAIL'}: {sum(checks)}/{len(checks)} checks passed")
    return 0 if all(checks) else 1


if __name__ == "__main__":
    raise SystemExit(main())
