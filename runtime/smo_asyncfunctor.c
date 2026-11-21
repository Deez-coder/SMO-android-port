/* smo_asyncfunctor.c - al::AsyncFunctorThread::start [main 0xa2bc4c] without its race (0.4.2).
 *
 * The game's start() sends the worker thread its message (sead::Thread::sendMessage, vtable +24, message 1, blocking) and
 * only THEN clears its done flag (+24 = false). The worker runs the functor and sets the flag (+24 = true). When the worker
 * finishes before the caller's store, the store wipes its "true" and isDone() [0xa2bc84] stays false for good. On the console
 * the worker never woke that fast; here a woken thread runs within microseconds, so a short job can win the race.
 * Seen as a loading screen that never ends (GitHub issues 31 and 34; on the S24 in 3 of 26 new-game starts, right after Start):
 * HakoniwaStateDeleteScene::exeDeleteScene [0x511a60] waits for this flag (the scene deletion on the DeleteScene thread)
 * and for WorldResourceLoader::isEndLoadWorldResource [0x5146f8], which is the same flag of the loader's own functor thread.
 * Every thread idle, the main thread only pacing frames, the state stuck in HakoniwaStateDeleteSceneNrvDeleteScene.
 * Here the flag is cleared before the message goes, so the worker's "true" is the last word. libsmo.so is linked with
 * -Wl,--wrap=f_4000a2bc4c (tools/android/build_runtime.py); the function's callers are all in other objects. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "plat.h"
#include "runtime.h"

void __real_f_4000a2bc4c(Ctx *c);

void __wrap_f_4000a2bc4c(Ctx *c) {                   /* (AsyncFunctorThread *this) */
    uint64_t self = c->x[0];
    uint64_t thread = rd64(self + 8);                 /* its sead::DelegateThread */
    uint64_t send = rd64(rd64(thread) + 24);          /* sead::Thread::sendMessage(msg, block) */
    static int test = -1;                             /* TEST ONLY, SMO_ASYNC_TEST: 1 = the game's order with a 20 ms gap (the race made likely), 2 = this order with the same gap */
    if (test < 0) { const char *e = getenv("SMO_ASYNC_TEST"); test = e ? atoi(e) : 0; if (test) printf("smo_asyncfunctor: TEST %d: %s, 20 ms between the message and the flag\n", test, test == 1 ? "the game's own order" : "the fixed order"); }
    if (test == 1) {
        c->x[0] = thread; c->x[1] = 1; c->x[2] = 1;
        call_indirect(c, send);
        plat_sleep_ms(20);
        wr8(self + 24, 0);
        return;
    }
    wr8(self + 24, 0);                                /* not done: BEFORE the worker can run */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    c->x[0] = thread; c->x[1] = 1; c->x[2] = 1;
    call_indirect(c, send);
    if (test == 2) plat_sleep_ms(20);
    {   static volatile int32_t races, starts;        /* the worker done before start() returned: the game's own order would have lost it */
        int n = __atomic_add_fetch(&starts, 1, __ATOMIC_RELAXED);
        if (rd8(self + 24)) {
            int r = __atomic_add_fetch(&races, 1, __ATOMIC_RELAXED);
            if (r <= 20) { printf("smo_asyncfunctor: start %d: the worker (functor thread %llx) finished before start() returned (%d times so far): the game's order would have left it \"not done\" for good, a loading screen that never ends\n", n, (unsigned long long)self, r); fflush(stdout); }
        }
    }
    {   static int said;
        if (!said) { said = 1; printf("smo_asyncfunctor: al::AsyncFunctorThread::start clears its done flag before the worker is woken (the game's order could lose the worker's \"done\")\n"); fflush(stdout); }
    }
}
