/* 直接编译固件SpO2_advanced.c，以真实600点窗口检查MCU浮点实现。 */
#define _CRT_SECURE_NO_WARNINGS
#define SPO2_HOST_TEST
#include "../../mydrivers/SpO2/SpO2_advanced.c"
#include <stdio.h>

int main(int argc, char **argv)
{
    FILE *fp;
    char line[512];
    PPG_Frame frames[PPG_WINDOW];
    SpO2_Result result;
    uint16_t count = 0U;
    int sequence;
    double red_raw, ir_raw, red_fir, ir_fir;
    if (argc != 2 || !(fp = fopen(argv[1], "r"))) return 2;
    (void)fgets(line, sizeof(line), fp);
    while (count < PPG_WINDOW && fgets(line, sizeof(line), fp))
    {
        if (sscanf(line, "%d,%lf,%lf,%lf,%lf", &sequence, &red_raw, &ir_raw,
                   &red_fir, &ir_fir) == 5)
        {
            frames[count].red = (int32_t)red_raw;
            frames[count].ir = (int32_t)ir_raw;
            frames[count].red_fir = (float)red_fir;
            frames[count].ir_fir = (float)ir_fir;
            ++count;
        }
    }
    fclose(fp);
    if (count != PPG_WINDOW) return 3;
    memset(&result, 0, sizeof(result));
    result.seq = 6214U;
    SpO2_Calculate(frames, &result);
    printf("{\n");
    printf("  \"time_pair_count\": %u, \"time_valid\": %u, \"time_hr_bpm\": %.9g, \"time_r\": %.9g, \"time_spo2_percent\": %.9g,\n",
           result.time_pair_count,result.time_valid,result.hr_time,result.ratio_time,result.spo2_time);
    printf("  \"fft_valid\": %u, \"fft_hr_bpm\": %.9g, \"fft_r\": %.9g, \"fft_spo2_percent\": %.9g,\n",
           result.fft_valid,result.hr_fft,result.ratio_fft,result.spo2_fft);
    printf("  \"dst_valid\": %u, \"dst_r\": %.9g, \"dst_spo2_percent\": %.9g, \"dst_prominence\": %.9g\n",
           result.dst_valid,result.ratio_dst,result.spo2_dst,result.dst_prominence);
    printf("}\n");
    return 0;
}
