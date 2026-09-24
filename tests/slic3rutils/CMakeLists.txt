# slic3rutils tests require libslic3r_gui (contains Http.cpp).
# Skip entirely when GUI is disabled.
if (NOT SLIC3R_GUI)
    return()
endif()

get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests_main.cpp
    SnapLogClient_test.cpp
    )

find_package(JPEG REQUIRED)
find_package(PNG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(libnoise REQUIRED)
find_package(Qhull 7.2 QUIET)
if(UNIX AND NOT APPLE)
    target_link_options(${_TEST_NAME}_tests PRIVATE "-Wl,--no-as-needed")
endif()
# libslic3r/libslic3r_gui are STATIC libraries whose PRIVATE deps are not
# propagated, so these must be linked explicitly here too (see
# src/libslic3r/CMakeLists.txt).
target_link_libraries(${_TEST_NAME}_tests PRIVATE JPEG::JPEG PNG::PNG ZLIB::ZLIB noise::noise qhull)
if (MSVC)
    target_link_libraries(${_TEST_NAME}_tests PRIVATE Setupapi.lib)
endif ()

target_link_libraries(${_TEST_NAME}_tests test_common libslic3r_gui libslic3r Catch2::Catch2WithMain)
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

orcaslicer_copy_test_dlls()

orcaslicer_discover_tests(${_TEST_NAME}_tests)
