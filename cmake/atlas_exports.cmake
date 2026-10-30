include_guard(GLOBAL)

function(atlas_export_library target component public_name)
    set_target_properties(${target} PROPERTIES EXPORT_NAME ${public_name})
    if(MSVC)
        set_target_properties(${target} PROPERTIES COMPILE_PDB_NAME ${target}
            COMPILE_PDB_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/symbols/$<CONFIG>")
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/symbols/$<CONFIG>/${target}.pdb"
            DESTINATION lib OPTIONAL)
    endif()
    install(TARGETS ${target} EXPORT atlas_${component}_exports ARCHIVE DESTINATION lib)
    install(EXPORT atlas_${component}_exports FILE ${component}.cmake
        NAMESPACE atlas:: DESTINATION share/atlas)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/" DESTINATION include)
endfunction()
