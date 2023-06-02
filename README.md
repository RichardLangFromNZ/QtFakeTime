# QtFakeTime

QtFakeTime is a library developed to facilitate unit testing of software using the Qt application framework.  It shims assorted functions of the Qt5Core library, allowing test code to fake arbitrary current date/time, and fast-forward time with active QTimer instances receiving faked timeout events along the way.  This means that application code that involves time based state changes doesn't have to be tested at real-time.

QtFakeTime is written in C++ and obviously has a dependancy on the Qt libraries.  It is fundamentally limited to linux hosts (the technique it uses to shim the Qt5Core library is linux specific) and has currently only been tried with Qt 5 and GCC compiler.  It is built using CMake, but should not necessarily be limited to only being used in CMake built projects.

## Building

From a directory containing the QtFakeTime source code in a directory named `QtFakeTime`

```
mkdir QtFakeTime-build
cd QtFakeTime-build
cmake ../QtFakeTime
make
```

The CMake project includes unit tests for the library, built using the GoogleGTest framework.  If a `gtest` install (either system, or a source package indicated by the <googletest_SOURCE_DIR> CMake variable) cannot be identified, the build will skip the tests while issuing a warning.

While QtFakeTime is generated using CMake, there is no reason it can't be used in a project using `make` or various other build systems.


## Getting started

QtFakeTime generates a shared library.  In order to effectively shim the Qt5Core library, a system environment variable named `LD_PRELOAD` indicating the path to the built library binary must be defined in the environment in which test code using it is run.

Supposing the following UUT


```
class Foo: public QObject
{
public:

    void set_bar_after_1_sec();

    bool get_bar() const;

private:
    ...
    <i>implementation using a QTimer to set bar member variable one sec after set_bar_after_1_sec() is called</i>
    ...
};
```

A Google gtest test case might look like the following

```
TEST(Foo_tests, bar_is_set_after_1_sec)
{
    Foo foo;

    ASSERT_FALSE(foo.get_bar());

    foo.set_bar_after_1_sec();

    ASSERT_FALSE(foo.get_bar());


    QThread::msleep(950);
    // QTimer uses application message queue, need to pump message queue
    // after wait in order for timeout message to actually get actioned.
    QCoreApplication::processEvents();

    ASSERT_FALSE(foo.get_bar());

    QThread::msleep(100);
    QCoreApplication::processEvents();

    ASSERT_TRUE(foo.get_bar());
}
```

While the above naive test performs the desired check, it runs in real time.  Given thousands of such tests, or state changes involving much longer timeouts, the testing implementation becomes completely impractical.


Rewritten using QtFakeTime

```
#include "QtFakeTime.h"

...

TEST(Foo_tests, bar_is_set_after_1_sec)
{
    Foo foo;

    ASSERT_FALSE(foo.get_bar());

    foo.set_bar_after_1_sec();

    ASSERT_FALSE(foo.get_bar());

    QtFakeTime::fastForward(950);

    ASSERT_FALSE(foo.get_bar());

    QtFakeTime::fastForward(100);

    ASSERT_TRUE(foo.get_bar());
}
```

the test will now run instantaneously without any blocking waits, but with all QTimer timeout() signal -> slots in the fast-forward periods executed in relative order.


QtFakeTime also includes a `set` method that sets "current" time (as reported by QDateTime::currentDateTime(), currentMSecsSinceEpoch() etc) to any arbitrary value.

```
TEST(Foo_tests, test_for__function_that_only_accepts_files_less_than_4_weeks_old)
{
    QString fileName = some file from test resources with arbitrary timestamp;

    QtFakeTime::set(27 days after file timestamp)

    ASSERT_TRUE(function_that_only_accepts_files_less_than_4_weeks_old(fileName));

    QtFakeTime::set(29 days after file timestamp)

    ASSERT_FALSE(function_that_only_accepts_files_less_than_4_weeks_old(fileName));

}
```

Note that even once faked with a `fastForward` or `set` call, current time still proceeds in rough lockstep with real time (driven from a 10mS resolution timer).

The library also has a `reset` function to return time to real time.

## TODO

The library currently supports faking:

 - QDateTime::currentDateTime/currentMSecsSinceEpoch
 - QTime::currentTime()
 - QElapsedTimer
 - QTimer

In particular is does *NOT* currently support

 - QDeadlineTimer
 - Thread & Async wait/sleep functions
 - QObject timer functions
 - QBasicTimer
