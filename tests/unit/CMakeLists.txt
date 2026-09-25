misign_add_test(tst_point
    LABEL unit
    SOURCES tst_point.cpp
    LIBRARIES misign_domain
)

misign_add_test(tst_page_geometry
    LABEL unit
    SOURCES tst_page_geometry.cpp
    LIBRARIES misign_domain
)
# Reads the corpus manifest (expected user-space coordinates of every page).
target_compile_definitions(tst_page_geometry PRIVATE
    MISIGN_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/pdf"
)
