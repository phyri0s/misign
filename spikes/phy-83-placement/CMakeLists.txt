# PHY-83 spike, built on its own, never part of the Misign build:
#   cmake -S spikes/phy-83-placement -B build-spike -G Ninja && cmake --build build-spike
cmake_minimum_required(VERSION 3.25)
project(Phy83Spike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(podofo 1.1 REQUIRED)
find_package(Qt6 6.11 REQUIRED COMPONENTS Gui Pdf)

# The conversion under test: the real domain code.
set(misign_src "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
add_executable(placement placement.cpp "${misign_src}/domain/page_geometry.cpp")
target_include_directories(placement PRIVATE "${misign_src}")
target_link_libraries(placement PRIVATE podofo::podofo_shared Qt6::Gui Qt6::Pdf)
