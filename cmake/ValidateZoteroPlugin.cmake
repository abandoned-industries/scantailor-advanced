if(NOT DEFINED SOURCE_MANIFEST OR NOT DEFINED XPI_PATH OR NOT DEFINED BUILD_SCRIPT)
  message(FATAL_ERROR "Zotero plugin validation requires SOURCE_MANIFEST, XPI_PATH, and BUILD_SCRIPT.")
endif()

if(NOT EXISTS "${XPI_PATH}")
  message(FATAL_ERROR
    "Zotero plugin XPI is missing: ${XPI_PATH}\n"
    "Run ${BUILD_SCRIPT}.")
endif()

file(READ "${SOURCE_MANIFEST}" SOURCE_MANIFEST_JSON)
string(REGEX MATCH
  "\"version\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\""
  SOURCE_VERSION_MATCH "${SOURCE_MANIFEST_JSON}")
set(SOURCE_PLUGIN_VERSION "${CMAKE_MATCH_1}")
if(NOT SOURCE_VERSION_MATCH)
  message(FATAL_ERROR
    "Could not read the Zotero plugin version from ${SOURCE_MANIFEST}.\n"
    "Run ${BUILD_SCRIPT}.")
endif()

execute_process(
  COMMAND /usr/bin/unzip -p "${XPI_PATH}" manifest.json
  RESULT_VARIABLE UNZIP_RESULT
  OUTPUT_VARIABLE XPI_MANIFEST_JSON
  ERROR_VARIABLE UNZIP_ERROR
)
if(NOT UNZIP_RESULT EQUAL 0)
  message(FATAL_ERROR
    "Could not read manifest.json from ${XPI_PATH}: ${UNZIP_ERROR}\n"
    "Run ${BUILD_SCRIPT}.")
endif()

string(REGEX MATCH
  "\"version\"[ \t\r\n]*:[ \t\r\n]*\"([^\"]+)\""
  XPI_VERSION_MATCH "${XPI_MANIFEST_JSON}")
set(XPI_PLUGIN_VERSION "${CMAKE_MATCH_1}")
if(NOT XPI_VERSION_MATCH)
  message(FATAL_ERROR
    "Could not read the Zotero plugin version from ${XPI_PATH}.\n"
    "Run ${BUILD_SCRIPT}.")
endif()

if(NOT XPI_PLUGIN_VERSION STREQUAL SOURCE_PLUGIN_VERSION)
  message(FATAL_ERROR
    "Zotero plugin XPI is stale (XPI ${XPI_PLUGIN_VERSION}, source ${SOURCE_PLUGIN_VERSION}).\n"
    "Run ${BUILD_SCRIPT}.")
endif()
