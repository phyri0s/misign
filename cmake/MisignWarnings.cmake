# Compiler warnings shared by every Misign target.
add_library(misign_warnings INTERFACE)

if(MSVC)
    target_compile_options(misign_warnings INTERFACE /W4 /permissive-)
    if(MISIGN_WARNINGS_AS_ERRORS)
        target_compile_options(misign_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(misign_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
    )
    if(MISIGN_WARNINGS_AS_ERRORS)
        target_compile_options(misign_warnings INTERFACE -Werror)
    endif()
endif()
