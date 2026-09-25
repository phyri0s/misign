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

# Every file of the reference corpus must pass `qpdf --check` (see fixtures/pdf/README.md).
find_program(QPDF_EXECUTABLE qpdf)
if(QPDF_EXECUTABLE)
    file(GLOB fixtures CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pdf/*.pdf")
    foreach(fixture IN LISTS fixtures)
        cmake_path(GET fixture FILENAME name)
        add_test(NAME qpdf_check_${name} COMMAND ${QPDF_EXECUTABLE} --check ${fixture})
        set_tests_properties(qpdf_check_${name} PROPERTIES LABELS integration)
    endforeach()
else()
    message(STATUS "qpdf not found: the reference corpus checks are skipped")
endif()
