#include "QtFakeTime.h"

// TODO: Extensive comments


#include <QDebug>
#include <QtGlobal>
#include <QProcessEnvironment>

#include <QDateTime>
#include <QElapsedTimer>
#include <QTimer>
#include <QCoreApplication>
#include <QThread>
#include <QMutex>

#include <dlfcn.h>

#include <map>
#include <algorithm>
#include <memory>
#include <cassert>

#ifndef __linux__
    #error "Library is dependant on LD_PRELOAD linker support in order to shim libQt5Core function implementations (a linux specific feature)"
#endif


using namespace QtFakeTime;

//------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------
// Function pointers indicating the original versions of the libQt5Core.so functions we are shimming.  Only populated
// where we actually need to access the underlying real behaviour from libQt5Core.

//------------------------------------------------------------------------------------------------------------------------
// QDateTime methods
static QDateTime (* pQt5Core_QDateTime_currentDateTime)(void) = nullptr;
static QDateTime (* pQt5Core_QDateTime_currentDateTimeUtc)(void) = nullptr;
static qint64 (* pQt5Core_QDateTime_currentMSecsSinceEpoch)(void) = nullptr;
static qint64 (* pQt5Core_QDateTime_currentSecsSinceEpoch)(void) = nullptr;

//------------------------------------------------------------------------------------------------------------------------
// QTimer methods

static void (* pQt5Core_QTimer_setInterval)(QTimer*, int) = nullptr;
static void (* pQt5Core_QTimer_start)(QTimer*) = nullptr;
static void (* pQt5Core_QTimer_singleShotImpl)(int, Qt::TimerType, const QObject*, QtPrivate::QSlotObjectBase *) = nullptr;

//------------------------------------------------------------------------------------------------------------------------

static qint64 fakedMSSinceEpoch = -1;

// These are declared external to setupIdleTimer() function so they can be reset from sanitiseTimers();
static qint64 fakedTimeAtLastIdleTimerTick   = -1;
static qint64 realTimeAtLastIdleTimerTick    = -1;

// Map associating active QElapsedTimers and their start times
//
// NOTE: QElapsedTimer doesn't have a destructor we can shim in order to clean up <qElapsedTimerStartTimes>, so the map is
// potentially going to fill up with stale pointers.  This infers that it is NOT safe to reference a QElapsedTimer instance
// from the pointer reference stored in the map.
static std::map<QElapsedTimer*, qint64> qElapsedTimerStartTimes;

// Map associating active QTimers and their end times, with entries cleaned up at point timer stops or is destroyed.
static std::map<QTimer*, qint64> qTimerDueTimes;

//------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------
// Shim functions faking selected Qt5Core library functionality.

//-----------------------------------------------------------------------------------------------------------------------
// QDateTime method shims

inline static QDateTime QDateTime_currentDateTime_shim(void)
{
    if (fakedMSSinceEpoch == -1)
    {
        // Return real value from underlying Qt Core library
        assert(pQt5Core_QDateTime_currentDateTime != nullptr);
        return pQt5Core_QDateTime_currentDateTime();
    }
    else
    {
        return QDateTime::fromMSecsSinceEpoch(fakedMSSinceEpoch);
    }
}

inline static QDateTime QDateTime_currentDateTimeUtc_shim(void)
{
    if (fakedMSSinceEpoch == -1)
    {
        // Return real value from underlying Qt Core library
        assert(pQt5Core_QDateTime_currentDateTimeUtc != nullptr);
        return pQt5Core_QDateTime_currentDateTimeUtc();
    }
    else
    {
        return QDateTime::fromMSecsSinceEpoch(fakedMSSinceEpoch, Qt::UTC);
    }
}

inline static qint64 QDateTime_currentMSecsSinceEpoch_shim(void)
{
    if (fakedMSSinceEpoch == -1)
    {
        // Return real value from underlying Qt Core library
        assert(pQt5Core_QDateTime_currentMSecsSinceEpoch != nullptr);
        return pQt5Core_QDateTime_currentMSecsSinceEpoch();
    }
    else
    {
        return fakedMSSinceEpoch;
    }
}

