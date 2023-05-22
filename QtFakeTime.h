#pragma once

#include <cstdint>
#include <QDateTime>

// A faking library for Qt framework based application unit testing that shims libQt5Core.so library to allow faking of current date/time
// and accelerated passing of time (with QTimer events generated along the way).
//
//
//
// Classes/methods it works with:
//
//  - QDateTime::currentDateTime/currentMSecsSinceEpoch
//  - QTime::currentTime()
//  - QElapsedTimer
//  - QTimer
//
// Classes it doesn't yet support (either too hard or I didn't need them)
//
//  - QBasicTimer
//  - QDeadlineTimer
//  - Thread & Async wait/sleep functions
//  - QObject timer functions

namespace QtFakeTime
{

// Fake "current" time to arbitrary point in past/future.  Note that currently active QElapsedTimer & QTimer instances will be invalidated
// or reset if the new time is wildly different from their current time, but will remain as-is on a relatively small jump.
// Note that even once faked, current time will continue incrementing in lockstep with "real" time rather than remaining frozen.
void set(const QDateTime& time);
void set(qint64 msSinceEpoch);

// Reset faked time back to real chronological time, incrementing normally
void reset(void);

// Fast-forward faked time <mS> into the future, generating QTimer::timeout() events as appropriate along the way.
void fastForward(uint64_t mS);

}
