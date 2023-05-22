#include <gtest/gtest.h>

#include <QDebug>
#include <QTime>
#include <QThread>
#include <QElapsedTimer>
#include <QTimer>
#include <QCoreApplication>


#include "QtFakeTime.h"

using ::testing::Test;

class QtFakeTimeTests : public Test
{
protected:
    // Need Qt event loop when testing code that uses QTimer, Qt queued method invocation etc.
    int dummy_argc;
    char** dummy_argv;
    QCoreApplication dummy_application_;

    QtFakeTimeTests()
        : dummy_argc(1), dummy_argv(nullptr), dummy_application_(dummy_argc, dummy_argv) {}


    virtual void SetUp()
    {
        QtFakeTime::reset();
    }

    virtual void TearDown()
    {
    }
};

void WaitWhileProcessingEvents(int wait_time_ms)
{
    QTime end_time = QTime::currentTime().addMSecs(wait_time_ms);
    while (QTime::currentTime() < end_time)
    {
        QThread::yieldCurrentThread();

        QCoreApplication::processEvents();
    }
}

TEST_F(QtFakeTimeTests, QDateTime_currentDateTime)
{
    // Confirm that QDateTime::currentDateTime() reflects fast-forward of time
    QDateTime t1 = QDateTime::currentDateTime();

    QtFakeTime::fastForward(5000);

    QDateTime t2 = QDateTime::currentDateTime();

    ASSERT_NEAR(t2.currentMSecsSinceEpoch(), t1.addMSecs(5000).currentMSecsSinceEpoch(), 10);

    // Confirm that QDateTime::currentDateTime() reflects natural/real passing of time
    WaitWhileProcessingEvents(500);

    t2 = QDateTime::currentDateTime();

    ASSERT_NEAR(t2.currentMSecsSinceEpoch(), t1.addMSecs(5500).currentMSecsSinceEpoch(), 10);

    QtFakeTime::reset();

    t2 = QDateTime::currentDateTime();

    ASSERT_NEAR(t2.currentMSecsSinceEpoch(), t1.addMSecs(500).currentMSecsSinceEpoch(), 10);


    // Confirm that QDateTime::currentDateTime() reflects arbitrary set of time
    QDateTime someOtherTime = QDateTime::fromString("2022-07-11T01:23:45", Qt::ISODate);

    QtFakeTime::set(someOtherTime);

    ASSERT_EQ(someOtherTime, QDateTime::currentDateTime());

    QtFakeTime::fastForward(5000);

    ASSERT_NEAR(someOtherTime.addMSecs(5000).toMSecsSinceEpoch(), QDateTime::currentDateTime().toMSecsSinceEpoch(), 10);
}

TEST_F(QtFakeTimeTests, QDateTime_currentDateTimeUtc)
{
    // Confirm that QDateTime::currentDateTimeUtc() reflects fast-forward of time
    QDateTime t1 = QDateTime::currentDateTimeUtc();

    QtFakeTime::fastForward(5000);

    QDateTime t2 = QDateTime::currentDateTimeUtc();

    ASSERT_NEAR(t2.currentMSecsSinceEpoch(), t1.addMSecs(5000).currentMSecsSinceEpoch(), 10);

    // Confirm that QDateTime::currentDateTimeUtc() reflects natural/real passing of time
    WaitWhileProcessingEvents(500);

    t2 = QDateTime::currentDateTimeUtc();

    ASSERT_NEAR(t2.currentMSecsSinceEpoch(), t1.addMSecs(5500).currentMSecsSinceEpoch(), 10);

    // Confirm that QDateTime::currentDateTimeUtc() reflects arbitrary set of time
    QDateTime someOtherTime = QDateTime::fromString("2022-07-11T01:23:45Z", Qt::ISODate);

    QtFakeTime::set(someOtherTime);

    ASSERT_EQ(someOtherTime, QDateTime::currentDateTimeUtc());

    QtFakeTime::fastForward(5000);

    ASSERT_NEAR(someOtherTime.addMSecs(5000).toMSecsSinceEpoch(), QDateTime::currentDateTimeUtc().toMSecsSinceEpoch(), 10);

}

