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
cutefin_add_test(tst_playbacknavigation)
cutefin_add_test(tst_trackselection)

cutefin_add_test(tst_qmlmodels)
target_sources(tst_qmlmodels PRIVATE
    ${CMAKE_SOURCE_DIR}/src/app/MediaItemModel.cpp
    ${CMAKE_SOURCE_DIR}/src/app/HomeSectionModel.cpp)

# Opt-in live integration test: only runs when invoked with `ctest -L live`
# and CUTEFIN_TEST_SERVER/USER/PASS env vars set.
add_executable(tst_live tst_live.cpp)
target_link_libraries(tst_live PRIVATE cutefin_core Qt6::Test)
target_include_directories(tst_live PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME tst_live COMMAND tst_live)
set_tests_properties(tst_live PROPERTIES LABELS "live")
