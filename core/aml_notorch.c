// Optional canonical NoTorch values and sampling binding for the AML core.
// Copyright (C) 2026 Oleg Ataeff & Arianna Method contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ariannamethod.h"
#include "notorch.h"
#include "sentencepiece.h"
#include <stdio.h>

static void* aml_notorch_tokenizer_load(const char* path, char* error, size_t cap) {
    return nt_spm_load(path, error, cap);
}

static void aml_notorch_tokenizer_destroy(void* model) {
    nt_spm_free(model);
}

static const char* aml_notorch_tokenizer_identity(const void* model) {
    return nt_spm_identity(model);
}

static int aml_notorch_tokenizer_pieces(const void* model, const char* text, size_t bytes,
                                       AM_TokenizerEmit emit, void* context,
                                       char* error, size_t error_cap) {
    nt_spm_result result = {0};
    if (nt_spm_encode(model, text, bytes, &result, error, error_cap) != 0) return -1;
    int rc = 0;
    for (size_t i = 0; i < result.count; i++) {
        const nt_spm_piece* piece = &result.pieces[i];
        if (piece->offset > result.normalized_bytes ||
            piece->length > result.normalized_bytes - piece->offset) {
            if (error && error_cap) snprintf(error, error_cap, "invalid NoTorch tokenizer piece span");
            rc = -1;
            break;
        }
        if (emit(context, result.normalized + piece->offset, piece->length) != 0) {
            rc = -1;
            break;
        }
    }
    nt_spm_result_free(&result);
    return rc;
}

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
    const AM_TokenizerBackend tokenizer = {
        .load = aml_notorch_tokenizer_load,
        .destroy = aml_notorch_tokenizer_destroy,
        .pieces = aml_notorch_tokenizer_pieces,
        .identity = aml_notorch_tokenizer_identity,
    };
    am_use_notorch_sampling();
    am_set_numerical_backend(&backend);
    am_set_tokenizer_backend(&tokenizer);
}