TEST_F(QtFakeTimeTests, QDateTime_currentMSecsSinceEpoch)
{
    // Confirm that QDateTime::currentMSecsSinceEpoch() reflects fast-forward of time
    qint64 t1 = QDateTime::currentMSecsSinceEpoch();

    QtFakeTime::fastForward(5000);

    qint64 t2 = QDateTime::currentMSecsSinceEpoch();

    ASSERT_NEAR(t2, t1 + 5000, 10);

    // Confirm that QDateTime::currentMSecsSinceEpoch() reflects natural/real passing of time
    WaitWhileProcessingEvents(500);

    t2 = QDateTime::currentMSecsSinceEpoch();

    ASSERT_NEAR(t2, t1 + 5500, 10);

    // Confirm that QDateTime::currentMSecsSinceEpoch() reflects arbitrary set of time
    QDateTime someOtherTime = QDateTime::fromString("2022-07-11T01:23:45", Qt::ISODate);

    QtFakeTime::set(someOtherTime);

    ASSERT_EQ(someOtherTime.toMSecsSinceEpoch(), QDateTime::currentMSecsSinceEpoch());

    QtFakeTime::fastForward(5000);

    ASSERT_NEAR(someOtherTime.addMSecs(5000).toMSecsSinceEpoch(), QDateTime::currentMSecsSinceEpoch(), 10);

}

TEST_F(QtFakeTimeTests, QDateTime_currentSecsSinceEpoch)
{
    // Confirm that QDateTime::currentSecsSinceEpoch() reflects fast-forward of time
    qint64 t1 = QDateTime::currentSecsSinceEpoch();

    QtFakeTime::fastForward(5000);

    qint64 t2 = QDateTime::currentSecsSinceEpoch();

    ASSERT_EQ(t2, t1 + 5);
}

TEST_F(QtFakeTimeTests, QTime_currentTime)
{
    // Confirm that QTime::currentTime() reflects fast-forward of time
    QTime t1 = QTime::currentTime();

    QtFakeTime::fastForward(5000);

    QTime t2 = QTime::currentTime();

    ASSERT_NEAR(t2.msecsSinceStartOfDay(), t1.addMSecs(5000).msecsSinceStartOfDay(), 10);

    // Confirm that QTime::currentTime() reflects natural/real passing of time
    WaitWhileProcessingEvents(500);

    t2 = QTime::currentTime();

    ASSERT_NEAR(t2.msecsSinceStartOfDay(), t1.addMSecs(5500).msecsSinceStartOfDay(), 10);

    QtFakeTime::reset();

    t2 = QTime::currentTime();

    ASSERT_NEAR(t2.msecsSinceStartOfDay(), t1.addMSecs(500).msecsSinceStartOfDay(), 12); // Sometimes ends up slight over 10 for "reasons"...

    // Confirm that QDateTime::currentMSecsSinceEpoch() reflects arbitrary set of time
    QDateTime someOtherTime = QDateTime::fromString("2022-07-11T01:23:45", Qt::ISODate);

    QtFakeTime::set(someOtherTime);

    ASSERT_EQ(someOtherTime.time(), QTime::currentTime());

    QtFakeTime::fastForward(5000);

    ASSERT_NEAR(someOtherTime.addMSecs(5000).time().msecsSinceStartOfDay(), QTime::currentTime().msecsSinceStartOfDay(), 10);
}

TEST_F(QtFakeTimeTests, QElapsedTimer)
{
    QElapsedTimer timer;

    // Confirm thatQElapsedTimer reflects mashup of fast-forward and natural/real passing of time
    timer.start();

    ASSERT_EQ(0, timer.elapsed());

    WaitWhileProcessingEvents(500);

    ASSERT_NEAR(500, timer.elapsed(), 10);

    QtFakeTime::fastForward(500);

    ASSERT_NEAR(1000, timer.elapsed(), 10);

    ASSERT_NEAR(1000, timer.restart(), 10);

    ASSERT_EQ(0, timer.elapsed());

    QtFakeTime::fastForward(500);

    ASSERT_EQ(500, timer.elapsed());

    WaitWhileProcessingEvents(500);

    ASSERT_NEAR(1000, timer.elapsed(), 10);

    ASSERT_TRUE(timer.hasExpired(990));
    ASSERT_FALSE(timer.hasExpired(1100));
}

TEST_F(QtFakeTimeTests, QElapsedTimer_restarted_on_big_backwards_jump_in_faked_time)
{
    QElapsedTimer timer;

    ASSERT_FALSE(timer.isValid());

    timer.start();

    ASSERT_TRUE(timer.isValid());

    QtFakeTime::fastForward(1000);

    ASSERT_NEAR(1000, timer.elapsed(), 10);

    // Big jump backwards
    QtFakeTime::set(QDateTime::fromString("2022-07-11T01:23:45", Qt::ISODate));

    ASSERT_TRUE(timer.isValid());

    ASSERT_EQ(0, timer.elapsed());
}

