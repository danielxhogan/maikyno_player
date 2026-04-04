#include "clock.h"
#include <libavutil/time.h>
#include <math.h>

#define AV_NOSYNC_THRESHOLD 10.0

void init_clock(Clock *clock, int *q_serial)
{
    clock->speed = 1.0;
    clock->paused = 0;
    clock->q_serial = q_serial;
    set_clock(clock, NAN, -1);
}

void set_clock(Clock *clock, double pts, int serial)
{
    double time = av_gettime_relative() / 1000000.0;
    set_clock_at(clock, pts, serial, time);
}

void set_clock_at(Clock *c, double pts, int serial, double time)
{
    c->pts = pts;
    c->last_updated = time;
    c->pts_drift = c->pts - time;
    c->serial = serial;
}

static double get_clock(Clock *c)
{
    if (*c->q_serial != c->serial)
        return NAN;
    if (c->paused) {
        return c->pts;
    } else {
        double time = av_gettime_relative() / 1000000.0;
        return c->pts_drift + time - (time - c->last_updated) * (1.0 - c->speed);
    }
}

void sync_clock_to_slave(Clock *c, Clock *slave)
{
    double clock = get_clock(c);
    double slave_clock = get_clock(slave);
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
        set_clock(c, slave_clock, slave->serial);
}
