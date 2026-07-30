set(sources
    main.cpp
    TestAutoColorModePolicy.cpp
    TestBatchProcessingContext.cpp
    TestColorDetection.cpp
    TestContentSpanFinder.cpp
    TestDurationFormatter.cpp
    TestOcrResult.cpp
    TestPageLayoutOutput.cpp
    TestPdfExporter.cpp
    TestPdfReader.cpp
    TestPictureRegionMask.cpp
    TestPlatePrior.cpp
    TestProjectFolder.cpp
    TestProjectPortability.cpp
    TestSmartFilenameOrdering.cpp)

add_executable(core_tests ${sources})
target_compile_definitions(core_tests PRIVATE SCANTAILOR_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(
    core_tests
    PRIVATE core Boost::unit_test_framework
    Boost::prg_exec_monitor ${EXTRA_LIBS})
target_include_directories(
    core_tests
    PRIVATE "${CMAKE_SOURCE_DIR}/src/dewarping")

add_test(NAME core_tests COMMAND core_tests --log_level=message)

add_executable(color_detection_diagnostic ColorDetectionDiagnostic.cpp)
target_link_libraries(color_detection_diagnostic PRIVATE core ${LEPTONICA_LIBRARIES})
target_include_directories(color_detection_diagnostic PRIVATE ${LEPTONICA_INCLUDE_DIRS})

add_executable(geometry_diagnostic GeometryDiagnostic.cpp)
target_link_libraries(
    geometry_diagnostic
    PRIVATE core page_split deskew select_content ${EXTRA_LIBS})

add_executable(mixed_output_diagnostic MixedOutputDiagnostic.cpp)
target_link_libraries(
    mixed_output_diagnostic
    PRIVATE core output ${EXTRA_LIBS})
target_include_directories(
    mixed_output_diagnostic
    PRIVATE "${CMAKE_SOURCE_DIR}/src/dewarping")

add_executable(margins_output_diagnostic MarginsOutputDiagnostic.cpp)
target_link_libraries(
    margins_output_diagnostic
    PRIVATE core page_layout output ${EXTRA_LIBS})
target_include_directories(
    margins_output_diagnostic
    PRIVATE "${CMAKE_SOURCE_DIR}/src/dewarping")
