/* A simple event-driven programming library. Originally I wrote this code
 * for the Jim's event-loop (Jim is a Tcl interpreter) but later translated
 * it in form of a library for easy reuse.
 *
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "ae.h"
#include "config.h"
#include "zmalloc.h"

/* Include the best multiplexing layer supported by this system.
 * The following should be ordered by performances, descending. */
#ifdef HAVE_EVPORT
#include "ae_evport.c"
#else
#ifdef HAVE_EPOLL
#include "ae_epoll.c"
#else
#ifdef HAVE_KQUEUE
#include "ae_kqueue.c"
#else
#include "ae_select.c"
#endif
#endif
#endif

aeEventLoop*
aeCreateEventLoop(int setsize)
{
    aeEventLoop* eventLoop;
    int i;

    if ((eventLoop = zmalloc(sizeof(*eventLoop))) == NULL) goto err;
    eventLoop->events = zmalloc(sizeof(aeFileEvent) * setsize);
    eventLoop->fired = zmalloc(sizeof(aeFiredEvent) * setsize);
    if (eventLoop->events == NULL || eventLoop->fired == NULL) goto err;
    eventLoop->setsize = setsize;
    eventLoop->lastTime = time(NULL);
    eventLoop->timeEventHead = NULL;
    eventLoop->timeEventNextId = 0;
    eventLoop->timeEventSkiplist = createSkiplist();
    eventLoop->timeEventSkiplist->compare = compareTimeEvent;
    eventLoop->stop = 0;
    eventLoop->maxfd = -1;
    eventLoop->beforesleep = NULL;
    if (aeApiCreate(eventLoop) == -1) goto err;
    /* Events with mask == AE_NONE are not set. So let's initialize the
     * vector with it. */
    for (i = 0; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;
    return eventLoop;

err:
    if (eventLoop) {
        zfree(eventLoop->events);
        zfree(eventLoop->fired);
        zfree(eventLoop);
    }
    return NULL;
}

int
compareTimeEvent(void* a, void* b)
{
    return ((aeTimeEvent*)a)->id - ((aeTimeEvent*)b)->id;
}
/* Return the current set size. */
int
aeGetSetSize(aeEventLoop* eventLoop)
{
    return eventLoop->setsize;
}

/* Resize the maximum set size of the event loop.
 * If the requested set size is smaller than the current set size, but
 * there is already a file descriptor in use that is >= the requested
 * set size minus one, AE_ERR is returned and the operation is not
 * performed at all.
 *
 * Otherwise AE_OK is returned and the operation is successful. */
int
aeResizeSetSize(aeEventLoop* eventLoop, int setsize)
{
    int i;

    if (setsize == eventLoop->setsize) return AE_OK;
    if (eventLoop->maxfd >= setsize) return AE_ERR;
    if (aeApiResize(eventLoop, setsize) == -1) return AE_ERR;

    eventLoop->events =
      zrealloc(eventLoop->events, sizeof(aeFileEvent) * setsize);
    eventLoop->fired =
      zrealloc(eventLoop->fired, sizeof(aeFiredEvent) * setsize);
    eventLoop->setsize = setsize;

    /* Make sure that if we created new slots, they are initialized with
     * an AE_NONE mask. */
    for (i = eventLoop->maxfd + 1; i < setsize; i++)
        eventLoop->events[i].mask = AE_NONE;
    return AE_OK;
}

void
aeDeleteEventLoop(aeEventLoop* eventLoop)
{
    aeApiFree(eventLoop);
    zfree(eventLoop->events);
    zfree(eventLoop->fired);
    zfree(eventLoop);
}

void
aeStop(aeEventLoop* eventLoop)
{
    eventLoop->stop = 1;
}

int
aeCreateFileEvent(aeEventLoop* eventLoop, int fd, int mask, aeFileProc* proc,
                  void* clientData)
{
    if (fd >= eventLoop->setsize) {
        errno = ERANGE;
        return AE_ERR;
    }
    aeFileEvent* fe = &eventLoop->events[fd];

    if (aeApiAddEvent(eventLoop, fd, mask) == -1) return AE_ERR;
    fe->mask |= mask;
    if (mask & AE_READABLE) fe->rfileProc = proc;
    if (mask & AE_WRITABLE) fe->wfileProc = proc;
    fe->clientData = clientData;
    if (fd > eventLoop->maxfd) eventLoop->maxfd = fd;
    return AE_OK;
}

void
aeDeleteFileEvent(aeEventLoop* eventLoop, int fd, int mask)
{
    if (fd >= eventLoop->setsize) return;
    aeFileEvent* fe = &eventLoop->events[fd];
    if (fe->mask == AE_NONE) return;

    aeApiDelEvent(eventLoop, fd, mask);
    fe->mask = fe->mask & (~mask);
    if (fd == eventLoop->maxfd && fe->mask == AE_NONE) {
        /* Update the max fd */
        int j;

        for (j = eventLoop->maxfd - 1; j >= 0; j--)
            if (eventLoop->events[j].mask != AE_NONE) break;
        eventLoop->maxfd = j;
    }
}

int
aeGetFileEvents(aeEventLoop* eventLoop, int fd)
{
    if (fd >= eventLoop->setsize) return 0;
    aeFileEvent* fe = &eventLoop->events[fd];

    return fe->mask;
}

static void
aeGetTime(long* seconds, long* milliseconds)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    *seconds = tv.tv_sec;
    *milliseconds = tv.tv_usec / 1000;
}

