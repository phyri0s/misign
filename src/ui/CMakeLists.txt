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
target_compile_definitions(misign PRIVATE MISIGN_VERSION="${MISIGN_VERSION}")

# Installation (ADR 0008). On Windows, Qt's deployment script (windeployqt) copies
# the Qt libraries, plugins and QML modules next to the executable for the NSIS
# installer. On Linux, the AppImage is assembled by linuxdeploy and its Qt plugin
# from this installed tree, so Qt is not deployed here.
include(GNUInstallDirs)
install(TARGETS misign
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(WIN32)
    qt_generate_deploy_qml_app_script(
        TARGET misign
        OUTPUT_SCRIPT deploy_script
    )
    install(SCRIPT ${deploy_script})
else()
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/linux/misign.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
    )
    install(FILES "${PROJECT_SOURCE_DIR}/packaging/icons/misign.svg"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
    )
endif()
