/**
 * @file    SpO2_advanced.c
 * @brief   上位机算法的MCU等效实现：MSPTDfast、逐搏AC/DC和FFT
 * @note    所有工作区固定分配并按阶段复用；算法窗口滑动时不重置连续FIR。
 */
#ifdef SPO2_HOST_TEST
#include <math.h>
#include <stdint.h>
#include <string.h>
#define PPG_WINDOW 600U
#define SPO2_CALIBRATION_CONFIRMED 0U
#define arm_cos_f32 cosf
#define arm_sin_f32 sinf
typedef struct { int32_t red, ir; float red_fir, ir_fir; } PPG_Frame;
typedef struct {
    uint32_t seq; float hr,spo2,pi,ratio,hr_time,hr_fft,correlation;
    float spo2_time,spo2_fft,spo2_dst,ratio_time,ratio_fft,ratio_dst,dst_prominence;
    uint8_t time_pair_count,time_valid,fft_valid,dst_valid,valid,calibrated;
} SpO2_Result;
#else
#include "SpO2.h"
#include "app_config.h"
#include "arm_math.h"
#include <math.h>
#include <string.h>
#endif

#define ALG_FS                 75.0f
#define DS_FACTOR              3U
#define DS_N                   (PPG_WINDOW / DS_FACTOR)
#define MSPTD_MAX_SCALE        24U
#define MAX_EXTREMA            32U
#define MAX_BEATS              32U
#define TEMPLATE_N             100U
/* 目标端用 CMSIS 2048 点实数 FFT：75Hz 下频率间隔约 0.037Hz，
 * 结合三点抛物线插值可满足心率分辨率，同时避免 65536 点逐频 DFT。 */
#ifdef SPO2_HOST_TEST
#define FFT_N                  65536U
#define FFT_MAG_N              2500U
#else
#define FFT_N                  2048U
#define FFT_MAG_N              (FFT_N / 2U + 1U)
#endif
#define TWO_PI                 6.2831853071795864769f

typedef struct
{
    uint16_t left, trough, right;
    uint8_t valid;
    float ac, dc, interval, norm_mean, norm_length, correlation;
} Beat;

typedef struct
{
    uint16_t peaks[MAX_EXTREMA], troughs[MAX_EXTREMA];
    uint8_t peak_count, trough_count, lambda_max, lambda_min;
} Detection;

typedef struct
{
    uint8_t pair_count, valid_count;
    float bpm;
    Detection detection;
} Quality;

#ifdef SPO2_HOST_TEST
typedef struct
{
    float a[PPG_WINDOW];
    float b[PPG_WINDOW];
    union { float magnitudes[FFT_MAG_N]; float c[PPG_WINDOW]; } auxiliary;
} SpectralWorkspace;
#endif

static Beat beat_list[MAX_BEATS];
static float ds_signal[DS_N], ds_detrended[DS_N];
static float template_signal[TEMPLATE_N];
static float scratch[PPG_WINDOW];
#ifdef SPO2_HOST_TEST
static SpectralWorkspace spectral;
#endif
#ifndef SPO2_HOST_TEST
static arm_rfft_fast_instance_f32 fft_instance;
static uint8_t fft_initialized;
/* RFFT 的输入与输出不能原地重叠；三块工作区仅在 FFT 阶段使用。 */
/* RFFT 输入与输出必须分离，但红光和红外不会同时做变换。
 * 两个通道按顺序复用同一组工作区，可稳定回收 16 KiB SRAM。 */
static float fft_work[FFT_N], fft_output[FFT_N];
#endif

static const uint32_t calibration_r[71] = {
1737383,1722037,1709660,1695925,1685365,1671184,1657999,1644825,1629170,1619578,
1602432,1590460,1576024,1564555,1549963,1536056,1521000,1508873,1494070,1479322,
1467410,1454216,1442881,1427024,1412967,1398653,1384991,1371156,1359368,1344576,
1331437,1316541,1301992,1288665,1272521,1258569,1245077,1228699,1215407,1203091,
1187513,1174223,1158901,1143704,1127104,1112217,1093446,1078312,1059389,1042940,
1023294,1005647,985896,965254,946976,922677,902153,879499,855860,831525,
807306,780265,754293,726932,698753,669438,638004,606200,572158,537932,503279};

