#define _CRT_SECURE_NO_WARNINGS
#define _USE_MATH_DEFINES
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define N 600
#define FS 75.0
#define MAX_EXTREMA 128
#define MAX_PAIRS 64
#define TEMPLATE_N 100
#define FFT_N 65536
#define FFT_SEARCH_MAX 3000
#define DST_CANDIDATES 61
#define DST_TAPS 12

typedef struct {
    int peaks[MAX_EXTREMA], troughs[MAX_EXTREMA];
    int peak_count, trough_count, lambda_max, lambda_min;
} Detection;

typedef struct {
    int left, trough, right, valid;
    double ac, dc, interval, corr;
    double waveform[TEMPLATE_N];
} Beat;

typedef struct {
    Beat beats[MAX_PAIRS];
    int pair_count, valid_count;
    double bpm;
    Detection detection;
} Quality;

static const double calib_r[71] = {
    1.737383,1.722037,1.709660,1.695925,1.685365,1.671184,1.657999,
    1.644825,1.629170,1.619578,1.602432,1.590460,1.576024,1.564555,
    1.549963,1.536056,1.521000,1.508873,1.494070,1.479322,1.467410,
    1.454216,1.442881,1.427024,1.412967,1.398653,1.384991,1.371156,
    1.359368,1.344576,1.331437,1.316541,1.301992,1.288665,1.272521,
    1.258569,1.245077,1.228699,1.215407,1.203091,1.187513,1.174223,
    1.158901,1.143704,1.127104,1.112217,1.093446,1.078312,1.059389,
    1.042940,1.023294,1.005647,0.985896,0.965254,0.946976,0.922677,
    0.902153,0.879499,0.855860,0.831525,0.807306,0.780265,0.754293,
    0.726932,0.698753,0.669438,0.638004,0.606200,0.572158,0.537932,
    0.503279
};

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median(const double *x, int n) {
    double tmp[FFT_SEARCH_MAX];
    int i;
    if (n <= 0) return NAN;
    for (i = 0; i < n; ++i) tmp[i] = x[i];
    qsort(tmp, (size_t)n, sizeof(double), cmp_double);
    return (n & 1) ? tmp[n / 2] : 0.5 * (tmp[n / 2 - 1] + tmp[n / 2]);
}

static double trimmed_mean(const double *x, int n) {
    double sum = 0.0, lo = x[0], hi = x[0];
    int i;
    for (i = 0; i < n; ++i) {
        sum += x[i]; if (x[i] < lo) lo = x[i]; if (x[i] > hi) hi = x[i];
    }
    return n >= 5 ? (sum - lo - hi) / (n - 2) : sum / n;
}

static double spo2_from_r(double r) {
    int i;
    if (!(r > 0.0) || !isfinite(r)) return NAN;
    if (r <= calib_r[70]) return 100.0;
    if (r >= calib_r[0]) return 30.0;
    for (i = 0; i < 70; ++i) {
        if (r <= calib_r[i] && r >= calib_r[i + 1]) {
            return 30.0 + i + (calib_r[i] - r) / (calib_r[i] - calib_r[i + 1]);
        }
    }
    return NAN;
}

static double r_from_spo2(double spo2) {
    int i;
    double f;
    if (spo2 <= 30.0) return calib_r[0];
    if (spo2 >= 100.0) return calib_r[70];
    i = (int)floor(spo2) - 30;
    f = spo2 - floor(spo2);
    return calib_r[i] + f * (calib_r[i + 1] - calib_r[i]);
}

static int load_csv(const char *path, double *red_raw, double *ir_raw,
                    double *red_fir, double *ir_fir) {
    FILE *fp = fopen(path, "r");
    char line[512]; int count = 0, seq;
    if (!fp) return 0;
    fgets(line, sizeof(line), fp);
    while (count < N && fgets(line, sizeof(line), fp)) {
        if (sscanf(line, "%d,%lf,%lf,%lf,%lf", &seq, &red_raw[count],
                   &ir_raw[count], &red_fir[count], &ir_fir[count]) == 5) ++count;
    }
    fclose(fp); return count;
}

