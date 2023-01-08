cmake_minimum_required(VERSION 3.15)

# -- Setup --
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -- Projects Metadata --
project(TCPServer
        VERSION 0.1.0
        DESCRIPTION "A simple TCP server"
        HOMEPAGE_URL "https://github.com/jsilll/tpc-epoll-server"
        LANGUAGES CXX)

# -- Baseline Flags --
if (MSVC)
    add_compile_options(/W4 /w14640 /WX)
else()
    add_compile_options(-Wall -Wextra -Wshadow -Wunused -Wnon-virtual-dtor
                        -pedantic -Werror)
endif()

# -- When Building Standalone --
if (CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    # -- Support for folders in IDEs --
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)

    # -- Docs only available when building standalone --
    find_package(Doxygen)
    if (DOXYGEN_FOUND)
        add_subdirectory(docs)
    else() 
        message(STATUS "Doxygen not found, not building docs")
    endif()
endif()

# -- Library --
add_library(tcp INTERFACE)
target_include_directories(tcp INTERFACE include)

# -- Executable --
add_subdirectory(app)