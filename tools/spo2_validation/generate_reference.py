"""用上位机算法为真实600点PPG窗口生成C移植基准。"""

import argparse
import json
import sys
from pathlib import Path

import numpy as np


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--gui-root", type=Path, required=True)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    sys.path.insert(0, str(args.gui_root / "tool"))
    from ppg_validator.algorithms import (  # pylint: disable=import-error
        analyze_dual_wavelength,
        calculate_dst_spo2,
        calculate_fft_spo2,
    )
    from ppg_validator.config import AppConfig  # pylint: disable=import-error

    data = np.genfromtxt(args.input, delimiter=",", names=True)
    config = AppConfig()
    time_result = analyze_dual_wavelength(
        data["red_fir"], data["ir_fir"], config.sample_rate_hz, config,
        red_dc_samples=data["red_raw"], infrared_dc_samples=data["ir_raw"],
    )
    fft_result = calculate_fft_spo2(
        data["red_raw"], data["ir_raw"], config.sample_rate_hz,
        hr_min_bpm=config.hr_min_bpm, hr_max_bpm=config.hr_max_bpm,
        integration_half_bins=config.fft_integration_half_bins,
        minimum_peak_snr=config.fft_minimum_peak_snr,
        zero_padding_factor=config.fft_zero_padding_factor,
    )
    dst_result = calculate_dst_spo2(
        data["red_raw"], data["ir_raw"], config.sample_rate_hz,
        spo2_min_percent=config.dst_spo2_min_percent,
        spo2_max_percent=config.dst_spo2_max_percent,
        spo2_step_percent=config.dst_spo2_step_percent,
        nlms_taps=config.dst_nlms_taps,
        nlms_step_size=config.dst_nlms_step_size,
        nlms_epsilon=config.dst_nlms_epsilon,
        minimum_peak_prominence_ratio=config.dst_minimum_peak_prominence_ratio,
    )
    expected = {
        "sample_rate_hz": config.sample_rate_hz,
        "sample_count": int(data.size),
        "time_hr_bpm": time_result["heart_rate_result"]["bpm"],
        "time_r": time_result["r_value"],
        "time_spo2_percent": time_result["spo2_percent"],
        "time_pair_count": time_result["paired_r_count"],
        "fft_hr_bpm": fft_result.heart_rate_bpm,
        "fft_r": fft_result.r_value,
        "fft_spo2_percent": fft_result.spo2_percent,
        "dst_r": dst_result.r_value,
        "dst_spo2_percent": dst_result.spo2_percent,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(expected, indent=2), encoding="utf-8")
    print(json.dumps(expected, indent=2))


if __name__ == "__main__":
    main()
