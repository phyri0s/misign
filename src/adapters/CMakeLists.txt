# Port implementations (Qt PDF, PoDoFo, input, local files).
add_library(misign_adapters STATIC
    json_signature_store.cpp
    json_signature_store.h
)
target_link_libraries(misign_adapters
    PUBLIC misign_application Qt6::Core
    PRIVATE misign_warnings
)
