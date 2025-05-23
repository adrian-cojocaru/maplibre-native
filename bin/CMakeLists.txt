add_executable(
    mbgl-cache
    ${PROJECT_SOURCE_DIR}/bin/cache.cpp
)

target_link_libraries(
    mbgl-cache
    PRIVATE
        MapLibreNative::Base
        MapLibreNative::Base::Extras::args
        mbgl-compiler-options
        mbgl-core
)

add_executable(
    mbgl-offline
    ${PROJECT_SOURCE_DIR}/bin/offline.cpp
)

target_link_libraries(
    mbgl-offline
    PRIVATE MapLibreNative::Base::Extras::args mbgl-compiler-options mbgl-core
)

add_executable(
    mbgl-render
    ${PROJECT_SOURCE_DIR}/bin/render.cpp
)

target_link_libraries(
    mbgl-render
    PRIVATE MapLibreNative::Base::Extras::args mbgl-compiler-options mbgl-core
)

if(WIN32)
    find_package(libuv REQUIRED)

    target_link_libraries(
        mbgl-cache PRIVATE $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    )

    target_link_libraries(
        mbgl-offline PRIVATE $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    )

    target_link_libraries(
        mbgl-render PRIVATE $<IF:$<TARGET_EXISTS:libuv::uv_a>,libuv::uv_a,libuv::uv>
    )
endif()

install(TARGETS mbgl-offline mbgl-render RUNTIME DESTINATION bin)

# FIXME: CI must have a valid token
#
# add_test(NAME mbgl-offline-tool-test COMMAND mbgl-offline -s maptiler://maps/hybrid --maxZoom=0 WORKING_DIRECTORY
# ${PROJECT_SOURCE_DIR} )
#
# add_test(NAME mbgl-render-tool-test COMMAND mbgl-render WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} )

if(MLN_WITH_OPENGL)
    target_compile_definitions(mbgl-render PRIVATE "MLN_RENDER_BACKEND_OPENGL=1")
endif()
