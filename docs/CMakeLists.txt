# -- Setting up Doxygen --
set(DOXYGEN_EXTRACT_ALL YES)
set(DOXYGEN_GENERATE_HTML YES)
set(DOXYGEN_GENERATE_LATEX NO)
set(DOXYGEN_BUILTIN_STL_SUPPORT YES)
set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# -- RT Public Headers --
file(GLOB LIB_TCP_FILES ../include/tcp/*.h)

# -- Building Documentation --
doxygen_add_docs(docs ${LIB_TCP_FILES} COMMENT "Generating API documentation for TCP Library")

