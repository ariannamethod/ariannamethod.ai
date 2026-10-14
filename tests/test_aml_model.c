/* Canonical NoTorch bridge: exact ids/results, immutable models, three host paths. */
#define _POSIX_C_SOURCE 200809L
#include "ariannamethod.h"
#include "pen7.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks;
#define CHECK(c) do { __atomic_add_fetch(&checks, 1, __ATOMIC_RELAXED); if (!(c)) { \
    fprintf(stderr,"FAIL model native line %d: %s (%s)\n",__LINE__,#c,am_get_error());exit(1); \
} } while(0)
static int run(int mode,const char* source) {
    if(mode==0)return am_exec(source);
    if(mode==1){void* p=am_program_open(source);if(!p)return 1;while(!am_program_step(p,1)){}return am_program_close(p);}
    void* p=am_compile(source);if(!p)return 1;int rc=am_exec_compiled(p);am_free_compiled(p);return rc;
}
static char* read_source(const char* path) {
    FILE* f=fopen(path,"rb");if(!f)return NULL;
    if(fseek(f,0,SEEK_END)){fclose(f);return NULL;}long n=ftell(f);
    if(n<0||n>1048576||fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    char* text=malloc((size_t)n+1);if(!text){fclose(f);return NULL;}
    size_t got=fread(text,1,(size_t)n,f);fclose(f);
    if(got!=(size_t)n){free(text);return NULL;}text[n]=0;return text;
}
static const AML_Var* field(const AM_Record* r,const char* key) {
    AM_String* k=am_string_new(key);CHECK(k);const AML_Var* v=am_record_get(r,k);am_string_free(k);return v;
}
static void expect(const AM_Record* r,const float* logits,const float* hidden,int nv,int nh) {
    CHECK(r&&field(r,"ok")->value==1);
    CHECK(field(r,"error")->string->byte_len==0);
    const AM_Array* a=field(r,"logits")->array;const AM_Array* b=field(r,"hidden")->array;
    CHECK(a->len==nv&&b->len==nh);
    CHECK(!memcmp(a->data,logits,(size_t)nv*sizeof(float)));
    CHECK(!memcmp(b->data,hidden,(size_t)nh*sizeof(float)));
}
typedef struct {AM_Model* m;AM_Array* ids;float* logits;float* hidden;int nv,nh;} Work;
static void* worker(void* data) {
    Work* w=data;
    for(int i=0;i<3;i++){
        AM_Record* r=am_model_forward(w->m,NULL,1,w->ids);expect(r,w->logits,w->hidden,w->nv,w->nh);am_record_free(r);
    }return NULL;
}
static void native(const char* path) {
    nt_pen7_model* raw=NULL;CHECK(nt_pen7_load(path,&raw)==0);
    const nt_pen7_info* ni=nt_pen7_get_info(raw);CHECK(ni&&ni->context>=2);
    AM_Model* m=am_model_try_load(path,NULL,0);CHECK(am_model_ready(m));
    const AM_ModelInfo* mi=am_model_get_info(m);CHECK(mi);
    CHECK(mi->vocab==ni->vocab&&mi->width==ni->width&&!strcmp(mi->identity,ni->identity));
    CHECK(!strcmp(mi->tokenizer_identity,ni->tokenizer_identity));
    AM_String* text=am_string_new("RaIN");CHECK(text);char error[256];
    AM_Array* ids=am_model_encode(m,text,ni->context,error,sizeof(error));CHECK(ids);
    int* native_ids=calloc((size_t)ni->context,sizeof(int));CHECK(native_ids);size_t count=0;
    CHECK(nt_pen7_encode(raw,text->data,(size_t)text->byte_len,native_ids,(size_t)ni->context,&count)==0);
    CHECK(count==(size_t)ids->len);for(size_t i=0;i<count;i++)CHECK(ids->data[i]==native_ids[i]);
    float* logits=malloc((size_t)ni->vocab*sizeof(float));float* hidden=malloc((size_t)ni->width*sizeof(float));CHECK(logits&&hidden);
    nt_pen7_workspace* ws=NULL;CHECK(nt_pen7_workspace_new(raw,count,&ws)==0);
    CHECK(nt_pen7_forward(raw,ws,native_ids,count,logits,(size_t)ni->vocab,hidden,(size_t)ni->width)==0);
    AM_Record* r=am_model_forward(m,NULL,1,ids);expect(r,logits,hidden,ni->vocab,ni->width);am_record_free(r);
    r=am_model_forward(m,m,0.3f,ids);CHECK(r&&field(r,"ok")->value==1);am_record_free(r);
    am_set_model_backend(NULL);
    pthread_t threads[4];Work w={m,ids,logits,hidden,ni->vocab,ni->width};
    for(int i=0;i<4;i++)CHECK(!pthread_create(&threads[i],NULL,worker,&w));
    for(int i=0;i<4;i++)CHECK(!pthread_join(threads[i],NULL));
    AM_Model* missing=am_model_try_load(path,NULL,0);CHECK(missing&&!am_model_ready(missing));
    r=am_model_forward(m,missing,0.5f,ids);CHECK(r&&field(r,"ok")->value==0&&!field(r,"logits"));am_record_free(r);
    am_model_free(missing);am_model_free(m);am_array_free(ids);am_string_free(text);
    nt_pen7_workspace_free(ws);nt_pen7_release(raw);free(native_ids);free(logits);free(hidden);
    printf("AML_MODEL_NATIVE_OK %d checks; direct NoTorch parity and concurrent per-call workspaces\n",checks);
}
int main(int argc,char** argv) {
    am_use_notorch();am_init();
    if(argc==3&&!strcmp(argv[1],"--native")){native(argv[2]);return 0;}
    if(argc==4&&!strcmp(argv[1],"--run")) {
        char* source=read_source(argv[3]);if(!source)return 2;
        int rc=run(atoi(argv[2]),source);if(rc)fprintf(stderr,"model: %s\n",am_get_error());free(source);return rc;
    }return 2;
}