TEST_F(QtFakeTimeTests, QTimer_behaves_normally_in_absense_of_fast_forward)
{
    int timeoutCounter = 0;

    QTimer timer;

    QObject::connect(&timer, &QTimer::timeout, [&](){++timeoutCounter;});

    timer.setSingleShot(true);
    timer.setInterval(1000);
    timer.start();

    WaitWhileProcessingEvents(900);

    ASSERT_EQ(0, timeoutCounter);

    WaitWhileProcessingEvents(200);

    ASSERT_EQ(1, timeoutCounter);

    timer.setSingleShot(false);
    timer.start();

    WaitWhileProcessingEvents(900);

    ASSERT_EQ(1, timeoutCounter);

    WaitWhileProcessingEvents(200);

    ASSERT_EQ(2, timeoutCounter);

    WaitWhileProcessingEvents(1000);

    ASSERT_EQ(3, timeoutCounter);
}

TEST_F(QtFakeTimeTests, QTimer_single_shot_timer_honours_fast_forward)
{
    int timeoutCounter = 0;

    QTimer timer;

    QObject::connect(&timer, &QTimer::timeout, [&](){++timeoutCounter;});

    timer.setSingleShot(true);
    timer.setInterval(1000);
    timer.start();

    QtFakeTime::fastForward(900);

    ASSERT_EQ(0, timeoutCounter);

    QtFakeTime::fastForward(200);

    ASSERT_EQ(1, timeoutCounter);


    // Mix of "real wait and fastForward() calls
    timer.start();

    WaitWhileProcessingEvents(900);

    ASSERT_EQ(1, timeoutCounter);

    QtFakeTime::fastForward(200);

    ASSERT_EQ(2, timeoutCounter);

    timer.start();

    QtFakeTime::fastForward(900);

    ASSERT_EQ(2, timeoutCounter);

    WaitWhileProcessingEvents(200);

    ASSERT_EQ(3, timeoutCounter);

    // Ensure timer is in fact 1-shot

    WaitWhileProcessingEvents(1100);

    ASSERT_EQ(3, timeoutCounter);

    QtFakeTime::fastForward(900);

    ASSERT_EQ(3, timeoutCounter);

    // Ensure timer stops when instructed

    timer.start();

    QtFakeTime::fastForward(900);

    ASSERT_EQ(3, timeoutCounter);

    timer.stop();

    QtFakeTime::fastForward(200);

    ASSERT_EQ(3, timeoutCounter);
}

TEST_F(QtFakeTimeTests, QTimer_repeating_timer_honours_fast_forward)
{
    int timeoutCounter = 0;

    QTimer timer;

    QObject::connect(&timer, &QTimer::timeout, [&](){++timeoutCounter;});

    timer.setSingleShot(false);
    timer.setInterval(1000);
    timer.start();

    QtFakeTime::fastForward(900);

    ASSERT_EQ(0, timeoutCounter);

    QtFakeTime::fastForward(200);

    ASSERT_EQ(1, timeoutCounter);

    WaitWhileProcessingEvents(800);

    ASSERT_EQ(1, timeoutCounter);

    QtFakeTime::fastForward(200);

    ASSERT_EQ(2, timeoutCounter);

    QtFakeTime::fastForward(800);

    ASSERT_EQ(2, timeoutCounter);

    WaitWhileProcessingEvents(200);

    ASSERT_EQ(3, timeoutCounter);

    // Ensure timer stops when instructed

    timer.stop();

    QtFakeTime::fastForward(1000);

    ASSERT_EQ(3, timeoutCounter);
}

TEST_F(QtFakeTimeTests, QTimer_timerId_and_isActive_reflect_run_state)
{
    int timeoutCounter = 0;

    QTimer timer;

    QObject::connect(&timer, &QTimer::timeout, [&](){++timeoutCounter;});

    timer.setSingleShot(true);
    timer.setInterval(1000);

    ASSERT_FALSE(timer.isActive());
    ASSERT_EQ(-1, timer.timerId());

    timer.start();

    ASSERT_TRUE(timer.isActive());
    ASSERT_NE(-1, timer.timerId());

    timer.stop();

    ASSERT_FALSE(timer.isActive());
    ASSERT_EQ(-1, timer.timerId());

    timer.start();

    ASSERT_TRUE(timer.isActive());
    ASSERT_NE(-1, timer.timerId());

    QtFakeTime::fastForward(990);

    ASSERT_TRUE(timer.isActive());
    ASSERT_NE(-1, timer.timerId());

    QtFakeTime::fastForward(20);

    ASSERT_FALSE(timer.isActive());
    ASSERT_EQ(-1, timer.timerId());
}

