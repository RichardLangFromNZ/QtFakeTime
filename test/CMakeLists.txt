# Unit testing of QtFakeTime uses googletest framework.

if (DEFINED googletest_SOURCE_DIR)

    # Use source distribution of GTest, location of which is specified via. googletest_SOURCE_DIR variable
    if (NOT EXISTS ${googletest_SOURCE_DIR})
        message(FATAL_ERROR "Specified GTest source directory \"${googletest_SOURCE_DIR}\" doesn't exist!!!")
    endif()

    add_subdirectory(${googletest_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/googletest-build)

else()

    # Check for system install of GTest
    find_package(GTest)

    if (NOT GTest_FOUND)
        message(WARNING "GTest library not found, QtFakeTime unit tests will not be built")
        return()
    endif()

endif()

enable_testing()

add_executable( test_QtFakeTime
                ${CMAKE_CURRENT_SOURCE_DIR}/test_QtFakeTime.cpp)


target_link_libraries(  test_QtFakeTime
                        QtFakeTime
                        Qt5::Core )

if (DEFINED googletest_SOURCE_DIR)
    target_link_libraries(  test_QtFakeTime
                            gtest
                            gmock_main)
else()
    target_link_libraries(  test_QtFakeTime
                            GTest::GTest
                            GTest::Main
                            )
endif()

add_test(NAME test_QtFakeTime COMMAND test_QtFakeTime)
