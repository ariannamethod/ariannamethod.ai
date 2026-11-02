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
static void json_value(const AM_CalendarValue* value) {
    putchar('{');
    for(int i=0;i<value->count;i++) {
        if(i) putchar(',');
        json_string(value->keys[i]);putchar(':');
        if(value->texts[i]) json_string(value->texts[i]);
        else printf("%.9g",value->numbers[i]);
    }
    putchar('}');
}
int main(int argc,char** argv) {
    static const char* names[]={"gregorian","hebrew","date","subject","local","add","anniversary",
      "instant","compare","duration","duration_new","shift","span","contains","overlaps","intersection"};
    if(argc<2) return 2;
    int op=-1,a[AM_CALENDAR_MAX_ARGS]={0},n=argc-2;
    for(int i=0;i<AM_CAL_OPERATION_COUNT;i++) if(!strcmp(argv[1],names[i]))op=i;
    if(n!=am_calendar_argument_count(op) || n<0 || n>AM_CALENDAR_MAX_ARGS) {
        fputs("invalid calendar operation or arity\n",stderr);return 2;
    }
    for(int i=0;i<n;i++) {
        char* end;errno=0;long value=strtol(argv[i+2],&end,10);
        if(errno || !argv[i+2][0] || *end || value < -16777216 || value>16777216) {
            fputs("calendar arguments must be exact integers\n",stderr);return 2;
        }
        a[i]=(int)value;
    }
    AM_CalendarReply reply;
    if(am_calendar_query(op,a,n,&reply)) {
        fputs("calendar date, moment or interval outside contract\n",stderr);return 2;
    }
    fputs("{\"api\":\"janus.calendar/1\",\"value\":",stdout);
    if(reply.kind==1) printf("%d",reply.scalar);else json_value(&reply.value);
    fputs("}\n",stdout);return 0;
}
