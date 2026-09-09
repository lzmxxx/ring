# SpO2 C 等效验证

本目录使用 `rt_thread_ipa1322_cw2015_ppg_ble/tool/ppg_validator` 作为唯一参考实现。
真实窗口先由 `generate_reference.py` 计算参考值；随后同一 CSV 输入编译后的 C 程序，逐项比较时域心率、时域 R/SpO2、FFT 心率/R/SpO2 和 DST R/SpO2。

允许误差：心率 0.2 BPM，R 0.002，SpO2 0.2%。连续移动平均和 151 taps FIR 另以逐点最大误差 `1e-3 * max(1, |reference|)` 校验。

## 2026-09-08 同窗验证结果

- 输入：`latest_600.csv`，75 Hz，600点（8秒）。
- 独立C实现与上位机Python实现共20项对照全部通过。
- MSPTDfast：8峰、7谷，lambda_max/lambda_min均为9；660/905 nm各保留5个有效搏动。
- 时域：79.225352 BPM，R=0.6919880791，SpO2=94.2307665%。
- FFT：29.972076 BPM，R=0.8147131843，SpO2=89.6941581%，峰值SNR=12.385512。
- DST：候选索引44，R=0.754293，SpO2=92%，峰突出度=6.604757。
- 连续3点平滑+151 taps FIR：848个有效输出点，最大相对误差2.31886275713e-9。

`verify_algorithms.py`会直接运行编译后的C程序，再从上位机包实时计算Python参考值并逐项比较，避免使用手写结果作为判定依据。

`test_firmware_algorithm.c`直接包含工程实际使用的`SpO2_advanced.c`，
`verify_firmware_algorithm.py`用于确认最终固件float实现仍满足同一组误差门限。
