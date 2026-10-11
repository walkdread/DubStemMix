// Loudness of a whole recording (ITU-R BS.1770-4 / EBU R128), for the MP3 export (PRD § 5.9): integrated
// loudness in LUFS (K-weighting, 400 ms blocks every 100 ms, absolute gate −70 LUFS, relative gate −10 LU)
// and true peak (4× oversampled). Offline, not for the audio thread: it allocates as the recording grows.

#include "dub_internal.h"

#define LOUDNESS_PHASES 4
#define LOUDNESS_TAPS 12   // per phase

typedef struct {
    double b0, b1, b2, a1, a2;
    double z1[2], z2[2];   // per channel, transposed direct form II
} Biquad;

struct DubLoudness {
    Biquad shelf, highPass;
    int segmentLength;     // 100 ms, in samples
    int segmentFill;
    double segmentSum;     // K-weighted energy of the segment in progress, both channels
    double *segments;      // energy of each finished 100 ms segment
    long segmentCount, segmentCapacity;
    float history[2][LOUDNESS_TAPS];
    float taps[LOUDNESS_PHASES][LOUDNESS_TAPS];
    float peak;
};

static inline double biquad_run(Biquad *f, int channel, double x) {
    const double y = f->b0 * x + f->z1[channel];
    f->z1[channel] = f->b1 * x - f->a1 * y + f->z2[channel];
    f->z2[channel] = f->b2 * x - f->a2 * y;
    return y;
}

DubLoudness *dub_loudness_create(double sampleRate) {
    DubLoudness *m = (DubLoudness *)calloc(1, sizeof(DubLoudness));
    if (!m) return NULL;
    // Stage 1: the head's high shelf (+4 dB above ~1.7 kHz). Stage 2: RLB high-pass (~38 Hz).
    {
        const double f0 = 1681.974450955533, gain = 3.999843853973347, q = 0.7071752369554196;
        const double k = tan(DUB_PI * f0 / sampleRate), vh = pow(10.0, gain / 20.0), vb = pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        m->shelf = (Biquad){ (vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                             2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0, {0, 0}, {0, 0} };
    }
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double k = tan(DUB_PI * f0 / sampleRate), a0 = 1.0 + k / q + k * k;
        m->highPass = (Biquad){ 1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0, {0, 0}, {0, 0} };
    }
    m->segmentLength = (int)lround(sampleRate * 0.1);
    if (m->segmentLength < 1) m->segmentLength = 1;
    // True peak: windowed-sinc interpolator, one phase per oversampled position, each phase summing to 1.
    const int length = LOUDNESS_PHASES * LOUDNESS_TAPS;
    for (int p = 0; p < LOUDNESS_PHASES; p++) {
        double sum = 0;
        for (int k = 0; k < LOUDNESS_TAPS; k++) {
            const int n = k * LOUDNESS_PHASES + p;
            const double t = ((double)n - (length - 1) / 2.0) / LOUDNESS_PHASES;
            const double sinc = fabs(t) < 1e-9 ? 1.0 : sin(DUB_PI * t) / (DUB_PI * t);
            const double window = 0.42 - 0.5 * cos(2.0 * DUB_PI * n / (length - 1)) + 0.08 * cos(4.0 * DUB_PI * n / (length - 1));
            m->taps[p][k] = (float)(sinc * window);
            sum += sinc * window;
        }
        for (int k = 0; k < LOUDNESS_TAPS; k++) m->taps[p][k] = (float)(m->taps[p][k] / sum);
    }
    return m;
}

void dub_loudness_destroy(DubLoudness *m) {
    if (!m) return;
    free(m->segments);
    free(m);
}

static int push_segment(DubLoudness *m) {
    if (m->segmentCount == m->segmentCapacity) {
        const long capacity = m->segmentCapacity ? m->segmentCapacity * 2 : 4096;
        double *grown = (double *)realloc(m->segments, (size_t)capacity * sizeof(double));
        if (!grown) return 0;
        m->segments = grown;
        m->segmentCapacity = capacity;
    }
    m->segments[m->segmentCount++] = m->segmentSum;
    m->segmentSum = 0;
    m->segmentFill = 0;
    return 1;
}

int dub_loudness_add(DubLoudness *m, const float *left, const float *right, int frames) {
    const float *channels[2] = { left, right };
    for (int n = 0; n < frames; n++) {
        for (int c = 0; c < 2; c++) {
            const float x = channels[c][n];
            const double weighted = biquad_run(&m->highPass, c, biquad_run(&m->shelf, c, x));
            m->segmentSum += weighted * weighted;
            // True peak: the sample itself and the three positions between it and the previous one.
            float *h = m->history[c];
            memmove(h + 1, h, (LOUDNESS_TAPS - 1) * sizeof(float));
            h[0] = x;
            for (int p = 0; p < LOUDNESS_PHASES; p++) {
                float y = 0;
                for (int k = 0; k < LOUDNESS_TAPS; k++) y += m->taps[p][k] * h[k];
                if (fabsf(y) > m->peak) m->peak = fabsf(y);
            }
            if (fabsf(x) > m->peak) m->peak = fabsf(x);
        }
        if (++m->segmentFill == m->segmentLength && !push_segment(m)) return 0;
    }
    return 1;
}

static double block_loudness(double meanSquare) { return -0.691 + 10.0 * log10(meanSquare); }

double dub_loudness_integrated(const DubLoudness *m) {
    const long blocks = m->segmentCount - 3; // a 400 ms block = 4 segments
    if (blocks < 1) return -INFINITY;
    const double length = 4.0 * m->segmentLength;
    double sum = 0;
    long count = 0;
    for (int pass = 0; pass < 2; pass++) {
        const double gate = pass == 0 ? -70.0 : block_loudness(sum / count) - 10.0;
        if (pass == 1 && count == 0) return -INFINITY;
        double gatedSum = 0;
        long gatedCount = 0;
        for (long i = 0; i < blocks; i++) {
            const double z = (m->segments[i] + m->segments[i + 1] + m->segments[i + 2] + m->segments[i + 3]) / length;
            if (z <= 0) continue;
            const double l = block_loudness(z);
            if (l > -70.0 && l > gate) {
                gatedSum += z;
                gatedCount++;
            }
        }
        sum = gatedSum;
        count = gatedCount;
    }
    return count > 0 ? block_loudness(sum / count) : -INFINITY;
}

double dub_loudness_true_peak(const DubLoudness *m) {
    return m->peak > 0 ? 20.0 * log10(m->peak) : -INFINITY;
}
