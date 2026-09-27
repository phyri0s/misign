# Use cases and ports. Becomes a static library with its first use case; the
# ports are headers only (signature_store.h).
add_library(misign_application INTERFACE)
target_link_libraries(misign_application INTERFACE misign_domain)
