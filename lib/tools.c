/*
 * Copyright © 2024 chargebyte GmbH
 * SPDX-License-Identifier: Apache-2.0
 */
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "tools.h"

#define NSEC_PER_SEC 1000000000L

bool timespec_is_set(const struct timespec *ts)
{
    return ts->tv_sec || ts->tv_nsec;
}

void set_normalized_timespec(struct timespec *ts, time_t sec, int64_t nsec)
{
    while (nsec >= NSEC_PER_SEC) {
        nsec -= NSEC_PER_SEC;
        ++sec;
    }
    while (nsec < 0) {
        nsec += NSEC_PER_SEC;
        --sec;
    }
    ts->tv_sec = sec;
    ts->tv_nsec = nsec;
}

struct timespec timespec_add(struct timespec lhs, struct timespec rhs)
{
    struct timespec ts_delta;

    set_normalized_timespec(&ts_delta, lhs.tv_sec + rhs.tv_sec, lhs.tv_nsec + rhs.tv_nsec);

    return ts_delta;
}

struct timespec timespec_sub(struct timespec lhs, struct timespec rhs)
{
    struct timespec ts_delta;

    set_normalized_timespec(&ts_delta, lhs.tv_sec - rhs.tv_sec, lhs.tv_nsec - rhs.tv_nsec);

    return ts_delta;
}

void timespec_add_ms(struct timespec *ts, long long msec)
{
    long long sec = msec / 1000;

    set_normalized_timespec(ts, ts->tv_sec + sec, ts->tv_nsec + (msec - sec * 1000) * 1000 * 1000);
}

/*
 * lhs < rhs:  return < 0
 * lhs == rhs: return 0
 * lhs > rhs:  return > 0
 */
int timespec_compare(const struct timespec *lhs, const struct timespec *rhs)
{
    if (lhs->tv_sec < rhs->tv_sec)
        return -1;
    if (lhs->tv_sec > rhs->tv_sec)
        return 1;
    return lhs->tv_nsec - rhs->tv_nsec;
}

long long timespec_to_ms(struct timespec ts)
{
    return ((long long)ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

long long timespec_to_us(struct timespec ts)
{
    return ((long long)ts.tv_sec * 1000000) + (ts.tv_nsec / 1000);
}

int msleep(int ms)
{
    struct timespec req, rem;
    int rv;

    rem.tv_sec = ms / 1000;
    rem.tv_nsec = (ms % 1000) * (1000 * 1000); /* x ms */

    do {
        req = rem;
        rv = nanosleep(&req, &rem);
    } while (rv == -1 && errno == EINTR);

    return rv;
}

int compare_version(const char *val, const char *ref)
{
    for (;;) {
        const char *v1, *r1;
        int len1 = 0, len2 = 0;

        while (*val == '.')
            val++;
        while (*ref == '.')
            ref++;

        while (*val == '0')
            val++;
        while (*ref == '0')
            ref++;

        v1 = val;
        r1 = ref;

        while (isdigit((unsigned char)*val)) {
            len1++;
            val++;
        }

        while (isdigit((unsigned char)*ref)) {
            len2++;
            ref++;
        }

        if (len1 < len2)
            return -1;
        if (len1 > len2)
            return 1;

        for (int i = 0; i < len1; i++) {
            if (v1[i] < r1[i])
                return -1;
            if (v1[i] > r1[i])
                return 1;
        }

        if (!*val && !*ref)
            return 0;

        if (*val == '.')
            val++;
        else if (*val)
            return 1;

        if (*ref == '.')
            ref++;
        else if (*ref)
            return -1;
    }
}
