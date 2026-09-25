qt_add_executable(misign
    main.cpp
)

qt_add_qml_module(misign
    URI Misign
    VERSION 1.0
    QML_FILES Main.qml
)

target_link_libraries(misign PRIVATE
    misign_application
    misign_adapters
    misign_warnings
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
)

set_target_properties(misign PROPERTIES WIN32_EXECUTABLE TRUE)
