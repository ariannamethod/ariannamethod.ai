/* Compile this against full libaml and AM_CALENDAR_ONLY; both share one value API. */
#include "ariannamethod.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int checks;
#define CHECK(c) do{checks++;if(!(c)){fprintf(stderr,"calendar values:%d: %s\n",__LINE__,#c);exit(1);}}while(0)
static float get(const AM_CalendarValue* v,const char* name) {
    for(int i=0;i<v->count;i++)if(!strcmp(v->keys[i],name))return v->numbers[i];
    CHECK(0); return 0;
}
int main(void) {
    AM_CalendarValue value, saved;
    memset(&value,0xa5,sizeof(value));memcpy(&saved,&value,sizeof(value));
    CHECK(am_calendar_value_date(AM_CALENDAR_MIN_DAY-1,&value)==1);
    CHECK(!memcmp(&saved,&value,sizeof(value)));
    CHECK(am_calendar_value_subject(498,AM_CALENDAR_MAX_DAY,&value)==1);
    CHECK(!memcmp(&saved,&value,sizeof(value)));
    CHECK(am_calendar_value_local(19999,86400,0,-1,&value)==1);
    CHECK(!memcmp(&saved,&value,sizeof(value)));
    CHECK(am_calendar_value_date(0,NULL)==1);
    CHECK(am_calendar_value_subject(498,731,NULL)==1);
    CHECK(am_calendar_value_local(19999,0,0,-1,NULL)==1);
    CHECK(am_calendar_value_date(0,&value)==0 && value.count==15);
    CHECK(get(&value,"hebrew_year")==5785 && get(&value,"hebrew_day")==1);
    saved=value;
    CHECK(am_calendar_value_date(477,&value)==0);
    CHECK(get(&saved,"day")==0 && get(&value,"day")==477);
    CHECK(!strcmp(saved.texts[0],"janus.calendar/1"));
    CHECK(am_calendar_value_subject(498,731,&value)==0 && value.count==31);
    CHECK(get(&value,"birthquake")==1 && get(&value,"age_days")==233);
    CHECK(am_calendar_value_local(19998,64800,0,64800,&value)==0);
    CHECK(get(&value,"hebrew_year")==5785 && get(&value,"metonic_year")==9);
    printf("PASS allocation-free calendar values: %d checks\n",checks);
    return 0;
}
