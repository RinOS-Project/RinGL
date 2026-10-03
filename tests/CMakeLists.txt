set(RINGL_TEST_NAMES
    buffer_storage
    buffer
    context
    cube_map
    draw_buffers_gpu
    embedding_clear
    first_triangle
    framebuffer_gpu
    framebuffer_objects
    framebuffer
    glsl_parser
    pipeline_cache
    program_gpu
    program
    reflection
    shader_ir
    shader
    srgb
    state
    sync
    texture_gpu
    texture_ir
    texture
    textured_draw
    vertex_array
    vertex_attrib)

foreach(test_name IN LISTS RINGL_TEST_NAMES)
    add_executable(ringl-${test_name}-test ${test_name}_test.c)
    target_include_directories(ringl-${test_name}-test
                               PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
    target_link_libraries(ringl-${test_name}-test PRIVATE RinGL::RinGL)
    if(MSVC)
        target_compile_options(ringl-${test_name}-test PRIVATE /W4 /WX)
    else()
        target_compile_options(ringl-${test_name}-test
                               PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
    add_test(NAME ringl-${test_name} COMMAND ringl-${test_name}-test)
endforeach()
