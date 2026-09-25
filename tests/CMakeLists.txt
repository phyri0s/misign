# misign_add_test(<name> LABEL <label> SOURCES <files...> [LIBRARIES <targets...>])
#
# Builds a Qt Test executable and registers it in CTest under <label>,
# so a suite can be run on its own with `ctest -L <label>`.
function(misign_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "LABEL" "SOURCES;LIBRARIES")
    qt_add_executable(${name} ${arg_SOURCES})
    target_link_libraries(${name} PRIVATE ${arg_LIBRARIES} misign_warnings Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS ${arg_LABEL})
endfunction()

add_subdirectory(unit)
add_subdirectory(integration)