inline static qint64 QDateTime_currentSecsSinceEpoch_shim(void)
{
    if (fakedMSSinceEpoch == -1)
    {
        // Return real value from underlying Qt Core library
        assert(pQt5Core_QDateTime_currentSecsSinceEpoch != nullptr);
        return pQt5Core_QDateTime_currentSecsSinceEpoch();
    }
    else
    {
        return fakedMSSinceEpoch / 1000;
    }
}

//------------------------------------------------------------------------------------------------------------------------
// QTime method shims

inline static QTime QTime_currentTime_shim(void)
{
    // Unconditionally return QTime component of possibily faked QDateTime::currentDateTime()
    return QDateTime::currentDateTime().time();
}

//------------------------------------------------------------------------------------------------------------------------
// QElapsedTimer method shims.

inline static bool QElapsedTimer_isValid_shim(QElapsedTimer* timer)
{
    return qElapsedTimerStartTimes.find(timer) != qElapsedTimerStartTimes.end();
}

inline static qint64 QElapsedTimer_elapsed_shim(QElapsedTimer* timer)
{
    // Qt documentation for "real" QElapsedTimer describes unspecified behaviour when calling elapsed(), hasExpired() etc
    // on invalid timer.

    assert(qElapsedTimerStartTimes.find(timer) != qElapsedTimerStartTimes.end());

    return QDateTime::currentMSecsSinceEpoch() - qElapsedTimerStartTimes.at(timer);
}

inline static bool _ZNK13QElapsedTimer_hasExpired_shim(QElapsedTimer* timer, qint64 timeout)
{
    return timer->elapsed() >= timeout;
}

inline static void QElapsedTimer_invalidate_shim(QElapsedTimer* timer)
{
    qElapsedTimerStartTimes.erase(timer);
}

inline static void QElapsedTimer_start_shim(QElapsedTimer* timer)
{
    qElapsedTimerStartTimes[timer] = QDateTime::currentMSecsSinceEpoch();
}

inline static qint64 QElapsedTimer_restart_shim(QElapsedTimer* timer)
{
    qint64 elapsed = timer->elapsed();

    timer->start();

    return elapsed;
}

inline static qint64 QElapsedTimer_secsTo_shim(QElapsedTimer* timer, const QElapsedTimer&)
{
    // Not currently supported
    assert(false);
    return -1;
}

inline static qint64 QElapsedTimer_msecsSinceReference_shim(QElapsedTimer* timer)
{
    // Not currently supported
    assert(false);
    return -1;
}

inline static qint64 QElapsedTimer_nsecsElapsed_shim(QElapsedTimer* timer)
{
    // Not currently supported
    assert(false);
    return -1;
}

inline static qint64 QElapsedTimer_msecsTo_shim(QElapsedTimer* timer, const QElapsedTimer&)
{
    // Not currently supported
    assert(false);
    return -1;
}

//------------------------------------------------------------------------------------------------------------------------
// QTimer method shims.

class QTimerIdAccessor: public QObject
{
public:
    int id;
};

static constexpr int inactiveTimerID    = -1;
static constexpr int fakeActiveTimerID  = 0;    // Value considered active by QTimer, but not by QObject::killTimer();


inline static void QTimer_start_shim(QTimer* timer)
{
    if (timer->isSingleShot() && (timer->interval() == 0))
    {
        // In case of zero interval single-shot timer, invoke real Qt5Base QTimer::singleShot() implementation, which performs deferred
        // invocation of <slotObj> via queued signal-slot connection.

        // NOTE: This overload of QTimer::singleShot(), taking a functor argument, hasn't been shimmed
        QTimer::singleShot(0, [=](){emit timer->timeout({});});

        return;
    }

    qTimerDueTimes[timer] = QDateTime::currentMSecsSinceEpoch() + timer->interval();

    reinterpret_cast<QTimerIdAccessor*>(timer)->id = fakeActiveTimerID;

    // Have to use event handler connected to QObject::destroyed() signal to remove timer from <qTimerDueTimes> on destruction, rather than shimming QTimer::~QTimer()
    // destructors (_ZN6QTimerD0Ev etc.), as the shim destructors is not invoked in the case of dynamically allocated timers, (they instead invoke their
    // real virtual destructor via their virtual method table.)
    QObject::connect(timer, &QObject::destroyed, [](QObject* obj)   {qTimerDueTimes.erase((QTimer*)obj);});
}

