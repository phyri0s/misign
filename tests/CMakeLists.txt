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

# Reference corpus (see fixtures/pdf/README.md): the committed files must match
# generate.py, and every file must pass `qpdf --check`.
set(fixtures_dir "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pdf")

# CI sets MISIGN_REQUIRE_TEST_TOOLS, so these tests can never be skipped silently there.
if(MISIGN_REQUIRE_TEST_TOOLS)
    set(missing_tool_level FATAL_ERROR)
else()
    set(missing_tool_level WARNING)
endif()

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME fixtures_up_to_date COMMAND Python3::Interpreter "${fixtures_dir}/generate.py" --check)
    set_tests_properties(fixtures_up_to_date PROPERTIES LABELS integration)
else()
    message(${missing_tool_level} "Python 3 not found: the reference corpus is not checked against generate.py")
endif()

find_program(QPDF_EXECUTABLE qpdf)
if(QPDF_EXECUTABLE)
    file(GLOB fixtures CONFIGURE_DEPENDS "${fixtures_dir}/*.pdf")
    foreach(fixture IN LISTS fixtures)
        cmake_path(GET fixture FILENAME name)
        add_test(NAME qpdf_check_${name} COMMAND ${QPDF_EXECUTABLE} --check ${fixture})
        set_tests_properties(qpdf_check_${name} PROPERTIES LABELS integration)
    endforeach()
else()
    message(${missing_tool_level} "qpdf not found: the reference corpus is not checked with qpdf --check")
endif()
