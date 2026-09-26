# Code coverage instrumentation (ADR 0009), enabled by MISIGN_COVERAGE.
# The report itself is produced by gcovr (gcovr.cfg), which only keeps src/.
if(NOT MISIGN_COVERAGE)
    return()
endif()

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "MISIGN_COVERAGE needs GCC (gcov), not ${CMAKE_CXX_COMPILER_ID}")
endif()

# Every target defined after this point is instrumented, tests included: gcovr
# filters them out of the report. -fprofile-update=atomic keeps the counters
# consistent in multithreaded code (Qt's worker threads).
add_compile_options(--coverage -fprofile-update=atomic)
add_link_options(--coverage)