static void
aeAddMillisecondsToNow(long long milliseconds, long* sec, long* ms)
{
    long cur_sec, cur_ms, when_sec, when_ms;

    aeGetTime(&cur_sec, &cur_ms);
    when_sec = cur_sec + milliseconds / 1000;
    when_ms = cur_ms + milliseconds % 1000;
    if (when_ms >= 1000) {
        when_sec++;
        when_ms -= 1000;
    }
    *sec = when_sec;
    *ms = when_ms;
}

long long
aeCreateTimeEvent(aeEventLoop* eventLoop, long long unixseconds,
                  long long milliseconds, aeTimeProc* proc, void* clientData,
                  aeEventFinalizerProc* finalizerProc)
{
    long long id = eventLoop->timeEventNextId++;
    aeTimeEvent* te;

    te = zmalloc(sizeof(*te));
    if (te == NULL) return AE_ERR;
    te->id = id;
    aeAddMillisecondsToNow(milliseconds, &te->when_sec, &te->when_ms);
    te->timeProc = proc;
    te->finalizerProc = finalizerProc;
    te->clientData = clientData;
    te->next = NULL;
    skiplistInsert(eventLoop->timeEventSkiplist, unixseconds, (void*)te, id);
    return id;
}

int
aeDeleteTimeEvent(aeEventLoop* eventLoop, long long score, long long id)
{
    return skiplistDelete(eventLoop->timeEventSkiplist, score, id);
}

/* Search the first timer to fire.
 * This operation is useful to know how many time the select can be
 * put in sleep without to delay any event.
 * If there are no timers NULL is returned.
 *
 * Note that's O(N) since time events are unsorted.
 * Possible optimizations (not needed by Redis so far, but...):
 * 1) Insert the event in order, so that the nearest is just the head.
 *    Much better but still insertion or deletion of timers is O(N).
 * 2) Use a skiplist to have this operation as O(1) and insertion as O(log(N)).
 */
static aeTimeEvent*
aeSearchNearestTimer(aeEventLoop* eventLoop)
{
    if (!eventLoop->timeEventSkiplist->header->level[0].forward) return NULL;
    return (aeTimeEvent*)eventLoop->timeEventSkiplist->header->level[0]
      .forward->obj;
}

/* Process time events */
static int
processTimeEvents(aeEventLoop* eventLoop)
{
    int processed = 0;
    aeTimeEvent* te;

    long now_sec, now_ms;
    long long id;
    aeGetTime(&now_sec, &now_ms);

    skiplistNode* x = eventLoop->timeEventSkiplist->header;
    if (x->level[0].forward == NULL) return processed;
    x = x->level[0].forward;
    te = (aeTimeEvent*)x->obj;

    while (te && (now_sec > te->when_sec ||
                  (now_sec == te->when_sec && now_ms >= te->when_ms))) {
        int retval;
        id = te->id;
        retval = te->timeProc(eventLoop, id, te->clientData);
        processed++;
        if (retval != AE_NOMORE) {
            aeAddMillisecondsToNow(retval, &te->when_sec, &te->when_ms);
        } else {
            aeDeleteTimeEvent(eventLoop, x->score, x->id);
        }
        x = x->level[0].forward;
        te = x ? (aeTimeEvent*)x->obj : NULL;
    }
    return processed;
}

/* Process every pending time event, then every pending file event
 * (that may be registered by time event callbacks just processed).
 * Without special flags the function sleeps until some file event
 * fires, or when the next time event occurs (if any).
 *
 * If flags is 0, the function does nothing and returns.
 * if flags has AE_ALL_EVENTS set, all the kind of events are processed.
 * if flags has AE_FILE_EVENTS set, file events are processed.
 * if flags has AE_TIME_EVENTS set, time events are processed.
 * if flags has AE_DONT_WAIT set the function returns ASAP until all
 * the events that's possible to process without to wait are processed.
 *
 * The function returns the number of events processed. */
