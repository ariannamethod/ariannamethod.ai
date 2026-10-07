// Optional canonical NoTorch sampling binding for the standalone AML core.
// Copyright (C) 2026 Oleg Ataeff & Arianna Method contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ariannamethod.h"
#include "notorch.h"

void am_use_notorch_sampling(void) {
    const AM_SamplingBackend backend = {
        .seed = nt_rng_seed,
        .u32 = nt_rng_u32,
        .uniform = nt_rng_uniform,
        .index = nt_rng_index,
        .categorical_at = nt_categorical_index,
        .categorical = nt_rng_categorical,
    };
    am_set_sampling_backend(&backend);
}