TEST_F(QtFakeTimeTests, interleaved_timers_trigger_appropriately)
{
    int timer1TimeoutCounter = 0;

    QTimer timer1;

    QObject::connect(&timer1, &QTimer::timeout, [&](){++timer1TimeoutCounter;});

    timer1.setSingleShot(false);
    timer1.setInterval(1000);

    int timer2TimeoutCounter = 0;

    QTimer timer2;

    QObject::connect(&timer2, &QTimer::timeout, [&](){++timer2TimeoutCounter;});

    timer2.setSingleShot(true);
    timer2.setInterval(2000);

    timer2.start();
    timer1.start();

    QtFakeTime::fastForward(800);

    ASSERT_EQ(0, timer1TimeoutCounter);
    ASSERT_EQ(0, timer2TimeoutCounter);

    QtFakeTime::fastForward(800);

    ASSERT_EQ(1, timer1TimeoutCounter);
    ASSERT_EQ(0, timer2TimeoutCounter);

    QtFakeTime::fastForward(800);

    ASSERT_EQ(2, timer1TimeoutCounter);
    ASSERT_EQ(1, timer2TimeoutCounter);

    QtFakeTime::fastForward(800);

    ASSERT_EQ(3, timer1TimeoutCounter);
    ASSERT_EQ(1, timer2TimeoutCounter);

}

TEST_F(QtFakeTimeTests, QTimer_single_shot_timer_scheduled_via_static_method_honours_fast_forward)
{
    int timeoutCounter = 0;

    QTimer::singleShot(1000, [&](){++timeoutCounter;});

    QtFakeTime::fastForward(990);

    ASSERT_EQ(0, timeoutCounter);

    QtFakeTime::fastForward(20);

    ASSERT_EQ(1, timeoutCounter);

    QtFakeTime::fastForward(1100);

    ASSERT_EQ(1, timeoutCounter);
}

TEST_F(QtFakeTimeTests, QTimer_zero_interval_single_shot_timer_actioned_on_fast_forward)
{
    int timeoutCounter = 0;

    QTimer timer;

    QObject::connect(&timer, &QTimer::timeout, [&](){++timeoutCounter;});

    timer.setSingleShot(true);
    timer.setInterval(0);

    timer.start();

    ASSERT_EQ(0, timeoutCounter);

    QtFakeTime::fastForward(0);

    ASSERT_EQ(1, timeoutCounter);

    QtFakeTime::fastForward(10);

    ASSERT_EQ(1, timeoutCounter);
}

TEST_F(QtFakeTimeTests, QTimer_zero_interval_single_shot_timer_actioned_on_QCoreApplication_processEvents)
{
    int timeoutCounter = 0;

    QTimer timer;

    QObject::connect(&timer, &QTimer::timeout, [&](){++timeoutCounter;});

    timer.setSingleShot(true);
    timer.setInterval(0);

    timer.start();

    ASSERT_EQ(0, timeoutCounter);

    QCoreApplication::processEvents();

    ASSERT_EQ(1, timeoutCounter);

    QCoreApplication::processEvents();

    ASSERT_EQ(1, timeoutCounter);
}


TEST_F(QtFakeTimeTests, QTimer_zero_interval_single_shot_timer_scheduled_via_static_method_actioned_on_fast_forward)
{
    int timeoutCounter = 0;

    QTimer::singleShot(0, [&](){++timeoutCounter;});

    ASSERT_EQ(0, timeoutCounter);

    QtFakeTime::fastForward(0);

    ASSERT_EQ(1, timeoutCounter);

    QtFakeTime::fastForward(0);

    ASSERT_EQ(1, timeoutCounter);
}

TEST_F(QtFakeTimeTests, QTimer_zero_interval_single_shot_timer_scheduled_via_static_method_actioned_on_QCoreApplication_processEvents)
{
    int timeoutCounter = 0;

    QTimer::singleShot(0, [&](){++timeoutCounter;});

    ASSERT_EQ(0, timeoutCounter);

    QCoreApplication::processEvents();

    ASSERT_EQ(1, timeoutCounter);

    QCoreApplication::processEvents();

    ASSERT_EQ(1, timeoutCounter);
}
