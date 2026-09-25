# Port implementations (Qt PDF, PoDoFo, input, local files).
# Becomes a static library with its first adapter.
add_library(misign_adapters INTERFACE)
target_link_libraries(misign_adapters INTERFACE misign_application)