static float abs_f(float x) { return x < 0.0f ? -x : x; }
static float select_value(float *v,uint16_t n,uint16_t target);

static float fir_at(const PPG_Frame *f, uint16_t index, uint8_t red)
{
    return red ? f[index].red_fir : f[index].ir_fir;
}

static void sort_values(float *v, uint16_t n)
{
    uint16_t i;
    for (i = 1U; i < n; ++i)
    {
        float x = v[i];
        uint16_t j = i;
        while (j && v[j - 1U] > x) { v[j] = v[j - 1U]; --j; }
        v[j] = x;
    }
}

static float median_values(const float *v, uint16_t n)
{
    memcpy(scratch, v, n * sizeof(float));
    sort_values(scratch, n);
    return (n & 1U) ? scratch[n / 2U] : 0.5f * (scratch[n / 2U - 1U] + scratch[n / 2U]);
}

static float median_raw(const PPG_Frame *f, uint16_t left, uint16_t right, uint8_t red)
{
    uint16_t i, n = 0U;
    for (i = left; i <= right; ++i) scratch[n++] = red ? (float)f[i].red : (float)f[i].ir;
    /* 逐搏区间通常有数十至数百采样点。只取中值时无需完整插入排序：
       Quickselect 保持完全相同的中值定义，平均复杂度由 O(n²) 降为 O(n)。 */
    return abs_f((n & 1U) ? select_value(scratch,n,n/2U) :
                 0.5f * (select_value(scratch,n,n/2U-1U)+select_value(scratch,n,n/2U)));
}

static float trimmed_mean(float *v, uint8_t n)
{
    uint8_t i, first = 0U, last = n;
    float sum = 0.0f;
    sort_values(v, n);
    if (n >= 5U) { first = 1U; last = n - 1U; }
    for (i = first; i < last; ++i) sum += v[i];
    return sum / (float)(last - first);
}

static float spo2_from_r(float ratio)
{
    uint32_t r;
    uint8_t i;
    if (!(ratio > 0.0f)) return 0.0f;
    r = (uint32_t)(ratio * 1000000.0f + 0.5f);
    if (r >= calibration_r[0]) return 30.0f;
    if (r <= calibration_r[70]) return 100.0f;
    for (i = 0U; i < 70U; ++i)
        if (r <= calibration_r[i] && r >= calibration_r[i + 1U])
            return 30.0f + (float)i + (float)(calibration_r[i] - r) /
                   (float)(calibration_r[i] - calibration_r[i + 1U]);
    return 0.0f;
}

static void refine(const PPG_Frame *f, const uint16_t *candidate, uint8_t count,
                   uint8_t maximum, uint8_t red, uint16_t *out, uint8_t *out_count)
{
    uint8_t i;
    *out_count = 0U;
    for (i = 0U; i < count; ++i)
    {
        uint16_t begin = candidate[i] > 4U ? candidate[i] - 4U : 0U;
        uint16_t end = candidate[i] + 5U < PPG_WINDOW ? candidate[i] + 5U : PPG_WINDOW;
        uint16_t best = begin, j;
        for (j = begin + 1U; j < end; ++j)
            if ((maximum && fir_at(f,j,red) > fir_at(f,best,red)) ||
                (!maximum && fir_at(f,j,red) < fir_at(f,best,red))) best = j;
        if (!*out_count || out[*out_count - 1U] != best) out[(*out_count)++] = best;
    }
}

