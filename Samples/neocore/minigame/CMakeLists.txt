add_executable(neocore_minigame main.cpp)
target_link_libraries(neocore_minigame PRIVATE NeoCore::Runtime)
target_compile_definitions(neocore_minigame PRIVATE
    NEOCORE_MINIGAME_SCRIPT="${CMAKE_CURRENT_SOURCE_DIR}/scripts/mover.nc")
if(MSVC)
    target_compile_options(neocore_minigame PRIVATE /utf-8 /W4)
endif()
set_target_properties(neocore_minigame PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
add_test(NAME neocore_minigame COMMAND neocore_minigame --verify)
set_tests_properties(neocore_minigame PROPERTIES
    PASS_REGULAR_EXPRESSION "NeoCore minigame: all checks passed" TIMEOUT 30)

add_custom_command(TARGET neocore_minigame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../THIRD_PARTY_NOTICES.txt"
        "$<TARGET_FILE_DIR:neocore_minigame>/NEOSCRIPT_THIRD_PARTY_NOTICES.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../LICENSE"
        "$<TARGET_FILE_DIR:neocore_minigame>/NEOSCRIPT_LICENSE.txt")
