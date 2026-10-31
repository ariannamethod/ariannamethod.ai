/* Thin WASM ABI over the canonical, allocation-free calendar values. */
#include "ariannamethod.h"
static AM_CalendarValue result;
static int result_kind;
static int scalar;
int janus_abi(void) { return 1; }
int janus_call(int op,int a,int b,int c,int d,int e) {
    result.count=0; result_kind=0; scalar=0;
    int rc=1;
    switch(op) {
    case 0: rc=am_calendar_from_gregorian(a,b,c,&scalar); break;
    case 1: rc=am_calendar_from_hebrew(a,b,c,&scalar); break;
    case 2: rc=am_calendar_value_date(a,&result); break;
    case 3: rc=am_calendar_value_subject(a,b,&result); break;
    case 4: rc=am_calendar_value_local(a,b,c,d,&result); break;
    case 5: rc=am_calendar_add(a,b,c,d,e,&scalar); break;
    case 6: rc=am_calendar_anniversary(a,b,c,&scalar); break;
    }
    if(rc) { result.count=0; return 1; }
    result_kind=(op==2 || op==3 || op==4) ? 2 : 1;
    return 0;
}
int janus_kind(void) { return result_kind; }
int janus_scalar(void) { return scalar; }
int janus_count(void) { return result_kind==2 ? result.count : 0; }
const char* janus_key(int i) { return i>=0 && i<janus_count() ? result.keys[i] : 0; }
const char* janus_text(int i) { return i>=0 && i<janus_count() ? result.texts[i] : 0; }
float janus_number(int i) { return i>=0 && i<janus_count() ? result.numbers[i] : 0; }
