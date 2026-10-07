// Optional canonical NoTorch values and sampling binding for the AML core.
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

void am_use_notorch(void) {
    const AM_NumericalBackend backend = {
        .linear = nt_linear_values,
        .linear_vjp = nt_linear_vjp_values,
        .tanh = nt_tanh_values,
        .tanh_vjp = nt_tanh_vjp_values,
        .mse_grad = nt_mse_grad_values,
        .sgd = nt_sgd_values,
        .normal = nt_rng_normal_values,
    };
    am_use_notorch_sampling();
    am_set_numerical_backend(&backend);
}