static void refine(const double *x, const int *candidate, int count,
                   int find_max, int *out, int *out_count) {
    int i, j, begin, end, best;
    *out_count = 0;
    for (i = 0; i < count; ++i) {
        begin = candidate[i] - 4; if (begin < 0) begin = 0;
        end = candidate[i] + 5; if (end > N) end = N;
        best = begin;
        for (j = begin + 1; j < end; ++j) {
            if ((find_max && x[j] > x[best]) || (!find_max && x[j] < x[best])) best = j;
        }
        if (*out_count == 0 || out[*out_count - 1] != best) out[(*out_count)++] = best;
    }
}

static Detection msptd(const double *x) {
    enum { DN = 200, MAX_SCALE = 24 };
    double y[DN], det[DN], sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
    unsigned char maxmap[MAX_SCALE][DN] = {{0}}, minmap[MAX_SCALE][DN] = {{0}};
    int s, i, counts_max[MAX_SCALE] = {0}, counts_min[MAX_SCALE] = {0};
    int pc[MAX_EXTREMA], tc[MAX_EXTREMA], pcn = 0, tcn = 0;
    Detection d; memset(&d, 0, sizeof(d));
    for (i = 0; i < DN; ++i) { y[i] = x[i * 3]; sx += i; sy += y[i]; sxx += (double)i*i; sxy += i*y[i]; }
    {
        double slope = (DN*sxy - sx*sy)/(DN*sxx - sx*sx);
        double intercept = (sy - slope*sx)/DN;
        for (i = 0; i < DN; ++i) det[i] = y[i] - (slope*i + intercept);
    }
    for (s = 1; s <= MAX_SCALE; ++s) {
        for (i = s; i < DN - s; ++i) {
            if (det[i] > det[i-s] && det[i] > det[i+s]) { maxmap[s-1][i] = 1; counts_max[s-1]++; }
            if (det[i] < det[i-s] && det[i] < det[i+s]) { minmap[s-1][i] = 1; counts_min[s-1]++; }
        }
    }
    d.lambda_max = d.lambda_min = 1;
    for (s = 1; s < MAX_SCALE; ++s) {
        if (counts_max[s] > counts_max[d.lambda_max-1]) d.lambda_max = s+1;
        if (counts_min[s] > counts_min[d.lambda_min-1]) d.lambda_min = s+1;
    }
    for (i = 0; i < DN; ++i) {
        int ok = 1; for (s=0;s<d.lambda_max;s++) if (!maxmap[s][i]) {ok=0;break;}
        if (ok && pcn < MAX_EXTREMA) pc[pcn++] = (i+1)*3-1;
        ok = 1; for (s=0;s<d.lambda_min;s++) if (!minmap[s][i]) {ok=0;break;}
        if (ok && tcn < MAX_EXTREMA) tc[tcn++] = (i+1)*3-1;
    }
    refine(x, pc, pcn, 1, d.peaks, &d.peak_count);
    refine(x, tc, tcn, 0, d.troughs, &d.trough_count);
    return d;
}

static void resample_normalize(const double *x, int left, int right, double *out) {
    int j, length = right-left+1; double mean=0.0, norm=0.0;
    for (j=0;j<TEMPLATE_N;j++) {
        double pos=(double)j*(length-1)/(TEMPLATE_N-1), frac=pos-floor(pos);
        int a=(int)floor(pos), b=a+1; if (b>=length) b=length-1;
        out[j]=x[left+a]+frac*(x[left+b]-x[left+a]); mean+=out[j];
    }
    mean/=TEMPLATE_N;
    for(j=0;j<TEMPLATE_N;j++){out[j]-=mean;norm+=out[j]*out[j];}
    norm=sqrt(norm); for(j=0;j<TEMPLATE_N;j++) out[j]/=norm;
}

