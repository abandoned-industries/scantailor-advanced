set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(resource_files
    resources.qrc
    dark_scheme/dark_scheme.qrc
    light_scheme/light_scheme.qrc)
list_items_prepend(resource_files "${SCANTAILOR_RESOURCES_DIR}/")

set(gui_only_sources
    RelinkablePathVisualization.cpp RelinkablePathVisualization.h
    RelinkingModel.cpp RelinkingModel.h
    RelinkingSortingModel.cpp RelinkingSortingModel.h
    RelinkingListView.cpp RelinkingListView.h
    RelinkingDialog.cpp RelinkingDialog.h
    SettingsDialog.cpp SettingsDialog.h
    AutoProcessDialog.cpp AutoProcessDialog.h
    FixDpiDialog.cpp FixDpiDialog.h
    LoadFilesStatusDialog.cpp LoadFilesStatusDialog.h
    ProjectCreationContext.cpp ProjectCreationContext.h
    ProjectOpeningContext.cpp ProjectOpeningContext.h
    OutOfMemoryDialog.cpp OutOfMemoryDialog.h
    ThumbnailSequence.cpp ThumbnailSequence.h
    ProjectFilesDialog.cpp ProjectFilesDialog.h
    NewOpenProjectPanel.cpp NewOpenProjectPanel.h
    StartupWindow.cpp StartupWindow.h
    ZoteroPluginInstaller.cpp ZoteroPluginInstaller.h
    AppController.cpp AppController.h
    AccessibilitySafeguard.cpp AccessibilitySafeguard.h
    SystemLoadWidget.cpp SystemLoadWidget.h
    MainWindow.cpp MainWindow.h
    main.cpp
    StatusBarPanel.cpp StatusBarPanel.h
    DefaultParamsDialog.cpp DefaultParamsDialog.h
    AutoAccept.cpp AutoAccept.h
    BenchmarkLog.cpp BenchmarkLog.h
    BatchSummaries.cpp BatchSummaries.h
    PdfExportFlow.cpp PdfExportFlow.h
    TempOutputCleanup.cpp TempOutputCleanup.h
    BatchProcessingSummaryDialog.cpp BatchProcessingSummaryDialog.h
    ContentCoverageSummaryDialog.cpp ContentCoverageSummaryDialog.h
    PageBoxSummaryDialog.cpp PageBoxSummaryDialog.h
    PageSizeWarningDialog.cpp PageSizeWarningDialog.h
    PdfImportDialog.cpp PdfImportDialog.h)

set(gui_only_ui_files
    AboutDialog.ui
    AutoProcessDialog.ui
    BatchProcessingLowerPanel.ui
    DefaultParamsDialog.ui
    FixDpiDialog.ui
    LoadFilesStatusDialog.ui
    MainWindow.ui
    NewOpenProjectPanel.ui
    OutOfMemoryDialog.ui
    ProjectFilesDialog.ui
    RelinkingDialog.ui
    RemovePagesDialog.ui
    SettingsDialog.ui
    StatusBarPanel.ui
    SystemLoadWidget.ui)

set(win32_resource_file "")
if (WIN32)
  string(REPLACE "/" "\\\\" SCANTAILOR_WIN_DIR_NATIVE "${SCANTAILOR_RESOURCES_DIR}/win32")
  configure_file("${SCANTAILOR_RESOURCES_DIR}/win32/resources.rc.in" "${CMAKE_BINARY_DIR}/resources.rc" @ONLY)
  set(rc_file "${CMAKE_BINARY_DIR}/resources.rc")
  file(GLOB win32_resources "${SCANTAILOR_RESOURCES_DIR}/win32/*.ico")
  set_source_files_properties(
      "${rc_file}" PROPERTIES
      OBJECT_DEPENDS ${win32_resources})
  if (MINGW)
    # CMake doesn't know how to process .rc files with MinGW.
    set(win32_resource_file "${CMAKE_BINARY_DIR}/win32_resources.o")
    add_custom_command(
        OUTPUT "${win32_resource_file}"
        WORKING_DIRECTORY "${SCANTAILOR_RESOURCES_DIR}/win32"
        COMMAND windres -i "${rc_file}" -o "${win32_resource_file}"
        MAIN_DEPENDENCY "${rc_file}"
        DEPENDS ${win32_resources})
  else()
    set(win32_resource_file "${rc_file}")
  endif()
endif()

# Determine if we're building a macOS bundle
set(MACOS_BUNDLE_FLAG "")
if(APPLE)
  set(MACOS_BUNDLE_FLAG MACOSX_BUNDLE)
endif()

add_executable(
    scantailor WIN32 ${MACOS_BUNDLE_FLAG} ${gui_only_sources} ${gui_only_ui_files}
    ${resource_files} ${win32_resource_file})
