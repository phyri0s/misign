# Use cases and ports. Becomes a static library with its first use case.
add_library(misign_application INTERFACE)
target_link_libraries(misign_application INTERFACE misign_domain)
