dfhack_plugin(smooth-movement smooth-movement.cpp LINK_LIBRARIES lua)
target_include_directories(smooth-movement PRIVATE ${SDL2_INCLUDE_DIRS})
target_compile_features(smooth-movement PRIVATE cxx_std_17)

add_executable(
    smooth-movement-test
    EXCLUDE_FROM_ALL
    tests/test_visual_animation_manager.cpp)
target_include_directories(smooth-movement-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(smooth-movement-test PRIVATE cxx_std_17)

add_executable(
    smooth-movement-render-test
    EXCLUDE_FROM_ALL
    tests/test_frame_render.cpp)
target_include_directories(smooth-movement-render-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(smooth-movement-render-test PRIVATE cxx_std_17)
