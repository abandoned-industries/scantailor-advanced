# Metal GPU Acceleration module
# Only built on Apple platforms

if(APPLE)
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_program(XCRUN_EXECUTABLE xcrun REQUIRED)

    # Metal shader compilation
    set(METAL_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gauss_blur.metal
    )
    set(METALLIB_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/default.metallib)

    execute_process(
        COMMAND ${XCRUN_EXECUTABLE} -sdk macosx -find metal
        RESULT_VARIABLE METAL_FIND_RESULT
        OUTPUT_VARIABLE METAL_EXECUTABLE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${XCRUN_EXECUTABLE} -sdk macosx -find metallib
        RESULT_VARIABLE METALLIB_FIND_RESULT
        OUTPUT_VARIABLE METALLIB_EXECUTABLE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )

    set(SCANTAILOR_METAL_SHADERS_AVAILABLE FALSE)
    if(METAL_FIND_RESULT EQUAL 0 AND METALLIB_FIND_RESULT EQUAL 0)
        set(SCANTAILOR_METAL_SHADERS_AVAILABLE TRUE)
    else()
        message(WARNING "Metal shader compiler not available; building without bundled Metal shaders.")
    endif()

    if(SCANTAILOR_METAL_SHADERS_AVAILABLE)
        # Compile .metal -> .air -> .metallib (compile all shaders then link)
        add_custom_command(
            OUTPUT ${METALLIB_OUTPUT}
            COMMAND ${METAL_EXECUTABLE} -c ${CMAKE_CURRENT_SOURCE_DIR}/shaders/gauss_blur.metal -o ${CMAKE_CURRENT_BINARY_DIR}/gauss_blur.air
            COMMAND ${METALLIB_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/gauss_blur.air -o ${METALLIB_OUTPUT}
            DEPENDS ${METAL_SOURCES}
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Compiling Metal shaders"
        )

        # Custom target to ensure metallib is built
        add_custom_target(metal_shaders DEPENDS ${METALLIB_OUTPUT})
    endif()

    set(sources
        MetalContext.h
        MetalContext.mm
        MetalGaussBlur.h
        MetalGaussBlur.mm
        MetalLifecycle.h
        MetalLifecycle.mm
    )

    add_library(acceleration STATIC ${sources})
    if(TARGET metal_shaders)
        add_dependencies(acceleration metal_shaders)
    endif()

    target_link_libraries(acceleration
        PUBLIC ${METAL_FRAMEWORK}
        PUBLIC ${FOUNDATION_FRAMEWORK}
        PUBLIC ${APPKIT_FRAMEWORK}
    )

    target_include_directories(acceleration PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

    if(SCANTAILOR_METAL_SHADERS_AVAILABLE)
        # Set metallib as a resource to be copied to app bundle
        set(SCANTAILOR_METALLIB ${METALLIB_OUTPUT} CACHE INTERNAL "Path to compiled Metal shader library")
    else()
        unset(SCANTAILOR_METALLIB CACHE)
    endif()
endif()
