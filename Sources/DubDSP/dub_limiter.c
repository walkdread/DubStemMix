// Master safety limiter (PRD § 5.2): look-ahead peak limiter, stereo linked, ceiling −1 dBFS.
// Each sample asks for the gain that keeps it under the ceiling. That request is held at its minimum over the
// look-ahead window, then averaged over the same window: the gain ramps down smoothly and is already low enough
// when the peak leaves the delay line (the audio is delayed by the window, 2 ms). Release is automatic: quick
// after an isolated peak, slow while the limiter keeps working (a phaser, a runaway delay), so it never rides
// the bass waveform. Switched off, the gain glides back to unity but the delay stays: no jump, no click.

#include "dub_internal.h"

#define LIMITER_RING 256              // ≥ look-ahead at 96 kHz (192 samples) + 1, power of two
#define LIMITER_LOOKAHEAD 0.002       // seconds
#define LIMITER_CEILING 0.891250938f  // −1 dBFS
#define LIMITER_RELEASE_FAST 0.05     // seconds, after an isolated peak
#define LIMITER_RELEASE_SLOW 0.6      // seconds, while limiting goes on
#define LIMITER_SUSTAIN_TIME 0.5      // seconds for "still limiting" to build up or fade

typedef struct {
    int length;                        // look-ahead window, samples
    int write;                         // ring position of the current sample
    long count;                        // samples since prepare
    float delayL[LIMITER_RING], delayR[LIMITER_RING];
    // Sliding minimum of the requested gain (monotonic deque, ring indexed).
    float minValue[LIMITER_RING];
    long minTime[LIMITER_RING];
    int minHead, minSize;
    // Moving average of the held minimum.
    float held[LIMITER_RING];
    double heldSum;
    float gain;                        // applied, after release
    float sustained;                   // 0 … 1: how much the limiter has been working lately
} LimiterState;

static void limiter_prepare(DubEffect *e) {
    LimiterState *s = (LimiterState *)e->state;
    memset(s, 0, sizeof *s);
    s->length = (int)lround(LIMITER_LOOKAHEAD * e->sampleRate);
    if (s->length < 1) s->length = 1;
    if (s->length > LIMITER_RING - 1) s->length = LIMITER_RING - 1;
    for (int i = 0; i < LIMITER_RING; i++) s->held[i] = 1.0f;
    s->heldSum = s->length;
    s->gain = 1.0f;
}

static inline float limiter_hold(LimiterState *s, float request) {
    while (s->minSize > 0 && s->minValue[(s->minHead + s->minSize - 1) & (LIMITER_RING - 1)] >= request) s->minSize--;
    const int back = (s->minHead + s->minSize) & (LIMITER_RING - 1);
    s->minValue[back] = request;
    s->minTime[back] = s->count;
    s->minSize++;
    while (s->minTime[s->minHead] <= s->count - s->length) {
        s->minHead = (s->minHead + 1) & (LIMITER_RING - 1);
        s->minSize--;
    }
    return s->minValue[s->minHead];
}

static void limiter_process(DubEffect *e, const float *inL, const float *inR, float *outL, float *outR, int frames) {
    LimiterState *s = (LimiterState *)e->state;
    const float sr = (float)e->sampleRate;
    const int bypass = dub_param(e, DUB_LIMITER_BYPASS) >= 0.5f;
    const float fast = dub_onepole(1.0 / (2.0 * DUB_PI * LIMITER_RELEASE_FAST), sr);
    const float slow = dub_onepole(1.0 / (2.0 * DUB_PI * LIMITER_RELEASE_SLOW), sr);
    const float sustain = dub_onepole(1.0 / (2.0 * DUB_PI * LIMITER_SUSTAIN_TIME), sr);
    const int length = s->length;
    float lowest = 1.0f;

    for (int n = 0; n < frames; n++) {
        const float l = inL[n], r = inR[n];
        const float peak = fmaxf(fabsf(l), fabsf(r));
        const float request = peak > LIMITER_CEILING ? LIMITER_CEILING / peak : 1.0f;

        // Held minimum over the window, averaged over the window: the sample leaving the delay line
        // (length − 1 samples old) is covered by every held value in the average.
        const float hold = limiter_hold(s, request);
        const int slot = (int)(s->count % length);
        s->heldSum += (double)hold - (double)s->held[slot];
        s->held[slot] = hold;
        float target = (float)(s->heldSum / length);
        if (target > 1.0f) target = 1.0f;
        if (bypass) target = 1.0f;

        if (target < s->gain) {
            s->gain = target;
        } else {
            s->gain += (target - s->gain) * (fast + (slow - fast) * s->sustained);
        }
        s->sustained += ((s->gain < 0.97f ? 1.0f : 0.0f) - s->sustained) * sustain;
        if (s->gain < lowest) lowest = s->gain;

        s->delayL[s->write] = l;
        s->delayR[s->write] = r;
        const int read = (s->write - (length - 1)) & (LIMITER_RING - 1);
        outL[n] = s->delayL[read] * s->gain;
        outR[n] = s->delayR[read] * s->gain;
        s->write = (s->write + 1) & (LIMITER_RING - 1);
        s->count++;
    }

    // Report the deepest reduction since the reader last cleared it.
    if (lowest < 1.0f && !bypass) {
        const float reduction = -20.0f * log10f(lowest);
        if (reduction > dub_param(e, DUB_LIMITER_REDUCTION)) dub_effect_set(e, DUB_LIMITER_REDUCTION, reduction);
    }
}

static void limiter_destroy(DubEffect *e) { free(e->state); }

void dub_limiter_install(DubEffect *e) {
    LimiterState *s = (LimiterState *)calloc(1, sizeof(LimiterState));
    if (!s) return;
    e->state = s;
    e->prepare = limiter_prepare;
    e->process = limiter_process;
    e->destroy = limiter_destroy;
    dub_effect_set(e, DUB_LIMITER_BYPASS, 0.0f);
    dub_effect_set(e, DUB_LIMITER_REDUCTION, 0.0f);
}