static Quality evaluate(const double *fir, const double *raw) {
    Quality q; int i,j,k; double acs[MAX_PAIRS], intervals[MAX_PAIRS], med, ratio;
    double templ[TEMPLATE_N], column[MAX_PAIRS], norm=0.0, mean=0.0;
    memset(&q,0,sizeof(q)); q.detection=msptd(fir);
    for(i=0;i<q.detection.peak_count-1 && q.pair_count<MAX_PAIRS;i++) {
        int l=q.detection.peaks[i], r=q.detection.peaks[i+1], t=-1;
        double seg[N]; int sn=0;
        for(j=0;j<q.detection.trough_count;j++) {
            int z=q.detection.troughs[j]; if(z>l&&z<r&&(t<0||fir[z]<fir[t])) t=z;
        }
        if(t<0 || fir[l]-fir[t]<0.0) continue;
        q.beats[q.pair_count].left=l; q.beats[q.pair_count].right=r; q.beats[q.pair_count].trough=t;
        q.beats[q.pair_count].ac=fir[l]-fir[t];
        for(j=l;j<=r;j++) seg[sn++]=raw[j];
        q.beats[q.pair_count].dc=fabs(median(seg,sn));
        q.beats[q.pair_count].interval=(double)(r-l);
        acs[q.pair_count]=q.beats[q.pair_count].ac; intervals[q.pair_count]=(r-l)/FS;
        q.pair_count++;
    }
    if(q.pair_count<3) return q;
    med=median(acs,q.pair_count);
    for(i=0;i<q.pair_count;i++) q.beats[i].valid=(acs[i]>=0.4*med&&acs[i]<=2.0*med&&intervals[i]<=3.0);
    while(1) {
        double vals[MAX_PAIRS], minv=1e99,maxv=0, m; int n=0,reject=-1; double far=-1;
        for(i=0;i<q.pair_count;i++)if(q.beats[i].valid){vals[n++]=intervals[i];if(intervals[i]<minv)minv=intervals[i];if(intervals[i]>maxv)maxv=intervals[i];}
        if(n<3||maxv/minv<2.2)break; m=median(vals,n);
        for(i=0;i<q.pair_count;i++)if(q.beats[i].valid){double z=fabs(log(intervals[i]/m));if(z>far){far=z;reject=i;}}
        q.beats[reject].valid=0;
    }
    for(i=0;i<q.pair_count;i++) if(q.beats[i].valid) resample_normalize(fir,q.beats[i].left,q.beats[i].right,q.beats[i].waveform);
    for(j=0;j<TEMPLATE_N;j++) {
        int n=0; for(i=0;i<q.pair_count;i++)if(q.beats[i].valid)column[n++]=q.beats[i].waveform[j];
        templ[j]=median(column,n); mean+=templ[j];
    }
    mean/=TEMPLATE_N; for(j=0;j<TEMPLATE_N;j++){templ[j]-=mean;norm+=templ[j]*templ[j];}
    norm=sqrt(norm); for(j=0;j<TEMPLATE_N;j++)templ[j]/=norm;
    for(i=0;i<q.pair_count;i++)if(q.beats[i].valid){
        q.beats[i].corr=0;for(j=0;j<TEMPLATE_N;j++)q.beats[i].corr+=q.beats[i].waveform[j]*templ[j];
        if(q.beats[i].corr<0.85)q.beats[i].valid=0;
    }
    for(i=0;i<q.pair_count;i++)if(q.beats[i].valid)q.valid_count++;
    if(q.valid_count>=3){double sum=0;for(i=0;i<q.pair_count;i++)if(q.beats[i].valid)sum+=q.beats[i].interval;q.bpm=60.0*FS/(sum/q.valid_count);}
    (void)k; (void)ratio; return q;
}

static double paired_r(const double *red_fir,const double *red_raw,const double *ir_raw,const Quality *ir,int *valid_count) {
    double red_gate[MAX_PAIRS], red_ac[MAX_PAIRS], rv[MAX_PAIRS], med; int i,n=0,rn=0;
    for(i=0;i<ir->pair_count;i++) if(ir->beats[i].valid) red_gate[n++]=red_fir[ir->beats[i].left]-red_fir[ir->beats[i].trough];
    med=median(red_gate,n);
    for(i=0;i<ir->pair_count;i++) {
        const Beat *b=&ir->beats[i]; double rs[N],is[N],rd,id; int j,sn=0;
        red_ac[i]=red_fir[b->left]-red_fir[b->trough];
        if(!b->valid||red_ac[i]<0.4*med||red_ac[i]>2.0*med||b->ac<=0)continue;
        for(j=b->left;j<=b->right;j++){rs[sn]=red_raw[j];is[sn]=ir_raw[j];sn++;}
        rd=fabs(median(rs,sn));id=fabs(median(is,sn)); if(rd<=1e-12||id<=1e-12)continue;
        rv[rn++]=(red_ac[i]/rd)/(b->ac/id);
    }
    *valid_count=rn; return rn>=3?trimmed_mean(rv,rn):NAN;
}

