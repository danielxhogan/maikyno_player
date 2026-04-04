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

void set_clock_at(Clock *clock, double pts, int serial, double time)
{
    clock->pts = pts;
    clock->last_updated = time;
    clock->pts_drift = clock->pts - time;
    clock->serial = serial;
}

static double get_clock(Clock *clock)
{
    if (*clock->q_serial != clock->serial)
        return NAN;
    if (clock->paused) {
        return clock->pts;
    } else {
        double time = av_gettime_relative() / 1000000.0;
        return clock->pts_drift + time - (time - clock->last_updated) * (1.0 - clock->speed);
    }
}

void sync_clock_to_slave(Clock *master, Clock *slave)
{
    double clock = get_clock(master);
    double slave_clock = get_clock(slave);
    if (!isnan(slave_clock) && (isnan(clock) || fabs(clock - slave_clock) > AV_NOSYNC_THRESHOLD))
        set_clock(master, slave_clock, slave->serial);
}