static Detection detect_msptd(const PPG_Frame *f, uint8_t red)
{
    Detection d;
    uint16_t max_candidate[MAX_EXTREMA], min_candidate[MAX_EXTREMA];
    uint8_t max_n = 0U, min_n = 0U, scale;
    uint16_t i, best_max = 0U, best_min = 0U;
    float sx = 0.0f, sy = 0.0f, sxx = 0.0f, sxy = 0.0f, slope, intercept;
    memset(&d, 0, sizeof(d));
    for (i = 0U; i < DS_N; ++i)
    {
        ds_signal[i] = fir_at(f, i * DS_FACTOR, red);
        sx += i; sy += ds_signal[i]; sxx += (float)i * i; sxy += (float)i * ds_signal[i];
    }
    slope = ((float)DS_N * sxy - sx * sy) / ((float)DS_N * sxx - sx * sx);
    intercept = (sy - slope * sx) / (float)DS_N;
    for (i = 0U; i < DS_N; ++i) ds_detrended[i] = ds_signal[i] - (slope * i + intercept);
    d.lambda_max = d.lambda_min = 1U;
    for (scale = 1U; scale <= MSPTD_MAX_SCALE; ++scale)
    {
        uint16_t maxima = 0U, minima = 0U;
        for (i = scale; i + scale < DS_N; ++i)
        {
            if (ds_detrended[i] > ds_detrended[i-scale] && ds_detrended[i] > ds_detrended[i+scale]) ++maxima;
            if (ds_detrended[i] < ds_detrended[i-scale] && ds_detrended[i] < ds_detrended[i+scale]) ++minima;
        }
        if (maxima > best_max) { best_max = maxima; d.lambda_max = scale; }
        if (minima > best_min) { best_min = minima; d.lambda_min = scale; }
    }
    for (i = 0U; i < DS_N; ++i)
    {
        uint8_t all_max = 1U, all_min = 1U;
        for (scale = 1U; scale <= d.lambda_max; ++scale)
            if (i < scale || i + scale >= DS_N || !(ds_detrended[i] > ds_detrended[i-scale] && ds_detrended[i] > ds_detrended[i+scale])) { all_max = 0U; break; }
        for (scale = 1U; scale <= d.lambda_min; ++scale)
            if (i < scale || i + scale >= DS_N || !(ds_detrended[i] < ds_detrended[i-scale] && ds_detrended[i] < ds_detrended[i+scale])) { all_min = 0U; break; }
        if (all_max && max_n < MAX_EXTREMA) max_candidate[max_n++] = (i + 1U) * DS_FACTOR - 1U;
        if (all_min && min_n < MAX_EXTREMA) min_candidate[min_n++] = (i + 1U) * DS_FACTOR - 1U;
    }
    refine(f,max_candidate,max_n,1U,red,d.peaks,&d.peak_count);
    refine(f,min_candidate,min_n,0U,red,d.troughs,&d.trough_count);
    return d;
}

static float resampled_value(const PPG_Frame *f, const Beat *beat, uint16_t j, uint8_t red)
{
    float position = (float)j * (beat->right - beat->left) / (float)(TEMPLATE_N - 1U);
    uint16_t lower = (uint16_t)position, upper = lower + 1U, length = beat->right - beat->left;
    float fraction = position - lower;
    if (upper > length) upper = length;
    return fir_at(f,beat->left+lower,red) + fraction *
          (fir_at(f,beat->left+upper,red) - fir_at(f,beat->left+lower,red));
}

static void prepare_beat(const PPG_Frame *f, Beat *beat, uint8_t red)
{
    uint16_t j;
    float mean = 0.0f, norm = 0.0f;
    for (j = 0U; j < TEMPLATE_N; ++j) mean += resampled_value(f,beat,j,red);
    mean /= TEMPLATE_N;
    for (j = 0U; j < TEMPLATE_N; ++j) { float x = resampled_value(f,beat,j,red)-mean; norm += x*x; }
    beat->norm_mean = mean; beat->norm_length = sqrtf(norm);
}

static float normalized_beat(const PPG_Frame *f, const Beat *beat, uint16_t j, uint8_t red)
{
    return (resampled_value(f,beat,j,red) - beat->norm_mean) / beat->norm_length;
}

