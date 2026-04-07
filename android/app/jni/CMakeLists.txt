cmake_minimum_required( VERSION 3.24 )

project( CataclysmBNAndroid C CXX )

if( NOT ANDROID )
    message( FATAL_ERROR "This CMake project is only for Android externalNativeBuild." )
endif()

set( CMAKE_C_STANDARD 17 )
set( CMAKE_C_STANDARD_REQUIRED ON )
set( CMAKE_CXX_STANDARD 23 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )

if( NOT DEFINED ANDROID_ABI )
    set( ANDROID_ABI "${CMAKE_ANDROID_ARCH_ABI}" )
endif()

set( jni_root "${CMAKE_CURRENT_SOURCE_DIR}" )
set( src_root "${jni_root}/src" )
set( sqlite_zip_path "${CMAKE_BINARY_DIR}/sqlite-amalgamation-3480000.zip" )
set( sqlite_extract_root "${CMAKE_BINARY_DIR}/sqlite-amalgamation-3480000" )

if( NOT EXISTS "${jni_root}/sqlite3/sqlite3.c" )
    file( DOWNLOAD
        "https://www.sqlite.org/2025/sqlite-amalgamation-3480000.zip"
        "${sqlite_zip_path}"
        SHOW_PROGRESS
    )
    file( ARCHIVE_EXTRACT INPUT "${sqlite_zip_path}" DESTINATION "${CMAKE_BINARY_DIR}" )
    file( MAKE_DIRECTORY "${jni_root}/sqlite3" )
    file( COPY "${sqlite_extract_root}/" DESTINATION "${jni_root}/sqlite3" )
endif()

file( GLOB cata_sources CONFIGURE_DEPENDS "${src_root}/*.cpp" )
file( GLOB lua_sources CONFIGURE_DEPENDS "${src_root}/lua/*.c" )

function( add_prebuilt_shared target_name library_dir library_name )
    add_library( ${target_name} SHARED IMPORTED )
    set_target_properties( ${target_name} PROPERTIES
        IMPORTED_LOCATION "${jni_root}/${library_dir}/${ANDROID_ABI}/lib${library_name}.so"
    )
endfunction()

add_library( libsqlite3 SHARED "${jni_root}/sqlite3/sqlite3.c" )
target_include_directories( libsqlite3 PUBLIC "${jni_root}/sqlite3" )
target_compile_definitions( libsqlite3 PRIVATE
    HAVE_USLEEP=1
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_RTREE
    SQLITE_ENABLE_FTS3
    SQLITE_ENABLE_BATCH_ATOMIC_WRITE
    SQLITE_TEMP_STORE=3
    HAVE_CONFIG_H
    KHTML_NO_EXCEPTIONS
    GKWQ_NO_JAVA
    NO_SUPPORT_JS_BINDING
    QT_NO_WHEELEVENT
    KHTML_NO_XBL
    HAVE_STRCHRNUL=0
    SQLITE_USE_URI=1
)
if( ANDROID_ABI STREQUAL "armeabi-v7a" )
    target_compile_options( libsqlite3 PRIVATE -DPACKED=__attribute__\(\(packed\)\) )
else()
    target_compile_options( libsqlite3 PRIVATE -DPACKED= )
endif()
target_compile_options( libsqlite3 PRIVATE
    -U__APPLE__
    -Wno-unused-parameter
    -Wno-int-to-pointer-cast
    -Wno-uninitialized
    -Wno-parentheses
)
target_link_libraries( libsqlite3 PRIVATE dl log )

add_prebuilt_shared( SDL2 SDL2 SDL2 )
add_prebuilt_shared( SDL2_image SDL2_image SDL2_image )
add_prebuilt_shared( SDL2_mixer SDL2_mixer SDL2_mixer )
add_prebuilt_shared( SDL2_ttf SDL2_ttf SDL2_ttf )
add_prebuilt_shared( hidapi libhidapi hidapi )
add_prebuilt_shared( mpg123 mpg-123 mpg123 )

add_library( main SHARED ${cata_sources} ${lua_sources} )
target_include_directories( main PRIVATE
    "${src_root}"
    "${src_root}/lua"
    "${jni_root}/SDL2/include"
    "${jni_root}/libhidapi"
    "${jni_root}/sqlite3"
)
target_compile_definitions( main PRIVATE TILES DYNAMIC_ATLAS SDL_SOUND LUA BACKTRACE RELEASE )
target_compile_options( main PRIVATE -Wextra -Wall -fsigned-char -ffast-math )
target_link_libraries( main PRIVATE
    GLESv1_CM
    GLESv2
    dl
    log
    z
    hidapi
    SDL2
    SDL2_image
    SDL2_mixer
    SDL2_ttf
    mpg123
    libsqlite3
)
