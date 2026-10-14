/* Linker-only clock fixture; the production runtime has no clock override. */
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>

static _Atomic unsigned utc_test_time_calls;

time_t __wrap_time(time_t* destination) {
    atomic_fetch_add(&utc_test_time_calls, 1);
    const char* seconds = getenv("AML_TEST_UTC_SECONDS");
    time_t result = (time_t)(seconds ? strtoll(seconds, NULL, 10) : 1709164800LL);
    if (destination) *destination = result;
    return result;
}
