cmake_minimum_required(VERSION 3.25)

project(Misign
    VERSION 0.1.0 # x-release-please-version
    DESCRIPTION "Sign PDF documents by hand, locally"
    HOMEPAGE_URL "https://github.com/phyri0s/misign"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MISIGN_BUILD_TESTS "Build the unit and integration tests" ON)
option(MISIGN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
# Pre-release label appended to the version, e.g. "-dev.3" (set by the dev deployment).
set(MISIGN_VERSION_SUFFIX "" CACHE STRING "Pre-release suffix appended to the project version")
set(MISIGN_VERSION "${PROJECT_VERSION}${MISIGN_VERSION_SUFFIX}")

option(MISIGN_REQUIRE_TEST_TOOLS "Fail the configuration when qpdf or Python 3 is missing, instead of skipping their tests" OFF)
option(MISIGN_COVERAGE "Instrument the build for code coverage (GCC only)" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(MisignWarnings)
include(MisignDependencyRules)
include(MisignCoverage)

set(MISIGN_QT_COMPONENTS Core Gui Qml Quick)
if(MISIGN_BUILD_TESTS)
    list(APPEND MISIGN_QT_COMPONENTS Test)
endif()
find_package(Qt6 6.11 REQUIRED COMPONENTS ${MISIGN_QT_COMPONENTS})
qt_standard_project_setup(REQUIRES 6.11)

add_subdirectory(src)

if(MISIGN_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# The domain must stay free of Qt, PoDoFo and any other external library (ADR 0001).
misign_forbid_external_dependencies(misign_domain)

# The NSIS installer (ADR 0008); the Linux AppImage is built by linuxdeploy in CI.
if(WIN32)
    include(MisignPackaging)
endif()
