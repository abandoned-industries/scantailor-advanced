cmake_minimum_required(VERSION 3.16 FATAL_ERROR)

# Disable in-source builds.
if ("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
  message(FATAL_ERROR
      "FATAL: In-source builds are not allowed."
      " You should create a separate directory for build files.")
endif()

project("ScanTailor Spectre")

if (NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  message(FATAL_ERROR "ScanTailor Spectre is supported on macOS only.")
endif()

if (APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING "Minimum macOS deployment target." FORCE)
endif()

# Set compiler flags.
set(CMAKE_CXX_STANDARD 17)

if (NOT DEFAULT_COMPILER_FLAGS_IS_SET)
  if (MSVC)
    # Disable checked iterators for extra performance.
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /D_SECURE_SCL=0")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /D_SECURE_SCL=0")

    if (DEBUG_CLI)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /DDEBUG_CLI")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /DDEBUG_CLI")
    endif()
  endif()

  if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -Werror=return-type -Wno-unused")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror=return-type -Wno-unused")
  endif()

  # Apple Silicon / macOS optimizations
  if (APPLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -Werror=return-type -Wno-unused")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror=return-type -Wno-unused")
  endif()

  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}"
      CACHE STRING "Common C flags for all build configurations." FORCE)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}"
      CACHE STRING "Common C++ flags for all build configurations." FORCE)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}"
      CACHE STRING "Common link flags for all build configurations." FORCE)

  set(DEFAULT_COMPILER_FLAGS_IS_SET YES CACHE INTERNAL "" FORCE)
endif()

if (UNIX)
  option(CMAKE_POSITION_INDEPENDENT_CODE "" ON)
  option(CMAKE_INSTALL_RPATH_USE_LINK_PATH "" ON)
endif()

include(cmake/SetDefaultBuildType.cmake)
include(cmake/UpdateTranslations.cmake)
include(cmake/ListItemsPrepend.cmake)
if (WIN32)
  include(cmake/CopyToBuildDir.cmake)
  include(cmake/AddDynamicLibraryLocations.cmake)
endif()

st_set_default_build_type(Release)

