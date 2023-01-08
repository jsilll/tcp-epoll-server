add_executable(server main.cpp handler.h)
target_link_libraries(server tcp)

# -- Debug and Release Flags --
if (CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_options(server PUBLIC -g -O0)
    target_compile_definitions(server PUBLIC DEBUG)
elseif (CMAKE_BUILD_TYPE MATCHES Release)
    target_compile_options(server PUBLIC -O3)
endif()