static void detrend_normalized(const double *raw,double *out,int hann) {
    double dc=0,mean=0,slope_num=0,den=0,center=(N-1)/2.0; int i;
    for(i=0;i<N;i++)dc+=raw[i];dc/=N;
    for(i=0;i<N;i++){out[i]=(raw[i]-dc)/fabs(dc);mean+=out[i];}mean/=N;
    for(i=0;i<N;i++){double z=i-center;slope_num+=z*(out[i]-mean);den+=z*z;}
    for(i=0;i<N;i++){out[i]-=mean+(slope_num/den)*(i-center);if(hann)out[i]*=0.5-0.5*cos(2*M_PI*i/(N-1));}
}

static void dft_bin(const double *x,int fft_n,int k,double *re,double *im) {
    double step=-2.0*M_PI*k/fft_n,c=cos(step),s=sin(step),cr=1,ci=0,nr; int i;
    *re=*im=0;for(i=0;i<N;i++){*re+=x[i]*cr;*im+=x[i]*ci;nr=cr*c-ci*s;ci=cr*s+ci*c;cr=nr;}
}

static void fft_method(const double *red_raw,const double *ir_raw,double *hr,double *r_out,double *snr_out,int *peak_out,double *red_amp_out,double *ir_amp_out) {
    double red[N],ir[N],mags[FFT_SEARCH_MAX],re,im,max=-1,noise,offset=0,ra=0,ia=0; int lo,hi,k,peak=0,count=0,half;
    detrend_normalized(red_raw,red,1);detrend_normalized(ir_raw,ir,1);
    lo=(int)ceil(0.5*FFT_N/FS);hi=(int)floor((200.0/60.0)*FFT_N/FS);
    for(k=lo;k<=hi;k++){dft_bin(ir,FFT_N,k,&re,&im);mags[count]=hypot(re,im);if(mags[count]>max){max=mags[count];peak=k;}count++;}
    noise=median(mags,count);*snr_out=max/noise;
    {double lm,cm,rm,den;dft_bin(ir,FFT_N,peak-1,&re,&im);lm=log(hypot(re,im));dft_bin(ir,FFT_N,peak,&re,&im);cm=log(hypot(re,im));dft_bin(ir,FFT_N,peak+1,&re,&im);rm=log(hypot(re,im));den=lm-2*cm+rm;if(fabs(den)>2.22e-16){offset=0.5*(lm-rm)/den;if(offset<-.5)offset=-.5;if(offset>.5)offset=.5;}}
    half=(int)floor((double)FFT_N/N+0.5);
    for(k=peak-half;k<=peak+half;k++){dft_bin(red,FFT_N,k,&re,&im);ra+=re*re+im*im;dft_bin(ir,FFT_N,k,&re,&im);ia+=re*re+im*im;}
    *hr=(peak+offset)*FS/FFT_N*60.0;*red_amp_out=sqrt(ra);*ir_amp_out=sqrt(ia);*r_out=*red_amp_out / *ir_amp_out;*peak_out=peak;
}

static void dst_prepare(const double *raw,double *out) {
    double norm[N],re[65]={0},im[65]={0}; int n,k;
    detrend_normalized(raw,norm,0);
    for(k=3;k<=64;k++)dft_bin(norm,N,k,&re[k],&im[k]);
    for(n=0;n<N;n++){out[n]=0;for(k=3;k<=64;k++){double a=2*M_PI*k*n/N;out[n]+=2.0/N*(re[k]*cos(a)-im[k]*sin(a));}}
}

