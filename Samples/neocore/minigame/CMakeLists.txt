add_executable(neocore_minigame main.cpp replay.cpp sample_io.h)
target_link_libraries(neocore_minigame PRIVATE NeoCore::Runtime)
set_target_properties(neocore_minigame PROPERTIES
    VS_DEBUGGER_COMMAND_ARGUMENTS "--verify --file \"${CMAKE_CURRENT_SOURCE_DIR}/scripts/mover.nc\"")
if(WIN32)
    target_link_libraries(neocore_minigame PRIVATE shell32)
endif()
if(MSVC)
    target_compile_options(neocore_minigame PRIVATE /utf-8 /W4)
endif()
set_target_properties(neocore_minigame PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>")
add_test(NAME neocore_minigame COMMAND neocore_minigame --verify
    --file "${CMAKE_CURRENT_SOURCE_DIR}/scripts/mover.nc")
set_tests_properties(neocore_minigame PROPERTIES
    PASS_REGULAR_EXPRESSION "NeoCore minigame: all checks passed" TIMEOUT 30)
add_test(NAME neocore_minigame_motion COMMAND neocore_minigame --motion --verify
    --file "${CMAKE_CURRENT_SOURCE_DIR}/scripts/replay.nc")
set_tests_properties(neocore_minigame_motion PROPERTIES
    PASS_REGULAR_EXPRESSION "NeoCore motion: all checks passed" TIMEOUT 30)
if(NEOCORE_BUILD_TESTS AND Python3_Interpreter_FOUND)
    add_test(NAME neocore_minigame_cli_regression COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../Tests/neocore_minigame_cli_regression.py"
        --minigame $<TARGET_FILE:neocore_minigame>)
    set_tests_properties(neocore_minigame_cli_regression PROPERTIES TIMEOUT 60)
endif()

add_custom_command(TARGET neocore_minigame POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/viewer.html"
        "$<TARGET_FILE_DIR:neocore_minigame>/viewer.html"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../THIRD_PARTY_NOTICES.txt"
        "$<TARGET_FILE_DIR:neocore_minigame>/NEOCORE_THIRD_PARTY_NOTICES.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../LICENSE"
        "$<TARGET_FILE_DIR:neocore_minigame>/NEOCORE_LICENSE.txt")
