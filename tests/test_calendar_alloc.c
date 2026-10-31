/* Every allocation boundary in a calendar result releases its partial owners. */
#include "ariannamethod.h"
#include <stdio.h>
#include <stdlib.h>
static int budget=-1,count,attempts;
static void* pointers[1024];
void* __real_malloc(size_t); void* __real_calloc(size_t,size_t);
void* __real_realloc(void*,size_t); void __real_free(void*);
static void fail(const char* message) { fprintf(stderr,"FAIL calendar allocation: %s\n",message); exit(1); }
static int refuse(void) { if(budget==0)return 1; if(budget>0)budget--; return 0; }
static void track(void* p) { if(p && budget>=0) { if(count==1024)fail("ledger full"); pointers[count++]=p; } }
static void untrack(void* p) { for(int i=0;p && i<count;i++)if(pointers[i]==p) { pointers[i]=pointers[--count];return; } }
void* __wrap_malloc(size_t n) { if(refuse())return NULL; void* p=__real_malloc(n);track(p);return p; }
void* __wrap_calloc(size_t n,size_t s) { if(refuse())return NULL;void* p=__real_calloc(n,s);track(p);return p; }
void* __wrap_realloc(void* old,size_t n) { if(refuse())return NULL;void* p=__real_realloc(old,n);if(p){untrack(old);track(p);}return p; }
void __wrap_free(void* p) { untrack(p);__real_free(p); }
int main(void) {
    for(int kind=0;kind<3;kind++) {
        int complete=0;
        for(int n=0;n<512;n++) {
            budget=n; attempts++;
            AM_Record* r=kind==0 ? am_calendar_date(0) : kind==1 ? am_calendar_subject(498,731) : am_calendar_local(19998,64800,0,64800);
            budget=-1;
            int success=r!=NULL;
            am_record_free(r);
            if(count)fail("partial record leaked");
            if(success) { complete=1;break; }
        }
        if(!complete)fail("result never completed");
    }
    printf("PASS calendar allocation: %d failure/success boundaries, zero retained allocations\n",attempts);
    return 0;
}