static Quality evaluate_quality(const PPG_Frame *f, uint8_t red)
{
    Quality q;
    uint8_t i;
    memset(&q,0,sizeof(q)); memset(beat_list,0,sizeof(beat_list));
    q.detection = detect_msptd(f,red);
    for (i=0U; i+1U<q.detection.peak_count && q.pair_count<MAX_BEATS; ++i)
    {
        uint16_t left=q.detection.peaks[i], right=q.detection.peaks[i+1U], trough=0U;
        uint8_t j,found=0U;
        for(j=0U;j<q.detection.trough_count;++j){uint16_t t=q.detection.troughs[j];if(t>left&&t<right&&(!found||fir_at(f,t,red)<fir_at(f,trough,red))){trough=t;found=1U;}}
        if(!found)continue;
        beat_list[q.pair_count].left=left;beat_list[q.pair_count].trough=trough;beat_list[q.pair_count].right=right;
        beat_list[q.pair_count].ac=fir_at(f,left,red)-fir_at(f,trough,red);
        if(beat_list[q.pair_count].ac<0.0f)continue;
        beat_list[q.pair_count].dc=median_raw(f,left,right,red);
        beat_list[q.pair_count].interval=(float)(right-left);++q.pair_count;
    }
    if(q.pair_count<3U)return q;
    /* 可穿戴端的接触压力会使搏动幅度变化较大；保留明显异常搏动，
       但不再因幅度变化过早丢弃正常心搏。 */
    {float med;for(i=0U;i<q.pair_count;++i)scratch[i]=beat_list[i].ac;med=median_values(scratch,q.pair_count);for(i=0U;i<q.pair_count;++i)beat_list[i].valid=(beat_list[i].ac>=0.25f*med&&beat_list[i].ac<=3.5f*med&&beat_list[i].interval/ALG_FS<=3.0f);}
    for(;;)
    {
        uint8_t n=0U,reject=0U;float lo=1.0e30f,hi=0.0f,med,far=-1.0f;
        for(i=0U;i<q.pair_count;++i)if(beat_list[i].valid){float x=beat_list[i].interval/ALG_FS;scratch[n++]=x;if(x<lo)lo=x;if(x>hi)hi=x;}
        if(n<3U||hi/lo<2.2f)break;med=median_values(scratch,n);
        for(i=0U;i<q.pair_count;++i)if(beat_list[i].valid){float d=abs_f(logf((beat_list[i].interval/ALG_FS)/med));if(d>far){far=d;reject=i;}}
        beat_list[reject].valid=0U;
    }
    for(i=0U;i<q.pair_count;++i)if(beat_list[i].valid){prepare_beat(f,&beat_list[i],red);if(beat_list[i].norm_length<=1.0e-12f)beat_list[i].valid=0U;}
    {
        uint16_t j;float mean=0.0f,norm=0.0f;
        for(j=0U;j<TEMPLATE_N;++j){uint8_t n=0U;for(i=0U;i<q.pair_count;++i)if(beat_list[i].valid)scratch[n++]=normalized_beat(f,&beat_list[i],j,red);if(!n)return q;template_signal[j]=median_values(scratch,n);mean+=template_signal[j];}
        mean/=TEMPLATE_N;for(j=0U;j<TEMPLATE_N;++j){template_signal[j]-=mean;norm+=template_signal[j]*template_signal[j];}norm=sqrtf(norm);if(norm<=1.0e-12f)return q;for(j=0U;j<TEMPLATE_N;++j)template_signal[j]/=norm;
        for(i=0U;i<q.pair_count;++i)if(beat_list[i].valid){beat_list[i].correlation=0.0f;for(j=0U;j<TEMPLATE_N;++j)beat_list[i].correlation+=normalized_beat(f,&beat_list[i],j,red)*template_signal[j];if(beat_list[i].correlation<0.65f)beat_list[i].valid=0U;}
    }
    {float sum=0.0f;for(i=0U;i<q.pair_count;++i)if(beat_list[i].valid){++q.valid_count;sum+=beat_list[i].interval;}if(q.valid_count>=2U)q.bpm=60.0f*ALG_FS/(sum/q.valid_count);}
    return q;
}

