if(NOT DEFINED SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()

set(UI_ROOT "${SOURCE_ROOT}/src/ui")
set(FORBIDDEN_DOCK_INCLUDE "#include \"ui/dock/")

foreach(layer IN ITEMS common platform settings views rendering)
    file(GLOB_RECURSE layer_files "${UI_ROOT}/${layer}/*.cpp" "${UI_ROOT}/${layer}/*.hpp")
    foreach(file_path IN LISTS layer_files)
        file(READ "${file_path}" file_contents)
        string(FIND "${file_contents}" "${FORBIDDEN_DOCK_INCLUDE}" forbidden_index)
        if(NOT forbidden_index EQUAL -1)
            message(FATAL_ERROR "${file_path} must not depend on ui/dock")
        endif()
    endforeach()
endforeach()

message(STATUS "UI dependency boundaries are valid")
