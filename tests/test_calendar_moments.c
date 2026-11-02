/* Exact moment boundary through the allocation-free C query and AML executors. */
#include "ariannamethod.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks;
#define CHECK(c) do{checks++;if(!(c)){fprintf(stderr,"FAIL moments:%d: %s\n",__LINE__,#c);exit(1);}}while(0)
static float number(const AM_CalendarValue* v,const char* name){
 for(int i=0;i<v->count;i++)if(!strcmp(v->keys[i],name))return v->numbers[i];
 CHECK(0);return 0;
}
static AM_CalendarReply query(int op,const int* args){
 AM_CalendarReply r;CHECK(am_calendar_query(op,args,am_calendar_argument_count(op),&r)==0);return r;
}
static void native(void){
 int before[]={-1,86399,999},between[]={-1,86399,999,0,0,0};
 AM_CalendarReply r=query(AM_CAL_INSTANT,before);CHECK(r.kind==2&&number(&r.value,"millisecond")==999);
 r=query(AM_CAL_COMPARE,between);CHECK(r.kind==1&&r.scalar==-1);
 r=query(AM_CAL_DURATION,between);CHECK(number(&r.value,"sign")==1&&number(&r.value,"days")==0&&number(&r.value,"milliseconds")==1);
 int backwards[]={0,0,0,-1,86399,999};r=query(AM_CAL_DURATION,backwards);CHECK(number(&r.value,"sign")==-1&&number(&r.value,"milliseconds")==1);
 int shifted[]={0,0,0,-1,0,0,1};r=query(AM_CAL_SHIFT,shifted);CHECK(number(&r.value,"unix_day")==-1&&number(&r.value,"second")==86399&&number(&r.value,"millisecond")==999);
 const int low=AM_CALENDAR_MIN_DAY+AM_CALENDAR_UNIX_EPOCH_DAY,high=AM_CALENDAR_MAX_DAY+AM_CALENDAR_UNIX_EPOCH_DAY;
 int whole[]={low,0,0,high,86399,999};r=query(AM_CAL_DURATION,whole);
 CHECK(number(&r.value,"days")==3652058&&number(&r.value,"seconds")==86399&&number(&r.value,"milliseconds")==999);
 int maxshift[]={low,0,0,1,3652058,86399,999};r=query(AM_CAL_SHIFT,maxshift);CHECK(number(&r.value,"unix_day")==high&&number(&r.value,"millisecond")==999);
 int points[]={0,0,0,0,0,1,0,0,0};r=query(AM_CAL_CONTAINS,points);CHECK(r.scalar==1);
 points[8]=1;r=query(AM_CAL_CONTAINS,points);CHECK(r.scalar==0);
 int touching[]={0,0,0,0,0,1,0,0,1,0,0,2};r=query(AM_CAL_OVERLAPS,touching);CHECK(r.scalar==0);
 r=query(AM_CAL_INTERSECTION,touching);CHECK(number(&r.value,"start_millisecond")==1&&number(&r.value,"end_millisecond")==1);
 int empty[]={0,0,1,0,0,1,0,0,0,0,0,2};r=query(AM_CAL_OVERLAPS,empty);CHECK(r.scalar==0);
 AM_CalendarReply saved;memset(&saved,0xa5,sizeof(saved));r=saved;
 CHECK(am_calendar_query(AM_CAL_INSTANT,before,2,&r)==1&&!memcmp(&r,&saved,sizeof(r)));
 CHECK(am_calendar_query(-1,before,3,&r)==1&&!memcmp(&r,&saved,sizeof(r)));
 CHECK(am_calendar_query(AM_CAL_INSTANT,NULL,3,&r)==1);
 CHECK(am_calendar_query(AM_CAL_INSTANT,before,3,NULL)==1);
 int invalid[][7]={{0,86400,0},{0,0,1000},{high+1,0,0},{0,-1,0},{16777217,0,0}};
 for(int i=0;i<5;i++)CHECK(am_calendar_query(AM_CAL_INSTANT,invalid[i],3,&r)==1&&!memcmp(&r,&saved,sizeof(r)));
 int wrongduration[]={1,0,0,0};CHECK(am_calendar_query(AM_CAL_DURATION_NEW,wrongduration,4,&r)==1&&!memcmp(&r,&saved,sizeof(r)));
 int overflow[]={high,86399,999,1,0,0,1};CHECK(am_calendar_query(AM_CAL_SHIFT,overflow,7,&r)==1&&!memcmp(&r,&saved,sizeof(r)));
}
#ifndef AM_CALENDAR_ONLY
static int run(int mode,const char* source){
 if(!mode)return am_exec(source);
 if(mode==1){void* p=am_program_open(source);if(!p)return 1;int limit=20000;while(!am_program_step(p,1))CHECK(--limit>0);return am_program_close(p);}
 void* p=am_compile(source);if(!p)return 1;int rc=am_exec_compiled(p);am_free_compiled(p);return rc;
}
static void invalids(void){
 const char* calls[]={"calendar_instant(0, 0)","calendar_instant(0, 0, 0.5)","calendar_instant(0, 86400, 0)",
 "calendar_instant('day', 0, 0)","calendar_compare(0, p)","calendar_duration(p, 0)",
 "calendar_compare(p, bad)","calendar_span(q, p)","calendar_contains(p, q)",
 "calendar_overlaps(span, bad)","calendar_intersection(span, p)","calendar_duration_new(1,0,0,0)",
 "calendar_duration_new(0,0,0,1)","calendar_duration_new(-1,0,86400,0)","calendar_shift(p, bad)",
 "calendar_shift(p, span)","calendar_span(p, bad)","calendar_compare(p, corrupt)"};
 for(int mode=0;mode<3;mode++){
  am_init();am_persistent_mode(1);
  CHECK(run(mode,"p=calendar_instant(0,0,0)\nq=calendar_instant(0,0,1)\nspan=calendar_span(p,q)\nbad=record_clone(p)\nrecord_set(bad,'time_scale','local')\ncorrupt=record_clone(p)\nrecord_set(corrupt,'second',86400)\nkept=17\n")==0);
  for(size_t i=0;i<sizeof(calls)/sizeof(calls[0]);i++){
   char source[512];snprintf(source,sizeof(source),"sentinel=0\nkept=%s\nsentinel=1\n",calls[i]);
   CHECK(run(mode,source)!=0);CHECK(am_get_var_float("kept")==17&&am_get_var_float("sentinel")==0);
  }
  CHECK(run(mode,"def CALENDAR_COMPARE(a,b):\n    return 0\n")==1);
  am_persistent_clear();am_persistent_mode(0);
 }
}
#endif
int main(int argc,char** argv){
#ifndef AM_CALENDAR_ONLY
 if(argc==4&&!strcmp(argv[1],"--run")){
  FILE* f=fopen(argv[3],"rb");CHECK(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);CHECK(n>=0&&n<1048576);
  char* source=calloc((size_t)n+1,1);CHECK(source);CHECK(fread(source,1,(size_t)n,f)==(size_t)n);fclose(f);
  am_init();int rc=run(atoi(argv[2]),source);if(rc)fprintf(stderr,"%s\n",am_get_error());free(source);return rc;
 }
 invalids();
#else
 (void)argc;(void)argv;
#endif
 native();printf("PASS calendar moments C: %d checks\n",checks);return 0;
}