static float paired_time_r(const PPG_Frame *f, uint8_t pair_count, uint8_t *valid_count)
{
    float red_ac[MAX_BEATS], gate[MAX_BEATS], ratios[MAX_BEATS], med;
    uint8_t i,n=0U,rn=0U;
    for(i=0U;i<pair_count;++i){red_ac[i]=f[beat_list[i].left].red_fir-f[beat_list[i].trough].red_fir;if(beat_list[i].valid&&red_ac[i]>=0.0f)gate[n++]=red_ac[i];}
    if(n<2U){*valid_count=0U;return 0.0f;}med=median_values(gate,n);
    for(i=0U;i<pair_count;++i)if(beat_list[i].valid&&red_ac[i]>=0.25f*med&&red_ac[i]<=3.5f*med&&beat_list[i].ac>0.0f){float rd=median_raw(f,beat_list[i].left,beat_list[i].right,1U),id=median_raw(f,beat_list[i].left,beat_list[i].right,0U);if(rd>1.0e-12f&&id>1.0e-12f)ratios[rn++]=(red_ac[i]/rd)/(beat_list[i].ac/id);}
    *valid_count=rn;return rn>=2U?trimmed_mean(ratios,rn):0.0f;
}

static void normalize_raw(const PPG_Frame *f, uint8_t red, float *out, uint8_t hann)
{
    uint16_t i;
    float dc=0.0f,mean=0.0f,numerator=0.0f,denominator=0.0f,center=(PPG_WINDOW-1U)*0.5f;
    for(i=0U;i<PPG_WINDOW;++i)dc+=red?(float)f[i].red:(float)f[i].ir;dc/=PPG_WINDOW;
    if(abs_f(dc)<=1.0e-12f){memset(out,0,PPG_WINDOW*sizeof(float));return;}
    for(i=0U;i<PPG_WINDOW;++i){float raw=red?(float)f[i].red:(float)f[i].ir;out[i]=(raw-dc)/abs_f(dc);mean+=out[i];}mean/=PPG_WINDOW;
    for(i=0U;i<PPG_WINDOW;++i){float x=(float)i-center;numerator+=x*(out[i]-mean);denominator+=x*x;}
    for(i=0U;i<PPG_WINDOW;++i){out[i]-=mean+numerator/denominator*((float)i-center);if(hann)out[i]*=0.5f-0.5f*arm_cos_f32(TWO_PI*i/(PPG_WINDOW-1U));}
}

#ifdef SPO2_HOST_TEST
static void dft_bin(const float *x,uint32_t transform_n,uint32_t bin,float *re,float *im)
{
    float step=-TWO_PI*(float)bin/(float)transform_n,c=arm_cos_f32(step),s=arm_sin_f32(step),cr=1.0f,ci=0.0f;
    uint16_t i;*re=0.0f;*im=0.0f;
    for(i=0U;i<PPG_WINDOW;++i){float next;*re+=x[i]*cr;*im+=x[i]*ci;next=cr*c-ci*s;ci=cr*s+ci*c;cr=next;}
}
#endif

static float select_value(float *v,uint16_t n,uint16_t target)
{
    uint16_t left=0U,right=n-1U;
    for(;;)
    {
        uint16_t i=left,j=right;float pivot=v[left+(right-left)/2U];
        while(i<=j)
        {
            float t;while(v[i]<pivot)++i;while(v[j]>pivot){if(!j)break;--j;}
            if(i<=j){t=v[i];v[i]=v[j];v[j]=t;++i;if(j)--j;else break;}
        }
        if(target<=j)right=j;else if(target>=i)left=i;else return v[target];
    }
}

static float median_large(float *v,uint16_t n)
{
    float upper=select_value(v,n,n/2U);
    return (n&1U)?upper:0.5f*(upper+select_value(v,n,n/2U-1U));
}

