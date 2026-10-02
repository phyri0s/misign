# Port implementations (Qt PDF, PoDoFo, input).
add_library(misign_adapters STATIC
    podofo_pdf_inspector.cpp
    podofo_pdf_inspector.h
    qtpdf_renderer.cpp
    qtpdf_renderer.h
)
target_link_libraries(misign_adapters
    PUBLIC misign_application
    PRIVATE misign_warnings podofo::podofo Qt6::Gui Qt6::Pdf
)
set_target_properties(misign_adapters PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