static void dst_method(const double *red_raw,const double *ir_raw,double *r_out,double *spo2_out,double *prom_out,int *selected_out) {
    double red[N],ir[N],powers[DST_CANDIDATES]={0},w[DST_CANDIDATES][DST_TAPS]={{0}},state[DST_CANDIDATES][DST_TAPS]={{0}};
    double local[DST_CANDIDATES],background,maxp=0;int c,n,t,peaks[DST_CANDIDATES],pn=0,selected=-1;
    dst_prepare(red_raw,red);dst_prepare(ir_raw,ir);
    for(n=0;n<N;n++)for(c=0;c<DST_CANDIDATES;c++){
        double ref=red[n]-r_from_spo2(70.0+0.5*c)*ir[n],est=0,e,norm=1e-8;
        for(t=DST_TAPS-1;t>0;t--)state[c][t]=state[c][t-1];state[c][0]=ref;
        for(t=0;t<DST_TAPS;t++){est+=w[c][t]*state[c][t];norm+=state[c][t]*state[c][t];}
        e=ir[n]-est;for(t=0;t<DST_TAPS;t++)w[c][t]+=0.1*e*state[c][t]/norm;
        if(n>=75)powers[c]+=e*e;
    }
    for(c=0;c<DST_CANDIDATES;c++){powers[c]/=(N-75);local[c]=powers[c];if(powers[c]>maxp)maxp=powers[c];}
    if(powers[0]>powers[1])peaks[pn++]=0;
    for(c=1;c<DST_CANDIDATES-1;c++)if(powers[c]>powers[c-1]&&powers[c]>=powers[c+1])peaks[pn++]=c;
    if(powers[DST_CANDIDATES-1]>powers[DST_CANDIDATES-2])peaks[pn++]=DST_CANDIDATES-1;
    if(!pn){for(c=0;c<DST_CANDIDATES;c++)if(powers[c]==maxp){peaks[pn++]=c;break;}}
    for(c=0;c<pn;c++)if(powers[peaks[c]]>=0.2*maxp)selected=peaks[c];
    background=median(local,DST_CANDIDATES);*selected_out=selected;*r_out=r_from_spo2(70.0+0.5*selected);*spo2_out=spo2_from_r(*r_out);*prom_out=powers[selected]/background;
}

int main(int argc,char **argv) {
    double rr[N],ir[N],rf[N],iff[N],tr,ts,fr,fhr,fs,f_red_amp,f_ir_amp,dr,ds,dp; int rn,dsel,peak;
    Quality rq,iq;
    if(argc!=2||load_csv(argv[1],rr,ir,rf,iff)!=N){fprintf(stderr,"usage: test_algorithms real_window.csv\n");return 2;}
    rq=evaluate(rf,rr);iq=evaluate(iff,ir);tr=paired_r(rf,rr,ir,&iq,&rn);ts=spo2_from_r(tr);
    fft_method(rr,ir,&fhr,&fr,&fs,&peak,&f_red_amp,&f_ir_amp);dst_method(rr,ir,&dr,&ds,&dp,&dsel);
    printf("{\n");
    printf("  \"msptd_ir_peaks\": %d, \"msptd_ir_troughs\": %d, \"lambda_max\": %d, \"lambda_min\": %d,\n",iq.detection.peak_count,iq.detection.trough_count,iq.detection.lambda_max,iq.detection.lambda_min);
    printf("  \"time_pair_count\": %d, \"time_hr_bpm\": %.15g, \"time_r\": %.15g, \"time_spo2_percent\": %.15g,\n",rn,iq.bpm,tr,ts);
    printf("  \"red_quality_valid_count\": %d, \"ir_quality_valid_count\": %d,\n",rq.valid_count,iq.valid_count);
    printf("  \"fft_peak_bin\": %d, \"fft_snr\": %.15g, \"fft_red_amplitude\": %.15g, \"fft_ir_amplitude\": %.15g, \"fft_hr_bpm\": %.15g, \"fft_r\": %.15g, \"fft_spo2_percent\": %.15g,\n",peak,fs,f_red_amp,f_ir_amp,fhr,fr,spo2_from_r(fr));
    printf("  \"dst_selected_index\": %d, \"dst_prominence\": %.15g, \"dst_r\": %.15g, \"dst_spo2_percent\": %.15g\n",dsel,dp,dr,ds);
    printf("}\n");return 0;
}