static uint8_t fft_spo2(const PPG_Frame *f,float *hr,float *ratio,float *spo2)
{
#ifdef SPO2_HOST_TEST
    uint32_t first=(uint32_t)ceilf(0.5f*FFT_N/ALG_FS),last=(uint32_t)floorf((200.0f/60.0f)*FFT_N/ALG_FS),bin,peak;
    uint16_t n=0U,half=(uint16_t)((float)FFT_N/PPG_WINDOW+0.5f);float re,im,maximum=-1.0f,noise,offset=0.0f,red_energy=0.0f,ir_energy=0.0f;
    normalize_raw(f,1U,spectral.a,1U);normalize_raw(f,0U,spectral.b,1U);peak=first;
    for(bin=first;bin<=last;++bin){float m;dft_bin(spectral.b,FFT_N,bin,&re,&im);m=sqrtf(re*re+im*im);spectral.auxiliary.magnitudes[n++]=m;if(m>maximum){maximum=m;peak=bin;}}
    noise=median_large(spectral.auxiliary.magnitudes,n);if(noise<=0.0f||maximum/noise<1.5f)return 0U;
    {float l,c,r,d;dft_bin(spectral.b,FFT_N,peak-1U,&re,&im);l=logf(sqrtf(re*re+im*im));dft_bin(spectral.b,FFT_N,peak,&re,&im);c=logf(sqrtf(re*re+im*im));dft_bin(spectral.b,FFT_N,peak+1U,&re,&im);r=logf(sqrtf(re*re+im*im));d=l-2.0f*c+r;if(abs_f(d)>1.0e-12f){offset=0.5f*(l-r)/d;if(offset<-.5f)offset=-.5f;if(offset>.5f)offset=.5f;}}
    for(bin=peak-half;bin<=peak+half;++bin){dft_bin(spectral.a,FFT_N,bin,&re,&im);red_energy+=re*re+im*im;dft_bin(spectral.b,FFT_N,bin,&re,&im);ir_energy+=re*re+im*im;}
    if(red_energy<=0.0f||ir_energy<=0.0f)return 0U;*hr=((float)peak+offset)*ALG_FS/FFT_N*60.0f;*ratio=sqrtf(red_energy/ir_energy);*spo2=spo2_from_r(*ratio);return *spo2>0.0f;
#else
    uint32_t first=(uint32_t)ceilf(0.5f*FFT_N/ALG_FS),last=(uint32_t)floorf((200.0f/60.0f)*FFT_N/ALG_FS),bin,peak;
    uint16_t n=0U,half=(uint16_t)((float)FFT_N/PPG_WINDOW+0.5f);
    float maximum=-1.0f,noise,offset=0.0f,red_energy=0.0f,ir_energy=0.0f;

    memset(fft_work,0,sizeof(fft_work));
    normalize_raw(f,1U,fft_work,1U);
    if (!fft_initialized)
    {
        if (arm_rfft_fast_init_f32(&fft_instance, FFT_N) != ARM_MATH_SUCCESS) return 0U;
        fft_initialized = 1U;
    }
    arm_rfft_fast_f32(&fft_instance,fft_work,fft_output,0U);
    /* 红光只需要主频附近少量频点。先暂存完整输出，待红外确定主频后
     * 无法再访问，因此红外先计算，红光随后重算并立即积分。 */
    memset(fft_work,0,sizeof(fft_work));
    normalize_raw(f,0U,fft_work,1U);
    arm_rfft_fast_f32(&fft_instance,fft_work,fft_output,0U);
    peak=first;
    for(bin=first;bin<=last;++bin)
    {
        float re=fft_output[2U*bin],im=fft_output[2U*bin+1U],m=sqrtf(re*re+im*im);
        /* scratch 已在时域路径结束后释放，复用它保存频带噪声估计值。 */
        scratch[n++]=m;
        if(m>maximum){maximum=m;peak=bin;}
    }
    noise=median_large(scratch,n);
    if(noise<=0.0f||maximum/noise<1.5f)return 0U;
    {
        float l,c,r,d;
        l=logf(sqrtf(fft_output[2U*(peak-1U)]*fft_output[2U*(peak-1U)]+fft_output[2U*(peak-1U)+1U]*fft_output[2U*(peak-1U)+1U]));
        c=logf(maximum);
        r=logf(sqrtf(fft_output[2U*(peak+1U)]*fft_output[2U*(peak+1U)]+fft_output[2U*(peak+1U)+1U]*fft_output[2U*(peak+1U)+1U]));
        d=l-2.0f*c+r;
        if(abs_f(d)>1.0e-12f){offset=0.5f*(l-r)/d;if(offset<-.5f)offset=-.5f;if(offset>.5f)offset=.5f;}
    }
    for(bin=peak-half;bin<=peak+half;++bin)
    {
        float re=fft_output[2U*bin],im=fft_output[2U*bin+1U];
        ir_energy+=re*re+im*im;
    }
    memset(fft_work,0,sizeof(fft_work));
    normalize_raw(f,1U,fft_work,1U);
    arm_rfft_fast_f32(&fft_instance,fft_work,fft_output,0U);
    for(bin=peak-half;bin<=peak+half;++bin)
    {
        float re=fft_output[2U*bin],im=fft_output[2U*bin+1U];
        red_energy+=re*re+im*im;
    }
    if(red_energy<=0.0f||ir_energy<=0.0f)return 0U;
    *hr=((float)peak+offset)*ALG_FS/FFT_N*60.0f;
    *ratio=sqrtf(red_energy/ir_energy);
    *spo2=spo2_from_r(*ratio);
    return *spo2>0.0f;
#endif
}