if (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
endif()
if (NOT CMAKE_INSTALL_BINDIR)
  if (WIN32)
    set(CMAKE_INSTALL_BINDIR ".")
  else()
    set(CMAKE_INSTALL_BINDIR "bin")
  endif()
endif()

#=================================== Dependencies ===================================#

if (WIN32)
  macro(real_path_find_glob Var Glob)
    file(GLOB ${Var} "${Glob}")
    if (${Var})
      list(GET ${Var} -1 ${Var})
      get_filename_component("${Var}" "${${Var}}" REALPATH)
    endif()
  endmacro()

  get_filename_component(lib_dir "${PROJECT_SOURCE_DIR}/../libs" REALPATH)
  set(DEPENDENCIES_ROOT "${lib_dir}" CACHE PATH
      "The assumed directory where all the required libraries are installed.")
  if (EXISTS "${DEPENDENCIES_ROOT}")
    real_path_find_glob(jpeg_dir_assumed "${DEPENDENCIES_ROOT}/jpeg-[0-9]*")
    real_path_find_glob(zlib_dir_assumed "${DEPENDENCIES_ROOT}/zlib-[0-9]*.[0-9]*.[0-9]*")
    real_path_find_glob(png_dir_assumed "${DEPENDENCIES_ROOT}/libpng-[0-9]*.[0-9]*.[0-9]*")
    real_path_find_glob(tiff_dir_assumed "${DEPENDENCIES_ROOT}/tiff-[0-9]*.[0-9]*.[0-9]*")
    get_filename_component(qt_dir_assumed "${DEPENDENCIES_ROOT}/Qt" REALPATH)
    get_filename_component(boost_dir_assumed "${DEPENDENCIES_ROOT}/Boost" REALPATH)
  endif()
  set(JPEG_INSTALL_PREFIX "${jpeg_dir_assumed}" CACHE PATH "Prefix where jpeg is installed")
  set(ZLIB_INSTALL_PREFIX "${zlib_dir_assumed}" CACHE PATH "Prefix where zlib is installed")
  set(PNG_INSTALL_PREFIX "${png_dir_assumed}" CACHE PATH "Prefix where png is installed")
  set(TIFF_INSTALL_PREFIX "${tiff_dir_assumed}" CACHE PATH "Prefix where tiff is installed")
  set(QT_INSTALL_PREFIX "${qt_dir_assumed}" CACHE PATH "Prefix where Qt is installed")
  set(BOOST_ROOT "${boost_dir_assumed}" CACHE PATH "Prefix where Boost is installed")

  macro(list_append_existing_path Var Path)
    get_filename_component(_real_path "${Path}" REALPATH)
    if (EXISTS "${_real_path}")
      list(APPEND "${Var}" "${_real_path}")
    endif()
  endmacro()

  foreach(_prefix
      "${JPEG_INSTALL_PREFIX}" "${ZLIB_INSTALL_PREFIX}" "${PNG_INSTALL_PREFIX}"
      "${TIFF_INSTALL_PREFIX}" "${QT_INSTALL_PREFIX}" "${BOOST_ROOT}")
    list_append_existing_path(CMAKE_PREFIX_PATH "${_prefix}")
    list_append_existing_path(CMAKE_INCLUDE_PATH "${_prefix}")
    list_append_existing_path(CMAKE_LIBRARY_PATH "${_prefix}/stage/lib")
  endforeach()
  list_append_existing_path(CMAKE_INCLUDE_PATH "${TIFF_INSTALL_PREFIX}/libtiff")
endif()

#=================================== JPEG ===================================#

find_package(JPEG REQUIRED)

if (WIN32)
  add_dynamic_library_locations(JPEG::JPEG "libjpeg-62.dll")
endif()

#=================================== ZLIB ===================================#

find_package(ZLIB REQUIRED)

if (WIN32)
  add_dynamic_library_locations(ZLIB::ZLIB)
endif()

#================================== PNG ==================================#

find_package(PNG REQUIRED)

if (WIN32)
  add_dynamic_library_locations(PNG::PNG "libpng16.dll")
endif()

#=================================== TIFF ===================================#

find_package(TIFF REQUIRED)

if (WIN32)
  add_dynamic_library_locations(TIFF::TIFF)
  set_target_properties(
      TIFF::TIFF PROPERTIES
      INTERFACE_COMPILE_DEFINITIONS USE_LIBTIFF_DLL)
endif()

#================================ Leptonica ===============================#

find_package(PkgConfig REQUIRED)
pkg_check_modules(LEPTONICA REQUIRED lept)
link_directories(${LEPTONICA_LIBRARY_DIRS})

#================================= Boost ================================#

#set(Boost_NO_WARN_NEW_VERSIONS ON)

if (WIN32)
  real_path_find_glob(boost_include_dir_hints "${BOOST_ROOT}/include/boost-[_0-9]*/boost")
  list(APPEND boost_include_dir_hints "${BOOST_ROOT}/include/boost" "${BOOST_ROOT}/boost")
  find_path(
      BOOST_INCLUDEDIR config.hpp
      HINTS ${boost_include_dir_hints}
      DOC "Boost include directory.")

  set(Boost_USE_STATIC_LIBS ON)
endif()
set(Boost_USE_MULTITHREADED ON)

find_package(Boost 1.60 COMPONENTS unit_test_framework prg_exec_monitor REQUIRED)

include_directories(${Boost_INCLUDE_DIRS})
add_definitions(-DBOOST_MULTI_INDEX_DISABLE_SERIALIZATION)
if (NOT Boost_USE_STATIC_LIBS)
  if (WIN32)
    add_dynamic_library_locations(Boost::unit_test_framework Boost::prg_exec_monitor)
  endif()
  add_definitions(-DBOOST_TEST_DYN_LINK)
endif()

#=================================== Qt ===================================#

find_package(
    Qt6 6.4 REQUIRED
    COMPONENTS Core Gui Widgets Xml Network LinguistTools OpenGL Svg OpenGLWidgets WebEngineWidgets WebChannel
    CONFIG)
set(QT_BINDIR "${QT6_INSTALL_PREFIX}/bin")

# Use modern Qt:: namespace targets
set(Qt_Core_lib Qt::Core)
set(Qt_Gui_lib Qt::Gui)
set(Qt_Network_lib Qt::Network)
set(Qt_OpenGL_lib Qt::OpenGL)
set(Qt_Svg_lib Qt::Svg)
set(Qt_Widgets_lib Qt::Widgets)
set(Qt_Xml_lib Qt::Xml)
set(Qt_OpenGLWidgets_lib Qt::OpenGLWidgets)


#=================================== Threads ===================================#

if (UNIX)
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
  link_libraries(Threads::Threads)
endif()

#=================================== LibHaru (PDF compression) ===================================#

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
find_package(LibHaru)
if(LibHaru_FOUND)
  message(STATUS "LibHaru found - PDF export will use optimized compression")
  add_definitions(-DHAVE_LIBHARU)
else()
  message(STATUS "LibHaru not found - PDF export will use Qt (larger files)")
endif()

#=================================== System ===================================#

set(EXTRA_LIBS "")
if (WIN32)
  list(APPEND EXTRA_LIBS winmm imm32 ws2_32 ole32 oleaut32 uuid gdi32 comdlg32 winspool)
endif()

#=================================== Main ===================================#

set(CMAKE_AUTOMOC ON)

if (WIN32 AND MSVC)
  add_definitions(-DNOMINMAX)
endif()

enable_testing()

# Prepare config.h
option(PORTABLE_VERSION "Whether to build the portable version." ON)
if (PORTABLE_VERSION)
  set(PORTABLE_CONFIG_DIR "config")
endif()
set(APPLICATION_NAME "scantailor-spectre")
if (WIN32)
  set(TRANSLATION_DIRS ".:translations")
  set(PLUGIN_DIRS ".")
else()
  set(TRANSLATION_DIRS ".:translations:../share/${APPLICATION_NAME}/translations")
  set(PLUGIN_DIRS ".:../lib/${APPLICATION_NAME}")
endif()
configure_file(config.h.in "${CMAKE_BINARY_DIR}/config.h" @ONLY)
file(READ "${CMAKE_BINARY_DIR}/config.h" config_h_contents)
set(enable_developer_version_default OFF)
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(enable_developer_version_default ON)
endif()
option(DEVELOPER_VERSION "Whether to enable the internal debug features." ${enable_developer_version_default})
if (NOT DEVELOPER_VERSION)
  string(REGEX REPLACE "(#define[ \\t]+ENABLE_DEBUG_FEATURES[^\n]*)" "//\\1" config_h_contents ${config_h_contents})
endif()
file(WRITE "${CMAKE_BINARY_DIR}/config.h" ${config_h_contents})
include_directories(BEFORE "${CMAKE_BINARY_DIR}")

# Extract VERSION and VERSION_QUAD from version.h
file(READ "${PROJECT_SOURCE_DIR}/version.h.in" version_h_contents)
string(
    REGEX REPLACE
    ".*#define[ \\t]+VERSION[ \\t]+\"([^\"]*)\".*"
    "\\1" VERSION "${version_h_contents}")
if ("${VERSION}" STREQUAL "${version_h_contents}")
  message(FATAL_ERROR "Failed to extract VERSION from version.h")
endif()
# VERSION_QUAD must be either empty or be in the form of X.Y.Z.Y
string(
    REGEX REPLACE
    ".*#define[ \\t]+VERSION_QUAD[ \\t]+\"(([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)?)\".*"
    "\\1" VERSION_QUAD "${version_h_contents}")
if ("${VERSION_QUAD}" STREQUAL "${version_h_contents}")
  message(FATAL_ERROR "Failed to extract VERSION_QUAD from version.h")
endif()
# Generate version.h with build timestamp (runs every build)
add_custom_target(generate_version_h ALL
    COMMAND ${CMAKE_COMMAND}
        -DVERSION_H_IN="${PROJECT_SOURCE_DIR}/version.h.in"
        -DVERSION_H_OUT="${CMAKE_BINARY_DIR}/version.h"
        -P "${PROJECT_SOURCE_DIR}/cmake/GenerateVersionH.cmake"
    COMMENT "Generating version.h with build timestamp"
)
if (VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$"
    OR VERSION MATCHES "^([0-9]+)\\.([0-9]+)[A-Za-z]+([0-9]+)$")
  set(APPLICATION_VERSION_MAJOR "${CMAKE_MATCH_1}")
  set(APPLICATION_VERSION_MINOR "${CMAKE_MATCH_2}")
  set(APPLICATION_VERSION_PATCH "${CMAKE_MATCH_3}")
else()
  message(FATAL_ERROR "Unsupported application version format: ${VERSION}")
endif()

set(SCANTAILOR_RESOURCES_DIR "${CMAKE_SOURCE_DIR}/src/resources")
set(SCANTAILOR_TRANSLATIONS_DIR "${CMAKE_SOURCE_DIR}/src/translations")

add_subdirectory(src)

# Translations
file(GLOB translation_files "${SCANTAILOR_TRANSLATIONS_DIR}/scantailor_*.ts")

finalize_translations(scantailor ${translation_files})
update_translations_target(update_translations scantailor)

list(FILTER translation_files EXCLUDE REGEX ".*untranslated.ts")
qt_add_translation(qm_files ${translation_files})
add_custom_target(compile_translations ALL DEPENDS ${qm_files})
if (WIN32)
  install(FILES ${qm_files} DESTINATION "${CMAKE_INSTALL_BINDIR}/translations")
else()
  install(FILES ${qm_files} DESTINATION "share/${APPLICATION_NAME}/translations")
endif()

# Install extra files.
if (WIN32)
  macro (add_runtime_libs_to_install Configuration Libs)
    set(configurations "${Configuration}")
    if (${configurations} MATCHES "ALL")
      set(configurations "DEBUG;RELEASE")
    endif()
    foreach (_config ${configurations})
      foreach (_lib ${Libs})
        if (EXISTS "${_lib}")
          list(APPEND "ADDITIONAL_RUNTIME_LIBS_${_config}" "${_lib}")
        endif()
      endforeach()
    endforeach()
  endmacro()

  macro (set_if_undefined Var Value)
    if (NOT ${Var})
      set(${Var} "${Value}")
    endif()
  endmacro()

  # Install the DLLs of the dependencies.
  set(dep_libs
      JPEG::JPEG PNG::PNG ZLIB::ZLIB TIFF::TIFF
      Qt::Core Qt::Gui Qt::Widgets Qt::Xml
      Qt::Network Qt::OpenGL Qt::Svg Qt::OpenGLWidgets)

  foreach (_target ${dep_libs})
    get_target_property(target_type ${_target} TYPE)
    if (target_type STREQUAL "SHARED_LIBRARY")
      set(dynlib_location_property LOCATION)
    elseif (target_type STREQUAL "UNKNOWN_LIBRARY")
      set(dynlib_location_property IMPORTED_DYNLIB)
    else()
      continue()
    endif()

    get_target_property(release_loc ${_target} ${dynlib_location_property}_RELEASE)
    get_target_property(debug_loc ${_target} ${dynlib_location_property}_DEBUG)
    set_if_undefined(debug_loc "${release_loc}")
    add_runtime_libs_to_install(RELEASE "${release_loc}")
    add_runtime_libs_to_install(DEBUG "${debug_loc}")
  endforeach()

  copy_to_build_dir(${ADDITIONAL_RUNTIME_LIBS_RELEASE} CONFIGURATIONS Release MinSizeRel RelWithDebInfo)
  copy_to_build_dir(${ADDITIONAL_RUNTIME_LIBS_DEBUG} CONFIGURATIONS Debug)

  # Generate the target that will actually do the copying.
  generate_copy_to_build_dir_target(copy_to_build_dir)

  install(PROGRAMS ${ADDITIONAL_RUNTIME_LIBS_RELEASE} TYPE BIN CONFIGURATIONS Release MinSizeRel RelWithDebInfo)
  install(PROGRAMS ${ADDITIONAL_RUNTIME_LIBS_DEBUG} TYPE BIN CONFIGURATIONS Debug)
endif()

if (UNIX)
  install(FILES "${SCANTAILOR_RESOURCES_DIR}/unix/scantailor.desktop"
      DESTINATION "share/applications")
  install(FILES "${SCANTAILOR_RESOURCES_DIR}/appicon.svg"
      DESTINATION "share/icons/hicolor/scalable/apps"
      RENAME "ScanTailor.svg")
  install(FILES "${SCANTAILOR_RESOURCES_DIR}/unix/mime/scantailor-project.xml"
      DESTINATION "share/mime/packages")
endif()

if (WIN32 AND MSVC)
  set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
endif()
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION "${CMAKE_INSTALL_BINDIR}")
include(InstallRequiredSystemLibraries)

