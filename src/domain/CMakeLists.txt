add_library(misign_domain STATIC
    page_geometry.cpp
    page_geometry.h
    point.cpp
    point.h
    signature.cpp
    signature.h
    stroke.cpp
    stroke.h
)
target_include_directories(misign_domain PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(misign_domain PRIVATE misign_warnings)
set_target_properties(misign_domain PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
