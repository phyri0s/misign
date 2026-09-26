# PHY-97 spike, built on its own, never part of the Misign build:
#   cmake -S spikes/phy-97-stylus-input -B build-spike-97 -G Ninja && cmake --build build-spike-97
cmake_minimum_required(VERSION 3.25)
project(Phy97Spike LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.11 REQUIRED COMPONENTS Gui Qml Quick)
qt_standard_project_setup(REQUIRES 6.11)

qt_add_executable(stylus-probe main.cpp recorder.cpp recorder.h inkview.cpp inkview.h)
qt_add_qml_module(stylus-probe
    URI StylusProbe
    VERSION 1.0
    QML_FILES Main.qml
)
target_link_libraries(stylus-probe PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
# QWindowsApplication::setWinTabEnabled, the only way to turn Wintab on in Qt 6.11.
if(WIN32)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
    target_link_libraries(stylus-probe PRIVATE Qt6::GuiPrivate)
endif()
set_target_properties(stylus-probe PROPERTIES WIN32_EXECUTABLE TRUE)