# Packaging
set(CPACK_PACKAGE_NAME "${APPLICATION_NAME}")
set(CPACK_PACKAGE_VERSION_MAJOR "${APPLICATION_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${APPLICATION_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${APPLICATION_VERSION_PATCH}")
set(CPACK_PACKAGE_VENDOR "4lex4 <4lex49@zoho.com>")
set(CPACK_PACKAGE_CONTACT "${CPACK_PACKAGE_VENDOR}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Interactive post-processing tool for scanned pages.")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_EXECUTABLES "scantailor;${CMAKE_PROJECT_NAME}")
set(CPACK_CREATE_DESKTOP_LINKS "scantailor")
if (WIN32)
  set(CPACK_NSIS_INSTALLED_ICON_NAME "scantailor.exe")
  set(CPACK_NSIS_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
  set(CPACK_NSIS_DISPLAY_NAME "${CMAKE_PROJECT_NAME} ${VERSION}")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY "${CMAKE_BINARY_DIR}")
  set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CMAKE_PROJECT_NAME}")
  set(CPACK_NSIS_CREATE_ICONS_EXTRA
      "CreateShortCut \\\"$DESKTOP\\\\${CMAKE_PROJECT_NAME}.lnk\\\" \\\"$INSTDIR\\\\scantailor.exe\\\"")
  set(CPACK_NSIS_DELETE_ICONS_EXTRA
      "Delete \\\"$DESKTOP\\\\${CMAKE_PROJECT_NAME}.lnk\\\"")
endif()
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${APPLICATION_NAME}-${VERSION}")
set(CPACK_SOURCE_IGNORE_FILES
    "/\\\\.svn/"
    "/\\\\.git/"
    "~$"
    "\\\\.pcs$"
    "TODO.txt"
    "CMakeLists.txt.user"
    "/doxygen/"
    "${CMAKE_BINARY_DIR}")
include(CPack)

# uninstall target
if (NOT TARGET uninstall)
  configure_file(
      "${CMAKE_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
      "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake"
      @ONLY)
  add_custom_target(
      uninstall
      COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_BINARY_DIR}/cmake_uninstall.cmake")
endif()
