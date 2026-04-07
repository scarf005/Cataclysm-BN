if (RELEASE)
    if (SOUND)
        set(sound_exclude_pattern)
    else()
        set(sound_exclude_pattern PATTERN "sound" EXCLUDE)
    endif()
    if (USE_PREFIX_DATA_DIR)
        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
                TYPE DATA
                PATTERN "CMakeLists.txt" EXCLUDE
                PATTERN "XDG" EXCLUDE
                ${sound_exclude_pattern}
                PATTERN "external_tileset/README.md" EXCLUDE
                PATTERN "mapgen/lab/README.md" EXCLUDE)
    else()
        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
                DESTINATION data
                PATTERN "CMakeLists.txt" EXCLUDE
                PATTERN "XDG" EXCLUDE
                ${sound_exclude_pattern}
                PATTERN "external_tileset/README.md" EXCLUDE
                PATTERN "mapgen/lab/README.md" EXCLUDE)
    endif()
endif ()
