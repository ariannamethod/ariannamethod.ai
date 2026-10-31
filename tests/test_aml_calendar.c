/* Calendar C/AML contract, state isolation, ownership, origins and replay. */
#include "ariannamethod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static int checks;
#define CHECK(c) do { checks++; if(!(c)) { fprintf(stderr,"FAIL calendar:%d: %s (%s)\n",__LINE__,#c,am_get_error()); exit(1); } } while(0)
static float number(const AM_Record* r,const char* name) {
    AM_String* k=am_string_new(name); CHECK(k);
    const AML_Var* v=am_record_get(r,k); CHECK(v && v->type==AML_TYPE_FLOAT);
    float result=v->value; am_string_free(k); return result;
}
static int from_greg(int y,int m,int d) { int day; CHECK(am_calendar_from_gregorian(y,m,d,&day)==0); return day; }
static int run(int mode,const char* source) {
    if(mode==0) return am_exec(source);
    if(mode==1) {
        void* p=am_program_open(source); if(!p) return 1;
        int budget=20000;
        while(!am_program_step(p,1)) CHECK(--budget>0);
        return am_program_close(p);
    }
    void* p=am_compile(source); if(!p) return 1;
    int rc=am_exec_compiled(p); am_free_compiled(p); return rc;
}
static void native(void) {
    am_init(); AM_State before=*am_get_state();
    CHECK(from_greg(2024,10,3)==0);
    CHECK(from_greg(2026,1,23)==477 && from_greg(2026,2,13)==498);
    CHECK(from_greg(1,1,1)==AM_CALENDAR_MIN_DAY);
    CHECK(from_greg(9999,12,31)==AM_CALENDAR_MAX_DAY);
    int out=17;
    CHECK(am_calendar_from_gregorian(1900,2,29,&out)==1 && out==17);
    CHECK(am_calendar_from_gregorian(2000,2,29,&out)==0);
    CHECK(am_calendar_from_hebrew(5786,13,1,&out)==1);
    CHECK(am_calendar_from_hebrew(5787,13,1,&out)==0);
    CHECK(am_calendar_date(AM_CALENDAR_MIN_DAY-1)==NULL);
    CHECK(am_calendar_subject(0,AM_CALENDAR_MAX_DAY)==NULL); // next anniversary outside domain
    AM_Record* a=am_calendar_subject(498,730),*b=am_calendar_subject(498,731),*c=am_calendar_subject(477,731);
    CHECK(a && b && c);
    CHECK(number(a,"birthquake")==0 && number(b,"birthquake")==1);
    CHECK(number(b,"personal_dissonance")-number(a,"personal_dissonance")>.47f);
    CHECK(number(b,"birth_drift")!=number(c,"birth_drift"));
    CHECK(number(b,"age_days")==233 && number(c,"age_days")==254);
    CHECK(number(b,"legacy_step") < -29.9f);
    CHECK(number(b,"next_hebrew_day")!=number(b,"next_gregorian_day"));
    am_record_free(a); am_record_free(b); am_record_free(c);
    int old=from_greg(1800,5,2);
    a=am_calendar_subject(old,-100); CHECK(a && number(a,"birth_day")==old); am_record_free(a);
    for(int second=43199;second<=43200;second++) {
        a=am_calendar_local(19999,second,0,-1); CHECK(a);
        CHECK(number(a,"epoch_day")== (second<43200 ? -1:0)); am_record_free(a);
    }
    a=am_calendar_local(19999,57599,120,64800);
    b=am_calendar_local(19999,57600,120,64800); CHECK(a && b);
    CHECK(number(a,"hebrew_day")==1 && number(b,"hebrew_day")==2);
    CHECK(number(a,"day")==0 && number(b,"day")==0);
    am_record_free(a); am_record_free(b);
    a=am_calendar_local(19999,0,-60,-1); CHECK(a && number(a,"day")==-1); am_record_free(a);
    CHECK(am_calendar_local(19999,86400,0,-1)==NULL);
    CHECK(am_calendar_local(19999,0,1441,-1)==NULL);
    CHECK(am_calendar_local(19999,0,0,-2)==NULL);
    CHECK(am_calendar_add(from_greg(2024,1,31),0,1,0,0,&out)==0 && out==from_greg(2024,2,29));
    CHECK(am_calendar_add(from_greg(2024,2,29),1,0,0,0,&out)==0 && out==from_greg(2025,2,28));
    CHECK(am_calendar_anniversary(from_greg(2024,2,29),2025,0,&out)==0 && out==from_greg(2025,3,1));
    CHECK(am_calendar_add(AM_CALENDAR_MIN_DAY,0,-1,0,0,&out)==1);
    CHECK(memcmp(&before,am_get_state(),sizeof(before))==0 && !am_birth_set());
    for(int d=AM_CALENDAR_MIN_DAY;d<=AM_CALENDAR_MAX_DAY;d+=997) {
        a=am_calendar_date(d); CHECK(a);
        CHECK(am_calendar_from_gregorian(number(a,"gregorian_year"),number(a,"gregorian_month"),number(a,"gregorian_day"),&out)==0 && out==d);
        CHECK(am_calendar_from_hebrew(number(a,"hebrew_year"),number(a,"hebrew_month"),number(a,"hebrew_day"),&out)==0 && out==d);
        am_record_free(a);
    }
    // C pure view equals the existing bound self and leaves its origin latched.
    CHECK(am_exec("BIRTH 498\nSELF_NOW_DAYS 731\nJANUS_KEY 1\n")==0); am_step(0.1f);
    float birth=am_get_state()->birth_drift,pd=am_get_state()->personal_dissonance;
    a=am_calendar_subject(498,731); CHECK(a);
    CHECK(number(a,"birth_drift")==birth && number(a,"personal_dissonance")==pd);
    CHECK(number(a,"janus_gap")==am_get_state()->janus_gap);
    CHECK(number(a,"yahrzeit")==am_get_state()->yahrzeit);
    CHECK(number(a,"janus_temporal_alpha")==am_get_state()->janus_temporal_alpha);
    am_record_free(a);
    CHECK(am_exec("BIRTH -100\nSELF_NOW_DAYS 730\n")==0); am_step(0.1f);
    CHECK(am_birth_epoch_days()==498 && am_get_state()->birth_drift==birth);
}
static void invalids(int mode) {
    static const char* cases[]={"calendar_date()","calendar_date(1,2)","calendar_date('day')","calendar_date(0.5)",
      "calendar_date(zeros(1))","calendar_date(16777216)","calendar_gregorian(2023,2,29)",
      "calendar_gregorian(0,1,1)","calendar_gregorian(10000,1,1)","calendar_hebrew(5786,13,1)",
      "calendar_hebrew(5787,13,30)","calendar_local(19999,86400,0,-1)","calendar_local(19999,0,0,86400)",
      "calendar_subject(498,2912897)","calendar_add(0,0,0,0,2)","calendar_anniversary(0,0,0)"};
    am_init(); am_persistent_mode(1);
    CHECK(run(mode,"kept = 17\n")==0);
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        char s[256]; snprintf(s,sizeof(s),"sentinel=0\nkept=%s\nsentinel=1\n",cases[i]);
        int rc=run(mode,s); if(!rc) fprintf(stderr,"accepted mode %d: %s\n",mode,cases[i]);
        CHECK(rc!=0); CHECK(am_get_var_float("kept")==17 && am_get_var_float("sentinel")==0);
    }
    CHECK(run(mode,"def CaLeNdAr_DaTe(day):\n    return day\n")==1);
    am_persistent_clear(); am_persistent_mode(0);
}
static void emit_dates(void) {
    int first=from_greg(2023,1,1),last=from_greg(2031,1,1);
    for(int pass=0;pass<2;pass++) {
        int begin=pass?first:AM_CALENDAR_MIN_DAY,end=pass?last:AM_CALENDAR_MAX_DAY,step=pass?1:997;
        for(int d=begin;d<=end;d+=step) {
            AM_Record* r=am_calendar_date(d); CHECK(r);
            printf("%d,%d,%d,%d,%d,%d,%d\n",d,(int)number(r,"gregorian_year"),(int)number(r,"gregorian_month"),
              (int)number(r,"gregorian_day"),(int)number(r,"hebrew_year"),(int)number(r,"hebrew_month"),(int)number(r,"hebrew_day"));
            am_record_free(r);
        }
    }
}
int main(int argc,char** argv) {
    if(argc==2 && !strcmp(argv[1],"--dates")) { emit_dates(); return 0; }
    if(argc==4 && !strcmp(argv[1],"--run")) {
        FILE* f=fopen(argv[3],"rb"); CHECK(f); fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
        CHECK(n>=0 && n<1048576); char* source=calloc((size_t)n+1,1); CHECK(source);
        CHECK(fread(source,1,(size_t)n,f)==(size_t)n); fclose(f);
        am_init(); int rc=run(atoi(argv[2]),source); free(source); if(rc) fprintf(stderr,"%s\n",am_get_error()); return rc;
    }
    native(); for(int mode=0;mode<3;mode++) invalids(mode);
    printf("PASS calendar C: %d checks\n",checks); return 0;
}