inline static void QTimer_start_shim(QTimer* timer, int interval)
{
    reinterpret_cast<QTimerIdAccessor*>(timer)->id = inactiveTimerID;

    assert(pQt5Core_QTimer_setInterval != nullptr);
    pQt5Core_QTimer_setInterval(timer, interval);

    timer->start();
}

inline static void QTimer_stop_shim(QTimer* timer)
{
    reinterpret_cast<QTimerIdAccessor*>(timer)->id = inactiveTimerID;

    qTimerDueTimes.erase(timer);
}

inline static void QTimer_setInterval_shim(QTimer* timer, int interval)
{
    bool wasActive = timer->isActive();

    reinterpret_cast<QTimerIdAccessor*>(timer)->id = inactiveTimerID;

    assert(pQt5Core_QTimer_setInterval != nullptr);
    pQt5Core_QTimer_setInterval(timer, interval);

    if (wasActive)
    {
        timer->start();
    }
}

inline static int QTimer_remainingTime_shim(QTimer* timer)
{
    auto ii = qTimerDueTimes.find(timer);

    if (ii != qTimerDueTimes.end())
    {
        return ii->second  - QDateTime::currentMSecsSinceEpoch();
    }
    else
    {
        return -1;
    }
}

inline static void QTimer_singleShotImpl_shim(  int msec,
                                                Qt::TimerType timerType,
                                                const QObject *receiver,
                                                QtPrivate::QSlotObjectBase *slotObj)
{
    if (msec == 0)
    {
        // In case of zero interval single-shot timer, invoke real Qt5Base QTimer::singelShot() implementation, which performs deferred
        // invocation of <slotObj> via queued signal-slot connection.
        assert(pQt5Core_QTimer_singleShotImpl != nullptr);
        pQt5Core_QTimer_singleShotImpl(0, timerType, receiver, slotObj);
        return;
    }


    // NOTE: Function expected to take ownership of <slotObj>, but dispose of it via. destroyIfLastRef() call rather than delete

    QTimer* pTimer = new QTimer();

    pTimer->setInterval(msec);
    pTimer->setSingleShot(true);

    QObject::connect(pTimer, &QTimer::timeout, [=](void){
                                                            void *empty_argv[] = { nullptr };

                                                            slotObj->call(const_cast<QObject*>(receiver), empty_argv);

                                                            slotObj->destroyIfLastRef();

                                                            qTimerDueTimes.erase(pTimer);

                                                            reinterpret_cast<QTimerIdAccessor*>(pTimer)->id = inactiveTimerID;

                                                            delete pTimer;
                                                        });

    pTimer->start();
}

//------------------------------------------------------------------------------------------------------------------------
// Declaration of compiler specific shim function wrappers with names matching the C++ name-mangled symbols corresponding
// to methods exported by libQt5Core.so, overriding libQt5Core originals due to LD_PRELOAD.  Functions are declared with "C"
// external linkage in order to prevent further mangling.
//
// Mangled names discovered by examining "objdump -t .../libQt5Core.so" output

#ifdef __GNUC__

// Overrides of C++ name-mangled identifiers generated by GCC compiling Qt5 (verified for Qt 5.9 & 5.15)

extern "C" QDateTime _ZN9QDateTime15currentDateTimeEv(void)
{
    return QDateTime_currentDateTime_shim();
}

extern "C" QDateTime _ZN9QDateTime18currentDateTimeUtcEv(void)
{
    return QDateTime_currentDateTimeUtc_shim();
}

extern "C" qint64 _ZN9QDateTime22currentMSecsSinceEpochEv(void)
{
    return QDateTime_currentMSecsSinceEpoch_shim();
}

extern "C" qint64 _ZN9QDateTime21currentSecsSinceEpochEv(void)
{
    return QDateTime_currentSecsSinceEpoch_shim();

}

extern "C" QTime _ZN5QTime11currentTimeEv(void)
{
    return QTime_currentTime_shim();
}

extern "C" bool _ZNK13QElapsedTimer7isValidEv(QElapsedTimer* timer)
{
    return QElapsedTimer_isValid_shim(timer);
}

extern "C" qint64 _ZNK13QElapsedTimer7elapsedEv(QElapsedTimer* timer)
{
    return QElapsedTimer_elapsed_shim(timer);
}

extern "C" bool _ZNK13QElapsedTimer10hasExpiredEx(QElapsedTimer* timer, qint64 timeout)
{
    return _ZNK13QElapsedTimer_hasExpired_shim(timer, timeout);
}

