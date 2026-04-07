if( NOT DEFINED CATA_SOURCE_DIR )
    message( FATAL_ERROR "CATA_SOURCE_DIR is required." )
endif()

if( NOT DEFINED CATA_LANGUAGES OR CATA_LANGUAGES STREQUAL "" OR CATA_LANGUAGES STREQUAL "all" )
    file( GLOB po_files "${CATA_SOURCE_DIR}/lang/po/*.po" )
    set( CATA_LANGUAGES "" )
    foreach( po_file ${po_files} )
        get_filename_component( lang "${po_file}" NAME_WE )
        list( APPEND CATA_LANGUAGES "${lang}" )
    endforeach()
endif()

find_program( MSGFMT_EXECUTABLE msgfmt REQUIRED )

foreach( lang ${CATA_LANGUAGES} )
    set( po_file "${CATA_SOURCE_DIR}/lang/po/${lang}.po" )
    set( mo_dir "${CATA_SOURCE_DIR}/lang/mo/${lang}/LC_MESSAGES" )
    set( mo_file "${mo_dir}/cataclysm-bn.mo" )

    if( NOT EXISTS "${po_file}" )
        message( FATAL_ERROR "Missing translation file: ${po_file}" )
    endif()

    file( MAKE_DIRECTORY "${mo_dir}" )
    execute_process(
        COMMAND "${MSGFMT_EXECUTABLE}" -f "${po_file}" -o "${mo_file}"
        RESULT_VARIABLE msgfmt_result
        COMMAND_ECHO STDOUT
    )
    if( NOT msgfmt_result EQUAL 0 )
        message( FATAL_ERROR "msgfmt failed for ${po_file}" )
    endif()
endforeach()
