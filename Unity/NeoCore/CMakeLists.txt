foreach(core_target neocore_backend neocore_type_model neocore_frontend neocore_runtime)
    set_target_properties(${core_target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endforeach()

add_library(NeoCoreUnity SHARED NeoCoreC.cpp)
target_link_libraries(NeoCoreUnity PRIVATE NeoCore::Runtime)
target_include_directories(NeoCoreUnity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(NeoCoreUnity PRIVATE NEOCORE_C_BUILD)
target_compile_features(NeoCoreUnity PRIVATE cxx_std_11)
if(MSVC)
    target_compile_options(NeoCoreUnity PRIVATE /utf-8 /W4)
elseif(UNIX AND NOT APPLE)
    target_link_options(NeoCoreUnity PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/neocore.exports")
endif()

if(NEOSCRIPT_BUILD_SMOKE)
    add_executable(neocore_unity_c_smoke smoke.c)
    target_link_libraries(neocore_unity_c_smoke PRIVATE NeoCoreUnity)
    add_test(NAME neocore_unity_c_smoke COMMAND neocore_unity_c_smoke
        "${CMAKE_CURRENT_SOURCE_DIR}/../com.neocore.unity/Samples~/Basic/demo.nc")
    set_tests_properties(neocore_unity_c_smoke PROPERTIES TIMEOUT 30)
endif()