extern "C" void _ZN13QElapsedTimer10invalidateEv(QElapsedTimer* timer)
{
    return QElapsedTimer_invalidate_shim(timer);
}

extern "C" void _ZN13QElapsedTimer5startEv(QElapsedTimer* timer)
{
    return QElapsedTimer_start_shim(timer);
}

extern "C" qint64 _ZN13QElapsedTimer7restartEv(QElapsedTimer* timer)
{
    return QElapsedTimer_restart_shim(timer);
}

extern "C" qint64 _ZNK13QElapsedTimer6secsToERKS_(QElapsedTimer* timer, const QElapsedTimer& other)
{
    return QElapsedTimer_secsTo_shim(timer, other);
}

extern "C" qint64 _ZNK13QElapsedTimer19msecsSinceReferenceEv(QElapsedTimer* timer)
{
    return QElapsedTimer_msecsSinceReference_shim(timer);

}

extern "C" qint64 _ZNK13QElapsedTimer12nsecsElapsedEv(QElapsedTimer* timer)
{
    return QElapsedTimer_nsecsElapsed_shim(timer);
}

extern "C" qint64 _ZNK13QElapsedTimer7msecsToERKS_(QElapsedTimer* timer, const QElapsedTimer& other)
{
    return QElapsedTimer_msecsTo_shim(timer, other);
}

extern "C" void _ZN6QTimer5startEv(QTimer* timer)
{
    return QTimer_start_shim(timer);
}

extern "C" void _ZN6QTimer5startEi(QTimer* timer, int interval)
{
    return QTimer_start_shim(timer, interval);
}

extern "C" void _ZN6QTimer4stopEv(QTimer* timer)
{
    return QTimer_stop_shim(timer);
}

extern "C" void _ZN6QTimer11setIntervalEi(QTimer* timer, int interval)
{
    return QTimer_setInterval_shim(timer, interval);
}

extern "C" int _ZNK6QTimer13remainingTimeEv(QTimer* timer)
{
    return QTimer_remainingTime_shim(timer);
}

extern "C" void _ZN6QTimer14singleShotImplEiN2Qt9TimerTypeEPK7QObjectPN9QtPrivate15QSlotObjectBaseE(int msec,
                                                                                                    Qt::TimerType timerType,
                                                                                                    const QObject *receiver,
                                                                                                    QtPrivate::QSlotObjectBase *slotObj)
{
    return QTimer_singleShotImpl_shim(msec, timerType, receiver, slotObj);
}

#else
    #error "Unsupported compiler"
#endif

//------------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------------
static void* h_libQt5Core = nullptr;

