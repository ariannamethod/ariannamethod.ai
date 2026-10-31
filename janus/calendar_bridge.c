/* JANUS.CALENDAR native value transport. Calendar arithmetic stays in libaml. */
#include "ariannamethod.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void json_string(const char* s) {
    putchar('"');
    for(const unsigned char* p=(const unsigned char*)s;*p;p++) {
        if(*p=='"' || *p=='\\') { putchar('\\'); putchar(*p); }
        else if(*p<32) printf("\\u%04x",*p);
        else putchar(*p);
    }
    putchar('"');
}
static int json_record(AM_Record* r) {
    AM_List* keys=am_record_keys(r); if(!keys) return 1;
    putchar('{');
    for(int i=0;i<keys->len;i++) {
        if(i) putchar(',');
        json_string(keys->items[i]->data); putchar(':');
        const AML_Var* v=am_record_get(r,keys->items[i]);
        if(v->type==AML_TYPE_STRING) json_string(v->string->data);
        else printf("%.9g",v->value);
    }
    putchar('}'); am_list_free(keys); return 0;
}
int main(int argc, char** argv) {
    if(argc<2) return 2;
    int a[5]={0},n=argc-2,need=-1;
    const char* op=argv[1];
    if(!strcmp(op,"gregorian") || !strcmp(op,"hebrew") || !strcmp(op,"anniversary")) need=3;
    else if(!strcmp(op,"date")) need=1;
    else if(!strcmp(op,"subject")) need=2;
    else if(!strcmp(op,"local")) need=4;
    else if(!strcmp(op,"add")) need=5;
    if(n!=need) { fputs("invalid calendar operation or arity\n",stderr); return 2; }
    for(int i=0;i<n;i++) {
        char* end; errno=0; long v=strtol(argv[i+2],&end,10);
        if(errno || !argv[i+2][0] || *end || v < -16777216 || v>16777216) {
            fputs("calendar arguments must be exact integers\n",stderr); return 2;
        }
        a[i]=(int)v;
    }
    AM_Record* r=NULL; int value=0,rc=0,record=0;
    if(!strcmp(op,"gregorian")) rc=am_calendar_from_gregorian(a[0],a[1],a[2],&value);
    else if(!strcmp(op,"hebrew")) rc=am_calendar_from_hebrew(a[0],a[1],a[2],&value);
    else if(!strcmp(op,"anniversary")) rc=am_calendar_anniversary(a[0],a[1],a[2],&value);
    else if(!strcmp(op,"add")) rc=am_calendar_add(a[0],a[1],a[2],a[3],a[4],&value);
    else {
        record=1;
        if(!strcmp(op,"date")) r=am_calendar_date(a[0]);
        else if(!strcmp(op,"subject")) r=am_calendar_subject(a[0],a[1]);
        else r=am_calendar_local(a[0],a[1],a[2],a[3]);
        rc=!r;
    }
    if(rc) { fputs("calendar date, recurrence or allocation outside contract\n",stderr); return 2; }
    fputs("{\"api\":\"janus.calendar/1\",\"value\":",stdout);
    rc=record ? json_record(r) : (printf("%d",value),0);
    fputs("}\n",stdout); am_record_free(r); return rc;
}
