if(BUILD_SHARED_LIBS)
    set(_build_shared ON)
    set(_build_static OFF)
else()
    set(_build_shared OFF)
    set(_build_static ON)
endif()

# get relative path of CMAKE_BINARY_DIR against root source directory
file(RELATIVE_PATH BINARY_DIR_REL  ${CMAKE_SOURCE_DIR}/.. ${CMAKE_BINARY_DIR})

if (IN_GIT_REPO)
    set(OPENVDB_DIRECTORY_FLAG --directory ${BINARY_DIR_REL}/dep_OpenVDB-prefix/src/dep_OpenVDB)
endif ()

Snapmaker_Orca_add_cmake_project(OpenVDB
    #  support vs2022, update to 8.2
    URL https://github.com/tamasmeszaros/openvdb/archive/a68fd58d0e2b85f01adeb8b13d7555183ab10aa5.zip 
    URL_HASH SHA256=f353e7b99bd0cbfc27ac9082de51acf32a8bc0b3e21ff9661ecca6f205ec1d81
    PATCH_COMMAND ${CMAKE_COMMAND} -E echo "Patching OpenVDB..." || true
    DEPENDS dep_TBB dep_Blosc dep_OpenEXR dep_Boost
    CMAKE_ARGS
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON 
        -DOPENVDB_BUILD_PYTHON_MODULE=OFF
        -DUSE_BLOSC=ON
        -DOPENVDB_CORE_SHARED=${_build_shared} 
        -DOPENVDB_CORE_STATIC=${_build_static}
        -DOPENVDB_ENABLE_RPATH:BOOL=OFF
        -DTBB_STATIC=${_build_static}
        -DOPENVDB_BUILD_VDB_PRINT=ON
        -DDISABLE_DEPENDENCY_VERSION_CHECKS=ON # Centos6 has old zlib
)

ExternalProject_Get_Property(dep_OpenVDB SOURCE_DIR)
# Clang >= 19 (Apple/Xcode, and the LLVM SDK extension the flatpak is built with since upstream 2.4.2) rejects
# OpT::template eval(...) without template args (-Wmissing-template-arg-list-after-template-kw); GCC and MSVC accept
# both spellings. Upstream applies 0001-clang19.patch with git apply; this rewrites the one header in place instead,
# so no git is needed in the build sandbox. Applied everywhere except MSVC.
if (NOT MSVC)
    ExternalProject_Add_Step(dep_OpenVDB fix_template_syntax
        DEPENDEES configure
        DEPENDERS build
        COMMAND ${CMAKE_COMMAND} -DNODE_MANAGER=${SOURCE_DIR}/openvdb/openvdb/tree/NodeManager.h -P ${CMAKE_CURRENT_LIST_DIR}/fix_template_syntax.cmake
    )
endif ()

if (MSVC)
    if (${DEP_DEBUG})
        ExternalProject_Get_Property(dep_OpenVDB BINARY_DIR)
        ExternalProject_Add_Step(dep_OpenVDB build_debug
            DEPENDEES build
            DEPENDERS install
            COMMAND ${CMAKE_COMMAND} ../dep_OpenVDB -DOPENVDB_BUILD_VDB_PRINT=OFF
            COMMAND msbuild /m /P:Configuration=Debug INSTALL.vcxproj
            WORKING_DIRECTORY "${BINARY_DIR}"
        )
    endif ()
endif ()