int
aeProcessEvents(aeEventLoop* eventLoop, int flags)
{
    int processed = 0, numevents;

    /* Nothing to do? return ASAP */
    if (!(flags & AE_TIME_EVENTS) && !(flags & AE_FILE_EVENTS)) return 0;

    /* Note that we want call select() even if there are no
     * file events to process as long as we want to process time
     * events, in order to sleep until the next time event is ready
     * to fire. */
    if (eventLoop->maxfd != -1 ||
        ((flags & AE_TIME_EVENTS) && !(flags & AE_DONT_WAIT))) {
        int j;
        aeTimeEvent* shortest = NULL;
        struct timeval tv, *tvp;

        if (flags & AE_TIME_EVENTS && !(flags & AE_DONT_WAIT))
            shortest = aeSearchNearestTimer(eventLoop);
        if (shortest) {
            long now_sec, now_ms;

            /* Calculate the time missing for the nearest
             * timer to fire. */
            aeGetTime(&now_sec, &now_ms);
            tvp = &tv;
            tvp->tv_sec = shortest->when_sec - now_sec;
            if (shortest->when_ms < now_ms) {
                tvp->tv_usec = ((shortest->when_ms + 1000) - now_ms) * 1000;
                tvp->tv_sec--;
            } else {
                tvp->tv_usec = (shortest->when_ms - now_ms) * 1000;
            }
            if (tvp->tv_sec < 0) tvp->tv_sec = 0;
            if (tvp->tv_usec < 0) tvp->tv_usec = 0;
        } else {
            /* If we have to check for events but need to return
             * ASAP because of AE_DONT_WAIT we need to set the timeout
             * to zero */
            if (flags & AE_DONT_WAIT) {
                tv.tv_sec = tv.tv_usec = 0;
                tvp = &tv;
            } else {
                /* Otherwise we can block */
                tvp = NULL; /* wait forever */
            }
        }

        numevents = aeApiPoll(eventLoop, tvp);
        for (j = 0; j < numevents; j++) {
            aeFileEvent* fe = &eventLoop->events[eventLoop->fired[j].fd];
            int mask = eventLoop->fired[j].mask;
            int fd = eventLoop->fired[j].fd;
            int rfired = 0;

            /* note the fe->mask & mask & ... code: maybe an already processed
             * event removed an element that fired and we still didn't
             * processed, so we check if the event is still valid. */
            if (fe->mask & mask & AE_READABLE) {
                rfired = 1;
                fe->rfileProc(eventLoop, fd, fe->clientData, mask);
            }
            if (fe->mask & mask & AE_WRITABLE) {
                if (!rfired || fe->wfileProc != fe->rfileProc)
                    fe->wfileProc(eventLoop, fd, fe->clientData, mask);
            }
            processed++;
        }
    }
    /* Check time events */
    if (flags & AE_TIME_EVENTS) processed += processTimeEvents(eventLoop);

    return processed; /* return the number of processed file/time events */
}

/* Wait for milliseconds until the given file descriptor becomes
 * writable/readable/exception */
int
aeWait(int fd, int mask, long long milliseconds)
{
    struct pollfd pfd;
    int retmask = 0, retval;

    memset(&pfd, 0, sizeof(pfd));
    pfd.fd = fd;
    if (mask & AE_READABLE) pfd.events |= POLLIN;
    if (mask & AE_WRITABLE) pfd.events |= POLLOUT;

    if ((retval = poll(&pfd, 1, milliseconds)) == 1) {
        if (pfd.revents & POLLIN) retmask |= AE_READABLE;
        if (pfd.revents & POLLOUT) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLERR) retmask |= AE_WRITABLE;
        if (pfd.revents & POLLHUP) retmask |= AE_WRITABLE;
        return retmask;
    } else {
        return retval;
    }
}

void
aeMain(aeEventLoop* eventLoop)
{
    eventLoop->stop = 0;
    while (!eventLoop->stop) {
        if (eventLoop->beforesleep != NULL) eventLoop->beforesleep(eventLoop);
        aeProcessEvents(eventLoop, AE_ALL_EVENTS);
    }
}

char*
aeGetApiName(void)
{
    return aeApiName();
}

void
aeSetBeforeSleepProc(aeEventLoop* eventLoop, aeBeforeSleepProc* beforesleep)
{
    eventLoop->beforesleep = beforesleep;
}
