// Optional canonical NoTorch values and sampling binding for the AML core.
// Copyright (C) 2026 Oleg Ataeff & Arianna Method contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "ariannamethod.h"
#include "notorch.h"
#include "sentencepiece.h"
#include "pen7.h"
#include <stdio.h>
#include <string.h>

static void* aml_notorch_model_load(const char* path, char* error, size_t cap) {
    nt_pen7_model* model = NULL;
    int rc = nt_pen7_load(path, &model);
    if (rc && error && cap) snprintf(error, cap, "%s", nt_pen7_error_string(rc));
    return rc ? NULL : model;
}

static void aml_notorch_model_destroy(void* model) { nt_pen7_release(model); }

static int aml_notorch_model_info(const void* model, AM_ModelInfo* out) {
    const nt_pen7_info* info = nt_pen7_get_info(model);
    if (!info || !out) return -1;
    out->vocab = info->vocab; out->width = info->width;
    out->context = info->context; out->layers = info->layers;
    out->ffn = info->ffn; out->heads = info->heads;
    memcpy(out->identity, info->identity, sizeof(out->identity));
    memcpy(out->tokenizer_identity, info->tokenizer_identity, sizeof(out->tokenizer_identity));
    return 0;
}

static int aml_notorch_model_encode(const void* model, const char* text, size_t bytes,
                                    int* ids, size_t capacity, size_t* count,
                                    char* error, size_t cap) {
    int rc = nt_pen7_encode(model, text, bytes, ids, capacity, count);
    if (rc && error && cap) snprintf(error, cap, "%s", nt_pen7_error_string(rc));
    return rc;
}

static const unsigned char* aml_notorch_model_piece(const void* model, int id, size_t* bytes) {
    return nt_pen7_token_piece(model, id, bytes);
}

static int aml_notorch_model_forward(const void* left, const void* right, float alpha,
                                     const int* ids, size_t count,
                                     float* logits, size_t logits_count,
                                     float* hidden, size_t hidden_count,
                                     char* error, size_t cap) {
    nt_pen7_workspace* workspace = NULL;
    int rc = nt_pen7_workspace_new((nt_pen7_model*)left, count, &workspace);
    if (!rc) {
        rc = right ? nt_pen7_mix_forward(left, right, alpha, workspace, ids, count,
                                         logits, logits_count, hidden, hidden_count)
                   : nt_pen7_forward(left, workspace, ids, count,
                                      logits, logits_count, hidden, hidden_count);
    }
    nt_pen7_workspace_free(workspace);
    if (rc && error && cap) snprintf(error, cap, "%s", nt_pen7_error_string(rc));
    return rc;
}

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
    const AM_ParliamentBackend parliament = {
        .parliament_elect = nt_parliament_elect_values,
        .parliament_inject = nt_parliament_inject_values,
        .oja = nt_oja_values,
    };
    const AM_TokenizerBackend tokenizer = {
        .load = aml_notorch_tokenizer_load,
        .destroy = aml_notorch_tokenizer_destroy,
        .pieces = aml_notorch_tokenizer_pieces,
        .identity = aml_notorch_tokenizer_identity,
    };
    const AM_ModelBackend model = {
        .load = aml_notorch_model_load, .destroy = aml_notorch_model_destroy,
        .info = aml_notorch_model_info, .encode = aml_notorch_model_encode,
        .forward = aml_notorch_model_forward, .piece = aml_notorch_model_piece,
    };
    am_use_notorch_sampling();
    am_set_numerical_backend(&backend);
    am_set_parliament_backend(&parliament);
    am_set_tokenizer_backend(&tokenizer);
    am_set_model_backend(&model);
}
