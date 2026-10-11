// Pull-up tape brake on the master (PRD § 11.3). The master keeps flowing in at normal speed into a buffer; once
// engaged, the output reads that buffer slower and slower (pitch falls with the speed, two octaves down at the
// end, like a tape braking), and fades out over the last stretch. Speed and fade move per sample, so the brake
// never clicks. Once braked it stays silent until released; released, it is a plain passthrough again.
// The output is always 2 samples late (the interpolator needs a sample on each side), so engaging is seamless.

#include "dub_internal.h"

#define BRAKE_LATENCY 2
#define BRAKE_FLOOR 0.25    // slowest speed, reached at the end
#define BRAKE_FADE_FROM 0.55 // fraction of the brake after which the level fades to silence

typedef struct {
    DubLine left, right;
    int engaged;
    double read;            // samples behind the newest one
    long elapsed;           // samples since engaged
} BrakeState;

static void brake_prepare(DubEffect *e) {
    BrakeState *s = (BrakeState *)e->state;
    dub_line_clear(&s->left);
    dub_line_clear(&s->right);
    s->engaged = 0;
    s->read = BRAKE_LATENCY;
    s->elapsed = 0;
}

static void brake_process(DubEffect *e, const float *inL, const float *inR, float *outL, float *outR, int frames) {
    BrakeState *s = (BrakeState *)e->state;
    const int engage = dub_param(e, DUB_BRAKE_ENGAGE) >= 0.5f;
    const double length = dub_clamp(dub_param(e, DUB_BRAKE_SECONDS), 0.1f, 10.0f) * e->sampleRate;
    const double deepest = (double)s->left.mask - 8.0;

    for (int n = 0; n < frames; n++) {
        dub_line_push(&s->left, inL[n]);
        dub_line_push(&s->right, inR[n]);
        if (engage && !s->engaged) {
            s->engaged = 1;
            s->elapsed = 0;
            s->read = BRAKE_LATENCY;
        } else if (!engage && s->engaged) {
            s->engaged = 0;
            s->read = BRAKE_LATENCY;
        }
        if (!s->engaged) {
            outL[n] = dub_line_tap(&s->left, BRAKE_LATENCY + 1);
            outR[n] = dub_line_tap(&s->right, BRAKE_LATENCY + 1);
            continue;
        }
        const double t = (double)s->elapsed / length;
        float gain = 0.0f;
        if (t < 1.0) {
            gain = t < BRAKE_FADE_FROM ? 1.0f : (float)pow(1.0 - (t - BRAKE_FADE_FROM) / (1.0 - BRAKE_FADE_FROM), 2.0);
            const double speed = 1.0 - (1.0 - BRAKE_FLOOR) * t * t; // accelerating like a real brake
            // Reading `speed` samples while one more comes in: the read point falls behind by 1 − speed.
            s->read += 1.0 - speed;
            if (s->read > deepest) s->read = deepest;
        }
        s->elapsed++;
        if (gain <= 0.0f) {
            outL[n] = outR[n] = 0.0f;
            continue;
        }
        const float delay = (float)s->read + 1.0f; // tap 1 is the sample just pushed
        outL[n] = gain * dub_line_tap_frac(&s->left, delay);
        outR[n] = gain * dub_line_tap_frac(&s->right, delay);
    }
}

static void brake_destroy(DubEffect *e) {
    BrakeState *s = (BrakeState *)e->state;
    dub_line_free(&s->left);
    dub_line_free(&s->right);
    free(s);
}

void dub_brake_install(DubEffect *e) {
    BrakeState *s = (BrakeState *)calloc(1, sizeof(BrakeState));
    if (!s) return;
    // The read point falls behind by at most (1 − floor) × 1/3 of the brake: 10 s at 96 kHz fits with room.
    const int length = (int)(DUB_MAX_SAMPLE_RATE * 10.0 * (1.0 - BRAKE_FLOOR) / 3.0) + 16;
    if (!dub_line_init(&s->left, length) || !dub_line_init(&s->right, length)) {
        dub_line_free(&s->left);
        dub_line_free(&s->right);
        free(s);
        return;
    }
    e->state = s;
    e->prepare = brake_prepare;
    e->process = brake_process;
    e->destroy = brake_destroy;
    dub_effect_set(e, DUB_BRAKE_ENGAGE, 0.0f);
    dub_effect_set(e, DUB_BRAKE_SECONDS, 1.3f);
}
