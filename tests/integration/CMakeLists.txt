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
