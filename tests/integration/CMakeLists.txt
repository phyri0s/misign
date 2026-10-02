# Integration tests run full use cases with the real adapters, under the
# "integration" label.

misign_add_test(tst_podofo_pdf_inspector
    LABEL integration
    SOURCES tst_podofo_pdf_inspector.cpp
    # PoDoFo itself to write an encrypted file, which the corpus does not have.
    LIBRARIES misign_adapters podofo::podofo
)
# Reads the corpus and its manifest (expected boxes and /Rotate of every page).
target_compile_definitions(tst_podofo_pdf_inspector PRIVATE
    MISIGN_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/pdf"
)

misign_add_test(tst_qtpdf_renderer
    LABEL integration
    SOURCES tst_qtpdf_renderer.cpp
    # PoDoFo itself to write the annotated and encrypted files the corpus does not have.
    LIBRARIES misign_adapters podofo::podofo Qt6::Gui
)
target_compile_definitions(tst_qtpdf_renderer PRIVATE
    MISIGN_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/pdf"
)

# The page view as the application shows it: the test builds the application's
# QML module (src/ui) into its own executable, from the same files.
misign_add_test(tst_page_view
    LABEL integration
    SOURCES tst_page_view.cpp
    LIBRARIES misign_adapters Qt6::Qml Qt6::Quick
)
set(ui_dir "${PROJECT_SOURCE_DIR}/src/ui")
get_property(ui_qml_files GLOBAL PROPERTY MISIGN_UI_QML_FILES)
get_property(ui_sources GLOBAL PROPERTY MISIGN_UI_SOURCES)
foreach(file IN LISTS ui_qml_files)
    # Keeps the files at the root of the module, as in the application.
    set_source_files_properties("${ui_dir}/${file}" PROPERTIES QT_RESOURCE_ALIAS "${file}")
endforeach()
list(TRANSFORM ui_qml_files PREPEND "${ui_dir}/")
list(TRANSFORM ui_sources PREPEND "${ui_dir}/")
qt_add_qml_module(tst_page_view
    URI Misign
    VERSION 1.0
    QML_FILES ${ui_qml_files}
    SOURCES ${ui_sources}
    # The application's module is the one that is linted.
    NO_LINT
)
target_include_directories(tst_page_view PRIVATE "${PROJECT_SOURCE_DIR}/src" "${ui_dir}")
target_compile_definitions(tst_page_view PRIVATE
    MISIGN_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/pdf"
)
# A device pixel ratio of 2, to check it reaches the render scale.
set_tests_properties(tst_page_view PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=2")
