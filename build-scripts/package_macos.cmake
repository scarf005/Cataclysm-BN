function( run_or_fail )
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE cmd_result
        COMMAND_ECHO STDOUT
    )
    if( NOT cmd_result EQUAL 0 )
        message( FATAL_ERROR "Command failed (${cmd_result}): ${ARGN}" )
    endif()
endfunction()

function( run_or_fail_in_dir workdir )
    execute_process(
        COMMAND ${ARGN}
        WORKING_DIRECTORY "${workdir}"
        RESULT_VARIABLE cmd_result
        COMMAND_ECHO STDOUT
    )
    if( NOT cmd_result EQUAL 0 )
        message( FATAL_ERROR "Command failed (${cmd_result}): ${ARGN}" )
    endif()
endfunction()

if( NOT DEFINED CATA_OSX_BINARY_NAME )
    message( FATAL_ERROR "CATA_OSX_BINARY_NAME is required." )
endif()

if( NOT DEFINED CATA_OSX_VERSION )
    set( CATA_OSX_VERSION HEAD-HASH )
endif()

if( NOT DEFINED CATA_OSX_APP_BUNDLE )
    set( CATA_OSX_APP_BUNDLE "${CMAKE_BINARY_DIR}/Cataclysm.app" )
endif()

if( NOT DEFINED CATA_OSX_DMG_PATH )
    set( CATA_OSX_DMG_PATH "${CMAKE_BINARY_DIR}/CataclysmBN-${CATA_OSX_VERSION}.dmg" )
endif()

if( NOT DEFINED CATA_DMGBUILD_EXECUTABLE )
    set( CATA_DMGBUILD_EXECUTABLE dmgbuild )
endif()

if( NOT DEFINED CATA_PLUTIL_EXECUTABLE )
    set( CATA_PLUTIL_EXECUTABLE plutil )
endif()

if( NOT DEFINED CMAKE_INSTALL_CONFIG_NAME )
    set( CMAKE_INSTALL_CONFIG_NAME "" )
endif()

set( app_contents "${CATA_OSX_APP_BUNDLE}/Contents" )
set( app_macos "${app_contents}/MacOS" )
set( app_resources "${app_contents}/Resources" )
set( install_command "${CMAKE_COMMAND}" --install "${CMAKE_BINARY_DIR}" --prefix "${app_resources}" )

if( NOT CMAKE_INSTALL_CONFIG_NAME STREQUAL "" )
    list( APPEND install_command --config "${CMAKE_INSTALL_CONFIG_NAME}" )
endif()

run_or_fail( "${CMAKE_COMMAND}" -E rm -rf "${CATA_OSX_APP_BUNDLE}" )
run_or_fail( "${CMAKE_COMMAND}" -E rm -f "${CATA_OSX_DMG_PATH}" )
run_or_fail( "${CMAKE_COMMAND}" -E make_directory "${app_macos}" "${app_resources}" )
run_or_fail( ${install_command} )
run_or_fail( "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/build-data/osx/Info.plist" "${app_contents}/Info.plist" )
run_or_fail( "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/build-data/osx/Cataclysm.sh" "${app_macos}/Cataclysm.sh" )
run_or_fail( "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/build-data/osx/AppIcon.icns" "${app_resources}/AppIcon.icns" )

if( CATA_OSX_BINARY_NAME STREQUAL "cataclysm-bn" )
    run_or_fail_in_dir( "${app_resources}" "${CMAKE_COMMAND}" -E create_symlink cataclysm-bn cataclysm )
endif()

run_or_fail( python3 "${CMAKE_SOURCE_DIR}/tools/copy_mac_libs.py" "${app_resources}/${CATA_OSX_BINARY_NAME}" )
run_or_fail( "${CATA_PLUTIL_EXECUTABLE}" -convert binary1 "${app_contents}/Info.plist" )
run_or_fail_in_dir(
    "${CMAKE_SOURCE_DIR}"
    "${CATA_DMGBUILD_EXECUTABLE}"
    -s "${CMAKE_SOURCE_DIR}/build-data/osx/dmgsettings.py"
    -D "app=${CATA_OSX_APP_BUNDLE}"
    "Cataclysm BN"
    "${CATA_OSX_DMG_PATH}"
)
