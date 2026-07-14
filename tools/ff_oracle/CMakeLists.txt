set(FF_ORACLE_ZLIB_SOURCES
    "${DEPS_DIR}/zlib/adler32.c"
    "${DEPS_DIR}/zlib/infblock.c"
    "${DEPS_DIR}/zlib/infcodes.c"
    "${DEPS_DIR}/zlib/inffast.c"
    "${DEPS_DIR}/zlib/inflate.c"
    "${DEPS_DIR}/zlib/inftrees.c"
    "${DEPS_DIR}/zlib/infutil.c"
    "${DEPS_DIR}/zlib/zutil.c"
)

add_library(bmk4-ff-oracle-zlib STATIC ${FF_ORACLE_ZLIB_SOURCES})
target_include_directories(bmk4-ff-oracle-zlib PRIVATE "${DEPS_DIR}/zlib")

add_executable(bmk4-ff-oracle ff_oracle.cpp)
target_compile_features(bmk4-ff-oracle PRIVATE cxx_std_20)
target_include_directories(bmk4-ff-oracle PRIVATE "${DEPS_DIR}")
target_link_libraries(bmk4-ff-oracle PRIVATE bmk4-ff-oracle-zlib)

if(MSVC)
    target_compile_options(bmk4-ff-oracle-zlib PRIVATE /W0)
    target_compile_options(bmk4-ff-oracle PRIVATE /W4 /WX /permissive-)
    set_property(TARGET bmk4-ff-oracle-zlib PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET bmk4-ff-oracle PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
