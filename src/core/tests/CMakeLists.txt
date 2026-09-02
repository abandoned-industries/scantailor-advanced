set(sources
    main.cpp
    TestAutoColorModePolicy.cpp
    TestBmpInput.cpp
    TestBatchProcessingContext.cpp
    TestColorDetection.cpp
    TestContentSpanFinder.cpp
    TestDeviationProvider.cpp
    TestDurationFormatter.cpp
    TestGifInput.cpp
    TestHeicInput.cpp
    TestJp2Input.cpp
    TestLoadFileTask.cpp
    TestOcrResult.cpp
    TestOcrTextCollector.cpp
    TestPageLayoutOutput.cpp
    TestPdfExporter.cpp
    TestPdfReadError.cpp
    TestPdfReader.cpp
    TestPictureRegionMask.cpp
    TestPnmInput.cpp
    TestPhotoAdjustments.cpp
    TestPlatePrior.cpp
    TestProjectFolder.cpp
    TestProjectPortability.cpp
    TestSmartFilenameOrdering.cpp
    TestTiffCompressionWiring.cpp
    TestWebpInput.cpp
    TestZoteroLoopSidecar.cpp)

add_executable(core_tests ${sources})
target_compile_definitions(core_tests PRIVATE SCANTAILOR_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(
    core_tests
    PRIVATE core Boost::unit_test_framework
    Boost::prg_exec_monitor TIFF::TIFF ${EXTRA_LIBS})
target_include_directories(
    core_tests
    PRIVATE "${CMAKE_SOURCE_DIR}/src/dewarping")

add_test(NAME core_tests COMMAND core_tests --log_level=message)

add_executable(zotero_client_tests TestZoteroClient.cpp)
target_link_libraries(zotero_client_tests PRIVATE core ${EXTRA_LIBS})
add_test(NAME zotero_client_tests COMMAND zotero_client_tests)

add_executable(export_options_layout_tests TestExportOptionsLayout.cpp)
target_link_libraries(export_options_layout_tests PRIVATE export_ Qt::Widgets)
add_test(NAME export_options_layout_tests COMMAND export_options_layout_tests)
set_tests_properties(export_options_layout_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(output_exposure_controls_tests TestOutputExposureControls.cpp)
target_link_libraries(output_exposure_controls_tests PRIVATE output Qt::Widgets)
target_include_directories(output_exposure_controls_tests PRIVATE
    "${CMAKE_BINARY_DIR}/src/core/filters/output/output_autogen/include"
    "${CMAKE_SOURCE_DIR}/src/dewarping")
add_test(NAME output_exposure_controls_tests COMMAND output_exposure_controls_tests)
set_tests_properties(output_exposure_controls_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(project_roundtrip_tests
    RoundtripTestMain.cpp
    RoundtripProjectBuilder.cpp
    TestProjectSaveLoadSave.cpp
    TestProjectXmlTolerance.cpp
    TestFrozenProjectFixtures.cpp)
target_link_libraries(
    project_roundtrip_tests
    PRIVATE core Boost::unit_test_framework
    Boost::prg_exec_monitor Qt::Widgets ${EXTRA_LIBS})
target_include_directories(
    project_roundtrip_tests
    PRIVATE "${CMAKE_SOURCE_DIR}/src/dewarping" "${CMAKE_BINARY_DIR}")
target_compile_definitions(project_roundtrip_tests PRIVATE SCANTAILOR_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME project_roundtrip_tests COMMAND project_roundtrip_tests --log_level=message)
set_tests_properties(project_roundtrip_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Writes the frozen v4 project fixtures (not a test; run manually — see
# fixtures/projects/README.md for the regeneration recipe).
add_executable(project_fixture_writer
    ProjectFixtureWriter.cpp
    RoundtripProjectBuilder.cpp)
target_link_libraries(
    project_fixture_writer
    PRIVATE core Qt::Widgets ${EXTRA_LIBS})
target_include_directories(
    project_fixture_writer
    PRIVATE "${CMAKE_SOURCE_DIR}/src/dewarping")

add_executable(output_golden_tests
    OutputGoldenMain.cpp
    OutputGoldenHarness.cpp
    GoldenSourcePages.cpp
    TestOutputGolden.cpp
    TestOutputGoldenD2Probe.cpp)
target_link_libraries(
    output_golden_tests
    PRIVATE core output Boost::unit_test_framework
    Boost::prg_exec_monitor ${EXTRA_LIBS})
target_include_directories(
    output_golden_tests
    PRIVATE "${CMAKE_SOURCE_DIR}/src/dewarping")
target_compile_definitions(output_golden_tests PRIVATE SCANTAILOR_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME output_golden_tests COMMAND output_golden_tests --log_level=message)

# Writes the frozen synthetic source pages for the golden tests (not a test;
# run manually — see fixtures/golden/README.md).
add_executable(golden_fixture_writer
    GoldenFixtureWriter.cpp
    GoldenSourcePages.cpp)
target_link_libraries(golden_fixture_writer PRIVATE Qt::Gui Qt::Core)

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

add_executable(full_pdf_bw_diagnostic FullPdfBwDiagnostic.cpp)
target_link_libraries(
    full_pdf_bw_diagnostic
    PRIVATE core output ${EXTRA_LIBS})
target_include_directories(
    full_pdf_bw_diagnostic
    PRIVATE "${CMAKE_SOURCE_DIR}/src/dewarping")
