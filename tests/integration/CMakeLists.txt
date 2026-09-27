# Integration tests run full use cases with the real adapters, under the
# "integration" label.

misign_add_test(tst_json_signature_store
    LABEL integration
    SOURCES tst_json_signature_store.cpp
    LIBRARIES misign_adapters
)
