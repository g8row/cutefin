# Qt Test targets. No network dependency in the default suite; each test
# links against cutefin_core so it exercises the real implementation.

function(cutefin_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE cutefin_core Qt6::Test)
    target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_compile_definitions(${name} PRIVATE CUTEFIN_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    add_test(NAME ${name} COMMAND ${name})
endfunction()

cutefin_add_test(tst_models)
cutefin_add_test(tst_blurhash)
cutefin_add_test(tst_settings)
cutefin_add_test(tst_playbackresolver)
cutefin_add_test(tst_itemquery)

# tst_live is intentionally not added here yet: it exercises a real server
# from CUTEFIN_TEST_SERVER/USER/PASS env vars and is out of scope for this
# phase (DESIGN.md §9).
