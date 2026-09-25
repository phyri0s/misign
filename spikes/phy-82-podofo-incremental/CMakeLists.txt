# PHY-82 spike, built on its own, never part of the Misign build:
#   cmake -S spikes/phy-82-podofo-incremental -B build-spike -G Ninja && cmake --build build-spike
cmake_minimum_required(VERSION 3.25)
project(Phy82Spike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(podofo 1.1 REQUIRED)
find_package(Qt6 6.11 REQUIRED COMPONENTS Gui Pdf)

add_executable(draw draw.cpp)
target_link_libraries(draw PRIVATE podofo::podofo_shared)

add_executable(render render.cpp)
target_link_libraries(render PRIVATE Qt6::Gui Qt6::Pdf)
