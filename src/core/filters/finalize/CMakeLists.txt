set(CMAKE_AUTOUIC ON)

set(ui_files
    OptionsWidget.ui)

set(sources
    ImageView.cpp ImageView.h
    Filter.cpp Filter.h
    OptionsWidget.cpp OptionsWidget.h
    Task.cpp Task.h
    CacheDrivenTask.cpp CacheDrivenTask.h
    Settings.cpp Settings.h AutoColorModePolicy.h ThumbnailColorModeFilter.h
    AutoColorDecisionReset.cpp AutoColorDecisionReset.h
    Thumbnail.cpp Thumbnail.h)

add_library(finalize STATIC ${sources} ${ui_files})
target_link_libraries(finalize PUBLIC core PRIVATE dewarping output page_layout)
target_include_directories(finalize PRIVATE ${CMAKE_BINARY_DIR}/src/core/filters/page_layout/page_layout_autogen/include)

translation_sources(scantailor ${sources} ${ui_files})