# Ensure version.h is regenerated before building scantailor
add_dependencies(scantailor generate_version_h)
target_link_libraries(
    scantailor
    PRIVATE core ${EXTRA_LIBS})
target_include_directories(
    scantailor
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
    "$<TARGET_PROPERTY:TIFF::TIFF,INTERFACE_INCLUDE_DIRECTORIES>"
    "${CMAKE_SOURCE_DIR}/src/dewarping")

# macOS bundle configuration
if(APPLE)
  option(SCANTAILOR_MAC_BUNDLE_POST_BUILD
         "Run macdeployqt, bundle-fixups, and ad-hoc signing after every scantailor build"
         OFF)

  set_target_properties(scantailor PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "ScanTailor Spectre"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.scantailor.spectre"
    MACOSX_BUNDLE_BUNDLE_VERSION "${VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${VERSION}"
    MACOSX_BUNDLE_ICON_FILE "scantailor.icns"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in"
    OUTPUT_NAME "ScanTailor Spectre"
  )

  set(SCANTAILOR_DOCUMENT_OCR_BUNDLE_COMMAND)
  if(TARGET spectre_document_ocr_bridge)
    add_dependencies(scantailor spectre_document_ocr_bridge)
    add_custom_command(TARGET scantailor POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_BUNDLE_DIR:scantailor>/Contents/Frameworks"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SCANTAILOR_DOCUMENT_OCR_BRIDGE}"
        "$<TARGET_BUNDLE_DIR:scantailor>/Contents/Frameworks/"
      COMMENT "Copying document OCR bridge to app bundle..."
      VERBATIM)
    list(APPEND SCANTAILOR_DOCUMENT_OCR_BUNDLE_COMMAND
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SCANTAILOR_DOCUMENT_OCR_BRIDGE}"
        "$<TARGET_BUNDLE_DIR:scantailor>/Contents/Frameworks/")
  endif()

  # Copy icon file into bundle Resources
  set(MACOS_ICON_FILE "${CMAKE_SOURCE_DIR}/packaging/macos/scantailor.icns")
  if(EXISTS "${MACOS_ICON_FILE}")
    set_source_files_properties("${MACOS_ICON_FILE}" PROPERTIES
      MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(scantailor PRIVATE "${MACOS_ICON_FILE}")
  endif()

  # Copy Metal shader library into bundle Resources
  if(DEFINED SCANTAILOR_METALLIB)
    add_custom_command(TARGET scantailor POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SCANTAILOR_METALLIB}"
        "$<TARGET_BUNDLE_DIR:scantailor>/Contents/Resources/default.metallib"
      COMMENT "Copying Metal shader library to app bundle..."
    )
  endif()

  # Find macdeployqt
  if(Qt6_FOUND)
    get_target_property(QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
  else()
    get_target_property(QMAKE_EXECUTABLE Qt5::qmake IMPORTED_LOCATION)
  endif()
  get_filename_component(QT_BIN_DIR "${QMAKE_EXECUTABLE}" DIRECTORY)
  find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${QT_BIN_DIR}")

  if(MACDEPLOYQT_EXECUTABLE)
    set(SCANTAILOR_BUNDLE_DIR "$<TARGET_BUNDLE_DIR:scantailor>")
    set(SCANTAILOR_BUNDLE_CONTENTS_DIR "${SCANTAILOR_BUNDLE_DIR}/Contents")
    set(SCANTAILOR_BUNDLE_FRAMEWORKS_DIR "${SCANTAILOR_BUNDLE_CONTENTS_DIR}/Frameworks")
    set(SCANTAILOR_BUNDLE_PLUGINS_DIR "${SCANTAILOR_BUNDLE_CONTENTS_DIR}/PlugIns")
    set(SCANTAILOR_BUNDLE_SIGNATURE_DIR "${SCANTAILOR_BUNDLE_CONTENTS_DIR}/_CodeSignature")
    set(SCANTAILOR_BUNDLE_QT_CONF "${SCANTAILOR_BUNDLE_CONTENTS_DIR}/Resources/qt.conf")
    set(SCANTAILOR_README_PDF "${CMAKE_SOURCE_DIR}/ScanTailor Spectre Readme.pdf")
    set(SCANTAILOR_BUNDLED_README_PDF
        "${SCANTAILOR_BUNDLE_CONTENTS_DIR}/Resources/ScanTailor Spectre Readme.pdf")
    set(SCANTAILOR_ZOTERO_PLUGIN_MANIFEST
        "${CMAKE_SOURCE_DIR}/zotero-plugin/manifest.json")
    set(SCANTAILOR_ZOTERO_PLUGIN_XPI
        "${CMAKE_SOURCE_DIR}/zotero-plugin/dist/st-spectre-loop.xpi")
    set(SCANTAILOR_BUNDLED_ZOTERO_PLUGIN_XPI
        "${SCANTAILOR_BUNDLE_CONTENTS_DIR}/Resources/st-spectre-loop.xpi")
    set(SCANTAILOR_ZOTERO_PLUGIN_BUILD_SCRIPT
        "${CMAKE_SOURCE_DIR}/zotero-plugin/build.sh")
    set(SCANTAILOR_ZOTERO_PLUGIN_VALIDATOR
        "${CMAKE_SOURCE_DIR}/cmake/ValidateZoteroPlugin.cmake")
    set(SCANTAILOR_ADHOC_SIGN_SCRIPT
        "${CMAKE_SOURCE_DIR}/packaging/macos/sign-adhoc.sh")

    add_custom_target(scantailor_bundle
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${SCANTAILOR_BUNDLE_FRAMEWORKS_DIR}"
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${SCANTAILOR_BUNDLE_PLUGINS_DIR}"
      COMMAND ${CMAKE_COMMAND} -E rm -rf "${SCANTAILOR_BUNDLE_SIGNATURE_DIR}"
      COMMAND ${CMAKE_COMMAND} -E rm -f "${SCANTAILOR_BUNDLE_QT_CONF}"
      COMMAND "${MACDEPLOYQT_EXECUTABLE}"
        "${SCANTAILOR_BUNDLE_DIR}"
        -always-overwrite
      COMMAND /bin/bash "${CMAKE_SOURCE_DIR}/packaging/macos/fix-bundle-libs.sh" "${SCANTAILOR_BUNDLE_DIR}"
      ${SCANTAILOR_DOCUMENT_OCR_BUNDLE_COMMAND}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SCANTAILOR_README_PDF}"
        "${SCANTAILOR_BUNDLED_README_PDF}"
      COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_MANIFEST=${SCANTAILOR_ZOTERO_PLUGIN_MANIFEST}"
        "-DXPI_PATH=${SCANTAILOR_ZOTERO_PLUGIN_XPI}"
        "-DBUILD_SCRIPT=${SCANTAILOR_ZOTERO_PLUGIN_BUILD_SCRIPT}"
        -P "${SCANTAILOR_ZOTERO_PLUGIN_VALIDATOR}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SCANTAILOR_ZOTERO_PLUGIN_XPI}"
        "${SCANTAILOR_BUNDLED_ZOTERO_PLUGIN_XPI}"
      COMMAND /bin/bash "${SCANTAILOR_ADHOC_SIGN_SCRIPT}" "${SCANTAILOR_BUNDLE_DIR}"
      DEPENDS scantailor "${SCANTAILOR_README_PDF}"
        "${SCANTAILOR_ZOTERO_PLUGIN_MANIFEST}"
        "${SCANTAILOR_ZOTERO_PLUGIN_VALIDATOR}"
        "${SCANTAILOR_ADHOC_SIGN_SCRIPT}"
      COMMENT "Bundling Qt frameworks and ad-hoc signing the app bundle..."
      VERBATIM
    )

    if(SCANTAILOR_MAC_BUNDLE_POST_BUILD)
      # Run macdeployqt to bundle Qt frameworks
      add_custom_command(TARGET scantailor POST_BUILD
      COMMAND "${MACDEPLOYQT_EXECUTABLE}"
        "$<TARGET_BUNDLE_DIR:scantailor>"
        -always-overwrite
      COMMENT "Running macdeployqt to bundle Qt frameworks..."
      )

      # Fix missing transitive dependencies (brotli libraries)
      # macdeployqt sometimes misses these
      add_custom_command(TARGET scantailor POST_BUILD
      COMMAND /bin/bash "${CMAKE_SOURCE_DIR}/packaging/macos/fix-bundle-libs.sh" "$<TARGET_BUNDLE_DIR:scantailor>"
      COMMENT "Fixing missing library dependencies..."
      )

      # Ad-hoc code signing for local development
      add_custom_command(TARGET scantailor POST_BUILD
      COMMAND codesign --force --deep --sign - "$<TARGET_BUNDLE_DIR:scantailor>"
      COMMENT "Ad-hoc signing the app bundle..."
      )
    endif()
  else()
    message(WARNING "macdeployqt not found. The app bundle will not be self-contained.")
  endif()

  # Install the bundle
  install(TARGETS scantailor BUNDLE DESTINATION ".")
else()
  install(TARGETS scantailor RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if (Qt6_FOUND AND WIN32)
  #manage plugins with windeployqt.
  add_custom_command(
    TARGET scantailor
    POST_BUILD
    COMMAND ${QT_BINDIR}/windeployqt  $<TARGET_FILE:scantailor>
    COMMENT "deploy qt files alongside binary (e.g. plugins)"
  )
  install(CODE "execute_process(COMMAND \"${QT_BINDIR}/windeployqt\" \"\$\{CMAKE_INSTALL_PREFIX\}\")")
endif()

translation_sources(scantailor ${gui_only_sources} ${gui_only_ui_files})
