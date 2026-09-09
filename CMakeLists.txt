dfhack_plugin(smooth-movement smooth-movement.cpp LINK_LIBRARIES lua)
target_include_directories(smooth-movement PRIVATE ${SDL2_INCLUDE_DIRS})
target_compile_features(smooth-movement PRIVATE cxx_std_17)

# The unit tests and the render property fuzzer, built on request only (tests/run.sh is the
# usual way to run them): each tests/<source>.cpp becomes the target smooth-movement-<name>.
set(SMOOTH_MOVEMENT_TESTS
    test:test_visual_animation_manager
    render-test:test_frame_render
    view-context-test:test_view_context
    free-camera-test:test_free_camera
    sdl-canvas-test:test_sdl_canvas
    render-fuzz:render_fuzz)
foreach(entry IN LISTS SMOOTH_MOVEMENT_TESTS)
    string(REPLACE ":" ";" entry "${entry}")
    list(GET entry 0 name)
    list(GET entry 1 source)
    add_executable(smooth-movement-${name} EXCLUDE_FROM_ALL tests/${source}.cpp)
    target_include_directories(smooth-movement-${name}
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_features(smooth-movement-${name} PRIVATE cxx_std_17)
endforeach()