void __attribute__((constructor)) initialize(void)
{
    // Called at shared library load time

    // Check that libQtFakeTime.so has been specified via. LD_PRELOAD environment variable - necessary for shimming of
    // libQt5Core.so functions to work
    QString LD_PRELOAD = QProcessEnvironment::systemEnvironment().value("LD_PRELOAD");

    if (!LD_PRELOAD.contains("libQtFakeTime.so"))
    {
        qFatal("libQtFakeTime needs to be pre-loaded via LD_PRELOAD env. variable in order to intercept Qt Core library calls");
    }

    h_libQt5Core = dlopen("libQt5Core.so", RTLD_LAZY);

    // Query libQt5Core library for pointers to original versions of functions that we are shimming

#ifdef __GNUC__

    // QDateTime methods

    *(void **) (&pQt5Core_QDateTime_currentDateTime) = dlsym(h_libQt5Core, "_ZN9QDateTime15currentDateTimeEv");

    if (pQt5Core_QDateTime_currentDateTime == nullptr)
    {
        qFatal("Couldn't locate symbol associated with QDateTime::currentDateTime() method in libQt5Core.so");
    }

    *(void **) (&pQt5Core_QDateTime_currentDateTimeUtc) = dlsym(h_libQt5Core, "_ZN9QDateTime18currentDateTimeUtcEv");

    if (pQt5Core_QDateTime_currentDateTimeUtc == nullptr)
    {
        qFatal("Couldn't locate symbol associated with QDateTime::currentDateTimeUtc() method in libQt5Core.so");
    }

    *(void **) (&pQt5Core_QDateTime_currentMSecsSinceEpoch) = dlsym(h_libQt5Core, "_ZN9QDateTime22currentMSecsSinceEpochEv");

    if (pQt5Core_QDateTime_currentMSecsSinceEpoch == nullptr)
    {
        qFatal("Couldn't locate symbol associated with QDateTime::currentMSecsSinceEpoch() method in libQt5Core.so");
    }

    *(void **) (&pQt5Core_QDateTime_currentSecsSinceEpoch) = dlsym(h_libQt5Core, "_ZN9QDateTime21currentSecsSinceEpochEv");

    if (pQt5Core_QDateTime_currentSecsSinceEpoch == nullptr)
    {
        qFatal("Couldn't locate symbol associated with QDateTime::currentSecsSinceEpoch() method in libQt5Core.so");
    }

    // QTimer methods

    *(void **) (&pQt5Core_QTimer_setInterval) = dlsym(h_libQt5Core, "_ZN6QTimer11setIntervalEi");

    if (pQt5Core_QTimer_setInterval == nullptr)
    {
        qFatal("Couldn't locate symbol associated with QTimer::setInterval() method in libQt5Core.so");
    }

    *(void **) (&pQt5Core_QTimer_start) = dlsym(h_libQt5Core, "_ZN6QTimer5startEv");

    if (pQt5Core_QTimer_start == nullptr)
    {
        qFatal("Couldn't locate symbol associated with QTimer::setInterval() method in libQt5Core.so");
    }

    *(void **) (&pQt5Core_QTimer_singleShotImpl) = dlsym(h_libQt5Core, "_ZN6QTimer14singleShotImplEiN2Qt9TimerTypeEPK7QObjectPN9QtPrivate15QSlotObjectBaseE");

    if (pQt5Core_QTimer_singleShotImpl == nullptr)
    {
        qFatal("Couldn't locate symbol associated with QTimer::singleShotImpl() method in libQt5Core.so");
    }
#else
    #error "Unsupported compiler"
#endif

    if (h_libQt5Core != nullptr)
    {
        dlclose(h_libQt5Core);
    }
}

static void sanitiseTimers(void);
static void generateTimeoutEventforOverdueQTimers(void);
static QTimer* nextTimerDue(void);
static void generateTimeoutEvent(QTimer& timer);

//------------------------------------------------------------------------------------------------------------------------
void QtFakeTime::set(const QDateTime& time)
{
    assert(time.isValid());

    fakedMSSinceEpoch               = time.toMSecsSinceEpoch();

    sanitiseTimers();
}

void set(qint64 msSinceEpoch)
{
    assert(msSinceEpoch > 0);

    fakedMSSinceEpoch               = msSinceEpoch;

    sanitiseTimers();
}

void QtFakeTime::reset(void)
{
    // Back to real date/time
    fakedMSSinceEpoch               = -1;

    sanitiseTimers();
}

void QtFakeTime::fastForward(uint64_t mS)
{
    if (fakedMSSinceEpoch == -1)
    {
        fakedMSSinceEpoch = QDateTime::currentMSecsSinceEpoch();
    }

    // Incrementally step faked current time to point <mS> in the future, generating QTimer::timeout() events for any active timers that timeout along the way
    qint64 endTime = fakedMSSinceEpoch + mS;

    while (true)
    {
        QTimer* pTimer = nextTimerDue();

        if (pTimer == nullptr)
        {
            // No timers active at all
            break;
        }

        qint64 timeDue = qTimerDueTimes.at(pTimer);

        if (timeDue > endTime)
        {
            // Earliest timer due point is beyond fast-forward period
            break;
        }

        // Perform intermediate increment of <fakedMSSinceEpoch> to <timeDue>
        fakedMSSinceEpoch = timeDue;

        generateTimeoutEvent(*pTimer);

        // Process any outstanding events that might have arisen from timeout (in particular any deferred signal-slot connections)
        QCoreApplication::processEvents();
    }

    // Perform final increment of faked current time
    fakedMSSinceEpoch = endTime;

    // Process any outstanding events that might have arisen from timeout (in particular any deferred signal-slot connections)
    QCoreApplication::processEvents();
}
//------------------------------------------------------------------------------------------------------------------------

