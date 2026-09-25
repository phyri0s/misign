cmake_minimum_required(VERSION 3.25)

project(Misign
    VERSION 0.1.0
    DESCRIPTION "Sign PDF documents by hand, locally"
    HOMEPAGE_URL "https://github.com/phyri0s/misign"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(MISIGN_BUILD_TESTS "Build the unit and integration tests" ON)
option(MISIGN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(MisignWarnings)
include(MisignDependencyRules)

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
