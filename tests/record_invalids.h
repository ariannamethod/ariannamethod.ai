/* Invalid expressions are also checked as standalone calls where applicable. */
static const char* record_invalids[] = {
    "record_new(1)", "record_get()", "record_get(state)",
    "record_get(state, 'number', 1)", "record_get(1, 'number')",
    "record_get(state, 1)", "record_get(state, 'absent')",
    "record_set(state, 'number')", "record_set(state, 'number', 1, 2)",
    "record_set(1, 'number', 1)", "record_set(state, 1, 1)",
    "record_set(state, 'nested', record_new())", "record_set(state, 'self', state)",
    "record_has(state)", "record_has(state, 1)", "record_has([1], 'number')",
    "record_keys(1)", "record_keys(state, 1)", "record_kind(state)",
    "record_kind(state, 1)", "record_kind(state, 'absent')",
    "record_clone(1)", "record_clone(state, state)",
    "record_replace(1, state)", "record_replace(state, 1)",
    "record_replace(state)", "record_replace(state, state, state)",
    "record_swap(state)", "record_swap(state, 1)", "record_swap(1, state)",
    "record_swap(state, state, state)",
    "len(state)", "sum(state)", "rows(state)", "cols(state)",
    "dot(state, [1])", "text_len(state)", "list_len(state)", "map_len(state)",
    "isfinite(state)", "floor(state)", "codepoint_isalnum(state)", "nt_tanh(state)",
    "checkpoint_save(state)", "checkpoint_save(state, 1)",
    "checkpoint_save(1, 'state.amlcp')", "checkpoint_save(state, '', 1)",
    "checkpoint_load()", "checkpoint_load(1)", "checkpoint_load('', '')",
    "file_exists()", "file_exists(1)", "file_exists('', 1)"
};
