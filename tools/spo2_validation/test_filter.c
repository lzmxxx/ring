#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define FIR_TAPS 151

static const double coefficients[FIR_TAPS] = {
#include "../../mydrivers/SpO2/SpO2_fir_0p3_5Hz.inc"
};

int main(int argc, char **argv)
{
    FILE *file;
    char line[256];
    double average_state[3] = {0.0};
    double fir_state[FIR_TAPS] = {0.0};
    unsigned average_count = 0, average_index = 0;
    unsigned fir_count = 0, fir_index = 0, checked = 0;
    double maximum_relative_error = 0.0;

    if (argc != 2 || fopen_s(&file, argv[1], "r") != 0) return 2;
    (void)fgets(line, sizeof(line), file);
    while (fgets(line, sizeof(line), file)) {
        unsigned index;
        double input, expected, averaged, actual = 0.0;
        char expected_text[80] = {0};
        unsigned tap;
        if (sscanf_s(line, "%u,%lf,%79[^\r\n]", &index, &input,
                     expected_text, (unsigned)sizeof(expected_text)) < 2) return 3;

        average_state[average_index] = input;
        average_index = (average_index + 1U) % 3U;
        if (average_count < 3U) ++average_count;
        if (average_count < 3U) continue;
        averaged = (average_state[0] + average_state[1] + average_state[2]) / 3.0;

        fir_state[fir_index] = averaged;
        fir_index = (fir_index + 1U) % FIR_TAPS;
        if (fir_count < FIR_TAPS) ++fir_count;
        if (fir_count < FIR_TAPS) continue;
        for (tap = 0; tap < FIR_TAPS; ++tap) {
            unsigned state_index = (fir_index + FIR_TAPS - 1U - tap) % FIR_TAPS;
            actual += coefficients[tap] * fir_state[state_index];
        }
        if (!expected_text[0]) return 4;
        expected = strtod(expected_text, NULL);
        {
            double scale = fmax(1.0, fabs(expected));
            double relative_error = fabs(actual - expected) / scale;
            if (relative_error > maximum_relative_error) maximum_relative_error = relative_error;
        }
        ++checked;
    }
    fclose(file);
    printf("checked=%u max_relative_error=%.12g\n", checked, maximum_relative_error);
    return checked == 848U && maximum_relative_error <= 1e-8 ? 0 : 1;
}
