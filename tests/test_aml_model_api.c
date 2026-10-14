/* Immutable model values, quiet outcomes, backend ownership and worker transfer. */
#define _POSIX_C_SOURCE 200809L
#include "ariannamethod.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static int checks;
#define CHECK(c) do { __atomic_add_fetch(&checks, 1, __ATOMIC_RELAXED); if (!(c)) { \
    fprintf(stderr, "FAIL model API line %d: %s (%s)\n", __LINE__, #c, am_get_error()); \
    exit(1); } } while (0)
#ifdef AML_MODEL_ALLOC_WRAP
static int allocation_budget = -1, tracked_count;
static void* tracked[200000];
void* __real_malloc(size_t);
void* __real_calloc(size_t, size_t);
void* __real_realloc(void*, size_t);
void __real_free(void*);
static void track(void* p) {
    if (p && allocation_budget >= 0) {
        CHECK(tracked_count < 200000); tracked[tracked_count++] = p;
    }
}
static void untrack(void* p) {
    for (int i = 0; p && i < tracked_count; i++)
        if (tracked[i] == p) { tracked[i] = tracked[--tracked_count]; return; }
}
static int refuse(void) {
    if (allocation_budget == 0) return 1;
    if (allocation_budget > 0) allocation_budget--;
    return 0;
}
void* __wrap_malloc(size_t n) {
    if (refuse()) return NULL;
    void* p = __real_malloc(n); track(p); return p;
}
void* __wrap_calloc(size_t n, size_t s) {
    if (refuse()) return NULL;
    void* p = __real_calloc(n, s); track(p); return p;
}
void* __wrap_realloc(void* old, size_t n) {
    if (refuse()) return NULL;
    void* p = __real_realloc(old, n);
    if (p) { untrack(old); track(p); }
    return p;
}
void __wrap_free(void* p) { untrack(p); __real_free(p); }
#endif
typedef struct { int bias, guard, bad_metadata; } Native;
static int loads, frees, forwards;
static void* load(const char* path, char* error, size_t cap) {
    if (strstr(path, "missing") || strstr(path, "corrupt")) {
        snprintf(error, cap, "mock file unavailable"); return NULL;
    }
    Native* n = malloc(sizeof(*n));
    if (!n) return NULL;
    *n = (Native){strstr(path, "second") ? 2 : 1, 771, strstr(path, "badmeta") != NULL};
    __atomic_add_fetch(&loads, 1, __ATOMIC_RELAXED); return n;
}
static void destroy(void* data) {
    Native* n = data; CHECK(n && n->guard == 771); n->guard = 0;
    __atomic_add_fetch(&frees, 1, __ATOMIC_RELAXED); free(n);
}
static int info(const void* data, AM_ModelInfo* out) {
    const Native* n = data; CHECK(n && n->guard == 771);
    *out = (AM_ModelInfo){.vocab=8, .width=4, .context=64, .layers=2, .ffn=8, .heads=2};
    memset(out->identity, '0' + n->bias, 64);
    memset(out->tokenizer_identity, 'f', 64);
    if (n->bad_metadata) out->vocab = -1;
    return 0;
}
static int encode(const void* data, const char* text, size_t bytes,
                  int* ids, size_t cap, size_t* count, char* error, size_t error_cap) {
    const Native* n = data; CHECK(n && n->guard == 771);
    if (bytes > cap) { snprintf(error, error_cap, "token capacity exceeded"); return -1; }
    for (size_t i = 0; i < bytes; i++) ids[i] = (unsigned char)text[i] % 8;
    *count = bytes;
    if (!strcmp(text, "invalid")) ids[0] = 99;
    if (!strcmp(text, "excess")) *count = cap + 1;
    return 0;
}
static int forward(const void* l, const void* r, float alpha,
                   const int* ids, size_t count, float* logits, size_t nv,
                   float* hidden, size_t nh, char* error, size_t cap) {
    const Native* a = l; const Native* b = r;
    CHECK(a && a->guard == 771 && (!b || b->guard == 771));
    CHECK(nv == 8 && nh == 4 && count > 0);
    __atomic_add_fetch(&forwards, 1, __ATOMIC_RELAXED);
    float bias = b ? alpha * a->bias + (1-alpha) * b->bias : a->bias;
    for (size_t i = 0; i < nv; i++) logits[i] = bias + (float)i;
    for (size_t i = 0; i < nh; i++) hidden[i] = bias + (float)ids[count-1];
    if (ids[0] == 6) { snprintf(error, cap, "mock rejected after staged writes"); return -1; }
    if (ids[0] == 7) hidden[0] = NAN;
    return 0;
}
static const unsigned char* piece(const void* data, int id, size_t* bytes) {
    const Native* n = data; CHECK(n && n->guard == 771);
    static const unsigned char* values[] = {(unsigned char*)"rain", (unsigned char*)"\xc3",
        (unsigned char*)"\0", (unsigned char*)"é", (unsigned char*)" a", (unsigned char*)"x",
        (unsigned char*)"y", (unsigned char*)"z"};
    *bytes = id == 3 || id == 4 ? 2 : id == 0 ? 4 : 1;
    return values[id];
}
static AM_ModelBackend table(void) {
    return (AM_ModelBackend){load,destroy,info,encode,forward,piece};
}
static void install(void) {
    AM_ModelBackend b = table(); am_set_model_backend(&b); memset(&b, 0, sizeof(b));
}
static int run(int mode, const char* source) {
    if (mode == 0) return am_exec(source);
    if (mode == 1) {
        void* p = am_program_open(source); if (!p) return 1;
        int budget = AML_MAX_LINES + 1; while (!am_program_step(p, 1)) CHECK(--budget > 0);
        return am_program_close(p);
    }
    void* p = am_compile(source); if (!p) return 1;
    int rc = am_exec_compiled(p); am_free_compiled(p); return rc;
}
static const AML_Var* field(const AM_Record* record, const char* name) {
    AM_String* key = am_string_new(name); CHECK(key);
    const AML_Var* value = am_record_get(record, key); am_string_free(key); return value;
}
static void outcome(const AM_Record* r, int ok, float expected) {
    CHECK(r && field(r,"ok") && field(r,"ok")->value == ok);
    const AML_Var* error = field(r,"error"); CHECK(error && error->type == AML_TYPE_STRING);
    if (!ok) {
        CHECK(error->string->byte_len > 0 && !field(r,"logits") && !field(r,"hidden"));
    } else {
        CHECK(error->string->byte_len == 0);
        CHECK(field(r,"logits")->array->len == 8 && field(r,"logits")->array->data[0] == expected);
        CHECK(field(r,"hidden")->array->len == 4);
    }
}
static void test_api(void) {
    am_set_model_backend(NULL);
    AM_Model* absent = am_model_try_load("whatever",NULL,0); CHECK(absent && !am_model_ready(absent));
    CHECK(strstr(am_model_error(absent),"backend") && !am_model_get_info(absent));
    am_model_free(absent);
    for (int missing = 0; missing < 5; missing++) {
        AM_ModelBackend b = table();
        if (missing == 0) b.load = NULL;
        if (missing == 1) b.destroy = NULL;
        if (missing == 2) b.info = NULL;
        if (missing == 3) b.encode = NULL;
        if (missing == 4) b.forward = NULL;
        am_set_model_backend(&b);
        absent = am_model_try_load("mock",NULL,0); CHECK(absent && !am_model_ready(absent)); am_model_free(absent);
    }
    install(); am_init();
    const char* bad[] = {NULL,"","missing","corrupt","badmeta"};
    for (size_t i=0; i<sizeof(bad)/sizeof(*bad); i++) {
        absent = am_model_try_load(bad[i],NULL,0); CHECK(absent && !am_model_ready(absent));
        CHECK(am_model_error(absent)[0]);
        AM_Record* metadata=am_model_info(absent); CHECK(metadata && field(metadata,"vocab")->value == 0);
        AM_Record* r=am_model_forward(absent,NULL,1,NULL); outcome(r,0,0); am_record_free(r);
        am_record_free(metadata); am_model_free(absent);
    }
    AM_Model* a=am_model_try_load("first",NULL,0); AM_Model* b=am_model_try_load("second",NULL,0);
    CHECK(am_model_ready(a) && am_model_ready(b));
    AM_Array* ids=am_array_new(2); CHECK(ids); ids->data[0]=1; ids->data[1]=2;
    am_set_model_backend(NULL); // copied callbacks survive unregistration
    AM_Record* r=am_model_forward(a,NULL,1,ids); outcome(r,1,1); am_record_free(r);
    r=am_model_forward(a,b,0.25f,ids); outcome(r,1,1.75f); am_record_free(r);
    AM_String* text=am_string_new("abc"); char error[256];
    AM_Array* encoded=am_model_encode(a,text,8,error,sizeof(error));
    CHECK(encoded && encoded->len==3 && encoded->data[0]==1); am_array_free(encoded);
    CHECK(!am_model_encode(a,text,1,error,sizeof(error)) && error[0]); am_string_free(text);
    const char* badtext[]={"","invalid","excess"};
    for (int i=0;i<3;i++) { text=am_string_new(badtext[i]); CHECK(text);
        CHECK(!am_model_encode(a,text,8,error,sizeof(error)) && error[0]); am_string_free(text); }
    for (int i=0;i<4;i++) { text=am_model_piece(a,i,error,sizeof(error)); CHECK(text);
        CHECK(!strcmp(text->data,i==0?"rain":i==3?"é":"")); am_string_free(text); }
    CHECK(!am_model_piece(a,8,error,sizeof(error)));
    float invalids[]={-1,0.5f,8,NAN,INFINITY}; int before=forwards;
    for (size_t i=0;i<sizeof(invalids)/sizeof(*invalids);i++) {
        ids->data[0]=invalids[i]; r=am_model_forward(a,NULL,1,ids); outcome(r,0,0); am_record_free(r);
    }
    CHECK(forwards==before);
    for (int i=6;i<=7;i++) { ids->data[0]=(float)i; r=am_model_forward(a,NULL,1,ids); outcome(r,0,0); am_record_free(r); }
    ids->data[0]=1;
    r=am_model_forward(a,b,NAN,ids); outcome(r,0,0); am_record_free(r);
    AM_Record* record=am_record_new(); AM_String* key=am_string_new("body");
    AML_Var handle={.type=AML_TYPE_MODEL,.model=a}; CHECK(am_record_set(record,key,&handle)==-1);
    am_record_free(record); am_string_free(key);
    CHECK(am_set_var_model("saved",a)==0 && am_get_var_model("saved")==a);
    am_model_free(a); am_model_free(b);
    CHECK(am_exec("r = model_forward(saved,[1,2])\nsaved = 0\n")==0);
    outcome(am_get_var_record("r"),1,1); am_persistent_clear(); am_array_free(ids);
    am_model_ref(NULL); am_model_free(NULL);
}
static void test_runtime(int mode) {
    install(); am_init(); am_persistent_mode(1);
    CHECK(run(mode,
        "def keep(value):\n    return value\n"
        "m = model_try_load('mock')\na = m\nb = keep(keep((a)))\n"
        "m = 0\na = [1]\nassert(model_ready(b) == 1,'alive')\n"
        "assert(text_len(model_identity(b)) == 64,'identity')\n"
        "assert(text_len(model_tokenizer_identity(b)) == 64,'tokenizer identity')\n"
        "assert(text_len(model_error(b)) == 0,'no error')\n"
        "info = model_info(b)\nassert(record_get(info,'width') == 4,'geometry')\n"
        "ids = model_encode(b,'abc',8)\nassert(len(ids)==3,'encode')\n"
        "r = model_forward(b,ids)\nassert(record_get(r,'ok')==1,'forward')\n"
        "second = model_try_load('second')\nr = model_mix_forward(b,second,0.25,ids)\n"
        "v = record_get(r,'logits')\nassert(v[0]==1.75,'mix')\n"
        "v[0]=99\nr2=model_mix_forward(b,second,0.25,ids)\nv2=record_get(r2,'logits')\nassert(v2[0]==1.75,'fresh result')\n"
        "disabled=model_try_load('')\nassert(model_ready(disabled)==0,'disabled quiet')\n"
        "missing=model_try_load('missing')\nassert(model_ready(missing)==0,'missing quiet')\n"
        "assert(text_len(model_identity(missing))==0,'missing identity')\n"
        "r=model_mix_forward(b,missing,0.5,ids)\nassert(record_get(r,'ok')==0,'one side missing')\n"
        "assert(record_has(r,'logits')==0,'no partial logits')\n"
        "assert(text_equal(model_piece(b,0),'rain'),'piece')\n"
        "assert(text_equal(model_piece(b,1),''),'byte fragment')\n"
        "kept='sentinel'\n") == 0);
    CHECK(run(mode,"def sixteen(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p):\n    return p\n"
        "wide=sixteen(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,b)\n"
        "assert(model_ready(wide)==1,'sixteen typed args')\n") == 0);
    const char* bad[]={"sixteen(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17)","model_try_load()","model_try_load(1)",
        "model_ready(1)","model_forward(b,1)","model_mix_forward(b,b,'x',[1])",
        "model_encode(b,'abc',1)","model_encode(b,'',8)","model_encode(b,1,8)",
        "model_encode(b,'abc',1.5)","model_piece(b,-1)","model_piece(b,8)",
        "b + 1","1 + b","len(b)","sum(b)","text_len(b)","list_len(b)","map_len(b)",
        "floor(b)","isfinite(b)","nt_tanh(b)","record_set(info,'body',b)","b[0]"};
    for (size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        char source[512]; snprintf(source,sizeof(source),"sentinel=0\nkept=%s\nsentinel=1\n",bad[i]);
        CHECK(run(mode,source)!=0); CHECK(am_get_error()[0] && am_get_var_float("sentinel")==0);
        CHECK(!strcmp(am_get_var_text("kept"),"sentinel") && am_get_var_model("b"));
    }
    CHECK(run(mode,"model_forward(b,[1])\nmodel_ready(b)\n")==0);
    CHECK(run(mode,"def excessive(a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p,q):\n    return p\n")==1);
    CHECK(run(mode,"def model_ready(x):\n    return 0\n")==1);
    am_persistent_clear();
}
static void test_workers(int mode) {
    install(); am_init(); am_persistent_mode(1); int before=frees;
    CHECK(run(mode,
        "def keep(model):\n    return model\n"
        "m=model_try_load('mock')\nCHANNEL CREATE gate 2\n"
        "SPAWN child:\n    CHANNEL READ gate go\n    return keep(m)\n"
        "m=0\nCHANNEL WRITE gate 1\nAWAIT child INTO received\n"
        "r=model_forward(received,[1,2])\nassert(record_get(r,'ok')==1,'worker model owner')\n"
        "i=0\nwhile i<24:\n    SPAWN repeated:\n        return model_forward(received,[1,2])\n"
        "    AWAIT repeated INTO r\n    assert(record_get(r,'ok')==1,'worker inference')\n    i=i+1\n"
        "assert(i==24,'completed every worker')\n"
        "received=0\n")==0);
    CHECK(am_spawn_count()==0 && frees==before+1); am_persistent_clear(); am_channel_close_all();
}
static void test_faults(void) {
#ifdef AML_MODEL_ALLOC_WRAP
    install(); char error[256]; int success=0;
    for (int budget=0;budget<12;budget++) {
        CHECK(tracked_count==0); int l=loads,f=frees; allocation_budget=budget;
        AM_Model* m=am_model_try_load("mock",error,sizeof(error)); allocation_budget=-1;
        if (m && am_model_ready(m)) success=1;
        am_model_free(m); CHECK(tracked_count==0 && loads-l==frees-f); if(success)break;
    } CHECK(success);
    AM_Model* m=am_model_try_load("mock",NULL,0); CHECK(am_model_ready(m));
    AM_Array* ids=am_array_new(2); CHECK(ids); ids->data[0]=1;
    AM_String* text=am_string_new("abc"); CHECK(text);
    for(int op=0;op<4;op++) {
        success=0;
        for(int budget=0;budget<140;budget++) {
            CHECK(tracked_count==0); allocation_budget=budget;
            AM_Record* r=NULL; AM_Array* a=NULL; AM_String* s=NULL;
            if(op==0)r=am_model_info(m);
            if(op==1)r=am_model_forward(m,NULL,1,ids);
            if(op==2)a=am_model_encode(m,text,8,error,sizeof(error));
            if(op==3)s=am_model_piece(m,0,error,sizeof(error));
            allocation_budget=-1;
            if(r||a||s)success=1;
            am_record_free(r);am_array_free(a);am_string_free(s);
            CHECK(tracked_count==0 && am_model_ready(m));
            if(success)break;
        } CHECK(success);
    }
    am_model_free(m);am_array_free(ids);am_string_free(text);
#endif
}
int main(void) {
    test_api(); for(int mode=0;mode<3;mode++){test_runtime(mode);test_workers(mode);}
    test_faults(); am_persistent_clear(); am_init(); CHECK(loads==frees);
    printf("AML_MODEL_API_OK %d checks; %d native owners released exactly once\n",checks,frees); return 0;
}
