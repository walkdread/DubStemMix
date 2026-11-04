#include "DubDSP.h"
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "Failed line %d: %s\n", __LINE__, #c); return 1; } } while (0)
int main(void) {
    _Atomic float af = 0;
    _Atomic long long ai = 0;
    CHECK(atomic_is_lock_free(&af));
    CHECK(atomic_is_lock_free(&ai));
    const double rates[] = {44100, 48000, 96000};
    const int blocks[] = {1, 64, 127, 256, 512, 4096};
    float l[4096], r[4096], ol[4096], or_[4096];
    for (int sr = 0; sr < 3; ++sr) {
        for (int kind = DUB_EFFECT_DELAY; kind <= DUB_EFFECT_SYNDRUM; ++kind) {
            DubEffect *fx = dub_effect_create((DubEffectKind)kind);
            CHECK(fx != NULL);
            dub_effect_prepare(fx, rates[sr]);
            for (int b = 0; b < 6; ++b) {
                int n = blocks[b];
                for (int i = 0; i < n; ++i) { l[i] = i == 0 ? 0.5f : 0; r[i] = -l[i]; }
                dub_effect_process(fx, l, r, ol, or_, n);
                for (int i = 0; i < n; ++i) CHECK(isfinite(ol[i]) && isfinite(or_[i]));
                dub_effect_process(fx, l, r, l, r, n);
                for (int i = 0; i < n; ++i) CHECK(isfinite(l[i]) && isfinite(r[i]));
            }
            dub_effect_destroy(fx);
        }
    }
    DubEffect *sub = dub_effect_create(DUB_EFFECT_SUB);
    CHECK(sub != NULL);
    dub_effect_set(sub, DUB_SUB_AMOUNT, 0);
    for (int i = 0; i < 256; ++i) { l[i] = 0.25f; r[i] = -0.25f; }
    dub_effect_process(sub, l, r, ol, or_, 256);
    for (int i = 0; i < 256; ++i) CHECK(ol[i] == l[i] && or_[i] == r[i]);
    dub_effect_destroy(sub);
    DubPortal *portal = dub_portal_create();
    CHECK(portal != NULL);
    dub_portal_set_gain(portal, 0, 1, 1);
    for (int i = 0; i < 512; ++i) { l[i] = 1; r[i] = -1; }
    dub_portal_read(portal, 1, 0, ol, or_, 512);
    for (int i = 0; i < 512; ++i) CHECK(ol[i] == 0 && or_[i] == 0);
    dub_portal_write(portal, 0, 0, l, r, 512);
    dub_portal_read(portal, 1, 512, ol, or_, 512);
    for (int i = 0; i < 512; ++i) CHECK(ol[i] == 1 && or_[i] == -1);
    dub_portal_destroy(portal);
    puts("PASS: 15 effects, 3 rates, 6 block sizes, in-place buffers, bypass, portal and lock-free atomics");
    return 0;
}