static void sanitiseTimers(void)
{
    // Called on arbitrary jumps in current time due to set/reset calls, which could render state of currently active
    // timers nonsensical.

    // Reset idle timer state variables so that next idle timer event performs resynchronisation
    fakedTimeAtLastIdleTimerTick    = -1;
    realTimeAtLastIdleTimerTick     = -1;

    // Reset any QElapsedTimer whose start date is now in the future.
    //
    // CAREFUL HERE: QElapsedTimer pointers in <qElapsedTimerStartTimes> could be stale/invalid and must not be referenced....
    auto ii = qElapsedTimerStartTimes.begin();
    while (ii != qElapsedTimerStartTimes.end())
    {
        if (ii->second > QDateTime::currentMSecsSinceEpoch())
        {
            // Jump backwards in time to before start point of timer, reset timer start time to current time to avoid possibility
            // of returning nonsensical negative elapsed time
            ii->second = QDateTime::currentMSecsSinceEpoch();
        }
        ++ii;
    }

    qint64 currentTime      = QDateTime::currentMSecsSinceEpoch();

    auto jj = qTimerDueTimes.begin();
    while (jj != qTimerDueTimes.end())
    {
        QTimer& timer           = *(jj->first);
        qint64 timerDueTime     = jj->second;
        qint64 timerInterval    = timer.interval();
        qint64 timerStartTime   = timerDueTime - timerInterval;

        if (currentTime < timerStartTime)
        {
            // Jump backwards in time to before start point of timer
            if (timer.isSingleShot())
            {
                // Cancel single single-shot timer
                jj = qTimerDueTimes.erase(jj);
            }
            else
            {
                // Restart repeating timer
                timer.start();
                ++jj;
            }
        }
        else
        {
            // Jump forward in time
            if (currentTime > timerDueTime + timerInterval)
            {
                // Jumped well beyond due time - *probably* not appropriate to be firing off timer event
                if (timer.isSingleShot())
                {
                    // Cancel single single-shot timer
                    jj = qTimerDueTimes.erase(jj);
                }
                else
                {
                    // Restart repeating timer
                    timer.start();
                    ++jj;
                }
            }
            else
            {
                // Current state of timer still valid
                ++jj;
            }
        }
    }

    // Only overdue timers remaining in <qTimerDueTimes> should be those who have expired *recently*
    generateTimeoutEventforOverdueQTimers();
}

static QTimer* nextTimerDue(void)
{
    auto ii = std::min_element( qTimerDueTimes.begin(),
                                qTimerDueTimes.end(),
                                [](const std::pair<QTimer*, qint64>& a, const std::pair<QTimer*, qint64>& b)
                                {
                                    return a.second < b.second;
                                });

    if (ii != qTimerDueTimes.end())
    {
        return ii->first;
    }
    else
    {
        return nullptr;
    }
}

static void generateTimeoutEvent(QTimer& timer)
{

    qint64 timeDue = qTimerDueTimes.at(&timer);

    // QTimer::timeout() is declared as "private signal, but can hack around intended access restriction by invoking
    // with empty braced-init-list.
    emit timer.timeout({});

    // Possible that timer has been explicitly stopped from within slot associated with timeout() signal
    auto ii = qTimerDueTimes.find(&timer);

    if ((ii != qTimerDueTimes.end()) && (ii->second == timeDue))
    {
        // Doesn't appear that timer has been explicitly stopped or rescheduled from timeout event handling

        if (timer.isSingleShot())
        {
            qTimerDueTimes.erase(ii);
            reinterpret_cast<QTimerIdAccessor&>(timer).id = inactiveTimerID;
        }
        else
        {
            ii->second += timer.interval();
        }
    }
}

static void generateTimeoutEventforOverdueQTimers(void)
{
    while (true)
    {
        QTimer* pTimer = nextTimerDue();

        if (pTimer == nullptr)
        {
            // No timers active at all
            break;
        }

        qint64 timeDue = qTimerDueTimes.at(pTimer);

        if (timeDue > QDateTime::currentMSecsSinceEpoch())
        {
            // Earliest timer due point is in future
            break;
        }

        generateTimeoutEvent(*pTimer);
    }
}

static void qApplicationTeardown(void);

