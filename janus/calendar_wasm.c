/* Thin WASM ABI over the canonical, allocation-free calendar values. */
#include "ariannamethod.h"
static AM_CalendarReply reply;
int janus_abi(void) { return 2; }
int janus_call(int op,int a,int b,int c,int d,int e,int f,int g,int h,int i,int j,int k,int l) {
    reply.kind=0;reply.scalar=0;reply.value.count=0;
    int args[]={a,b,c,d,e,f,g,h,i,j,k,l};
    return am_calendar_query(op,args,am_calendar_argument_count(op),&reply);
}
int janus_kind(void) { return reply.kind; }
int janus_scalar(void) { return reply.scalar; }
int janus_count(void) { return reply.kind==2 ? reply.value.count : 0; }
const char* janus_key(int i) { return i>=0 && i<janus_count() ? reply.value.keys[i] : 0; }
const char* janus_text(int i) { return i>=0 && i<janus_count() ? reply.value.texts[i] : 0; }
float janus_number(int i) { return i>=0 && i<janus_count() ? reply.value.numbers[i] : 0; }
