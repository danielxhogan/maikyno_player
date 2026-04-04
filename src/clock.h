#pragma once

typedef struct Clock {
    double pts;           /* clock base */
    double pts_drift;     /* clock base minus time at which we updated the clock */
    double last_updated;
    double speed;
    int serial;           /* clock is based on a packet with this serial */
    int paused;
    int *q_serial;    /* pointer to the current packet queue serial, used for obsolete clock detection */
} Clock;

void init_clock(Clock *clock, int *queue_serial);
void set_clock(Clock *c, double pts, int serial);
void set_clock_at(Clock *c, double pts, int serial, double time);
void sync_clock_to_slave(Clock *c, Clock *slave);