static void setupIdleTimer(void)
{
    // Called on creation of QApplication object, which in gtest style unit test build, may occur multiple times as QApplication object
    // may be set up & torn down for every case(hence the <idleTimerConfigured> guard)

    // Setup a REAL timer (that does not have it's start/stop/etc intercepted) to perform 10mS interval checking for real timeouts of all
    // the faked timers detailed in <qTimerDueTimes>, such that faked timers will still behave correctly if client test code uses
    // real-world "wait while processing events" type wait instead of or combined with calls to fastForward()

    static QTimer idleTimer;  //This is a functioning REAL timer that does not have it's start/stop/etc intercepted by the library
    static bool idleTimerConfigured = false;

    if (!idleTimerConfigured)
    {
        assert(pQt5Core_QTimer_setInterval != nullptr);
        assert(pQt5Core_QTimer_start != nullptr);

        pQt5Core_QTimer_setInterval(&idleTimer, 10);

        idleTimer.setTimerType(Qt::CoarseTimer);
        idleTimer.setSingleShot(true);  // Single shot timer rescheduled from timeout() event, so we don't get multiple events stacking up
                                        // in hosts message queue
        QObject::connect(&idleTimer,
                         &QTimer::timeout,
                         [&](){

                                if (fakedMSSinceEpoch != -1)
                                {
                                    qint64 realTimeNow = pQt5Core_QDateTime_currentMSecsSinceEpoch();

                                    if (fakedTimeAtLastIdleTimerTick == fakedMSSinceEpoch)
                                    {
                                        // Currently faking time, but 10mS (or more) of real time has passed without any increment to <fakedMSSinceEpoch>,
                                        // suggesting test code may well be in waitWhileProcessingEvents() type loop...

                                        // Step faked time forward in sync. with real time passing
                                        assert(realTimeAtLastIdleTimerTick != -1);
                                        assert(realTimeAtLastIdleTimerTick < realTimeNow);

                                        qint64 realTimeElapsedSinceLastTick = realTimeNow - realTimeAtLastIdleTimerTick;

                                        fastForward(realTimeElapsedSinceLastTick);
                                    }

                                    fakedTimeAtLastIdleTimerTick = fakedMSSinceEpoch;
                                    realTimeAtLastIdleTimerTick  = realTimeNow;
                                }
                                else
                                {
                                    assert(fakedTimeAtLastIdleTimerTick == -1);
                                    assert(realTimeAtLastIdleTimerTick == -1);

                                    // Generate timeout events for any timers that have become due in real time elapsed since last idle timer event
                                    generateTimeoutEventforOverdueQTimers();
                                }

                                // Schedule next idle processing event
                                pQt5Core_QTimer_start(&idleTimer);

                             });

        idleTimerConfigured = true;
    }


    // Plug qApplicationTeardown() routine into QApplication global instance on-destruction cleanup sequence
    qAddPostRoutine(qApplicationTeardown);

    // Start <idleTimer> with real QT5Core QTimer::start() method
    pQt5Core_QTimer_start(&idleTimer);

}

// Call setupIdleTimer() from QCoreApplication constructor once it's got to point of being able to support registration of timers etc.
// NOTE: In gtest style unit test build, may occur multiple times as QApplication object may be set up & torn down for every case.
Q_COREAPP_STARTUP_FUNCTION(setupIdleTimer)

static void qApplicationTeardown(void)
{
    // In gtest style unit test build global QApplication object may be set up & torn down for every test case, rather than just once
    // at program termination

    // Opportunity to clean up/reset QtFakeTime

    fakedTimeAtLastIdleTimerTick   = -1;
    realTimeAtLastIdleTimerTick    = -1;

    // <qElapsedTimerStartTimes> is problematic as QElapsedTimer doesn't have any destructor we can shim/hook to remove entries
    // from the map as corresponding QElapsedTimer instances are destroyed, and thus tends to fill up with stale pointers.
    // Opportunity to purge it here.
    qElapsedTimerStartTimes.clear();

    // <qTimerDueTimes> *SHOULD* be self maintaining, as every time we add a QTimer pointer to it we connect a cleanup lambda function
    // to the timer's destroyed() signal.  This appeared to work reliably on a Ubuntu 20.04(/GCC9) test host, however on shift to
    // Ubuntu 22.04(/GCC 11) occasionally have zombie pointers remaining in map at QApplication teardown, that trigger segmentation
    // faults when referenced in QtFakeTime operations in subsequent tests.
    qTimerDueTimes.clear();
}
