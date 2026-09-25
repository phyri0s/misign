# misign_forbid_external_dependencies(<target>)
#
# Fails the configuration when <target> links anything other than Misign's own
# targets. Must be called once every target has been declared.
function(misign_forbid_external_dependencies target)
    set(allowed misign_warnings)
    foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(libraries ${target} ${property})
        if(NOT libraries)
            continue()
        endif()
        foreach(library IN LISTS libraries)
            # Strip generator expressions such as $<LINK_ONLY:...> or $<BUILD_INTERFACE:...>.
            string(REGEX REPLACE "^\\$<[A-Z_]+:(.*)>$" "\\1" library "${library}")
            if(NOT library IN_LIST allowed)
                message(FATAL_ERROR
                    "${target} must not depend on external libraries, but links '${library}'. "
                    "See docs/adr/0001-hexagonal-architecture.md."
                )
            endif()
        endforeach()
    endforeach()
endfunction()