static void legacy_metrics(const PPG_Frame *f,SpO2_Result *r)
{
    uint16_t i;float ir_mean=0.0f,re=0.0f,ie=0.0f,cross=0.0f;
    for(i=0U;i<PPG_WINDOW;++i){ir_mean+=(float)f[i].ir;re+=f[i].red_fir*f[i].red_fir;ie+=f[i].ir_fir*f[i].ir_fir;cross+=f[i].red_fir*f[i].ir_fir;}ir_mean/=PPG_WINDOW;
    if(re>0.0f&&ie>0.0f)r->correlation=cross/sqrtf(re*ie);if(abs_f(ir_mean)>1.0f&&ie>0.0f)r->pi=100.0f*sqrtf(ie/PPG_WINDOW)/abs_f(ir_mean);
}

void SpO2_Calculate(const PPG_Frame *frames,SpO2_Result *result)
{
    uint32_t seq;Quality red,ir;uint8_t paired=0U;
    if(!frames||!result)return;seq=result->seq;memset(result,0,sizeof(*result));result->seq=seq;
    red=evaluate_quality(frames,1U);ir=evaluate_quality(frames,0U);
    result->hr_time=ir.bpm;result->ratio_time=paired_time_r(frames,ir.pair_count,&paired);result->time_pair_count=paired;result->spo2_time=spo2_from_r(result->ratio_time);
    /* 8 秒窗口在低心率时通常仅含 4~5 个完整搏动。两个一致搏动已足以
       作为连续上报的质量提示；最终医学有效性仍由上位机结合原始波形判断。 */
    result->time_valid=(red.valid_count>=2U&&ir.valid_count>=2U&&paired>=2U&&ir.bpm>=30.0f&&ir.bpm<=200.0f&&result->spo2_time>0.0f);
    result->fft_valid=fft_spo2(frames,&result->hr_fft,&result->ratio_fft,&result->spo2_fft);
    /* DST 已移除以降低工作态 CPU 占用；协议字段保留为 0，避免破坏上位机帧结构。 */
    result->dst_valid=0U;
    legacy_metrics(frames,result);result->hr=result->hr_time;result->ratio=result->ratio_time;result->spo2=result->spo2_time;result->valid=1U;result->calibrated=SPO2_CALIBRATION_CONFIRMED;
}
