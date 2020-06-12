cmake_minimum_required (VERSION 3.0)
project(pd-mpv)

set(CMAKE_MODULE_PATH
  ${CMAKE_CURRENT_LIST_DIR}/CMake-modules)

set(CMAKE_CXX_STANDARD 11)

if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang$")
  set(CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS "${CMAKE_SHARED_LIBRARY_CREATE_CXX_FLAGS} -undefined dynamic_lookup")
endif()

set(PUREDATA_INCLUDE_DIRS "" CACHE PATH "Path to folder containing m_pd.h" )
set(GEM_INCLUDE_DIRS "" CACHE PATH "Path to Gem header folder")
find_package(PkgConfig)
set(ENV{PKG_CONFIG_PATH} "/usr/local/lib/pkgconfig/;/usr/lib/pkgconfig/")
pkg_check_modules(GEM Gem)
message(STATUS "GEM_INCLUDE_DIRS ${GEM_INCLUDE_DIRS}")
include_directories(${PUREDATA_INCLUDE_DIRS} ${GEM_INCLUDE_DIRS} ${GEM_INCLUDE_DIRS}/Gem)

find_package(MPV 0.27.2 REQUIRED)

set(PD_INSTALL_FOLDER "${CMAKE_BINARY_DIR}/package/mpv")
message(STATUS "pd-mpv install dir : ${PD_NDI_INSTALL_FOLDER}")

set(SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/gem_mpv.cpp
  ${CMAKE_CURRENT_LIST_DIR}/gemframebuffer.cpp
)

set(HEADERS
  ${CMAKE_CURRENT_LIST_DIR}/gem_mpv.hpp
  ${CMAKE_CURRENT_LIST_DIR}/gemframebuffer.hpp
)

include_directories(
  ${GEM_INCLUDE_DIR}
  ${MPV_INCLUDE_DIR}
)

add_library(${PROJECT_NAME} SHARED ${SOURCES})

# set library binary file name and extension
set_target_properties(${PROJECT_NAME}
  PROPERTIES
    PREFIX ""
    OUTPUT_NAME "mpv")

if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".pd_darwin")
    set_target_properties(${PROJECT_NAME}
      PROPERTIES
      INSTALL_RPATH "@loader_path"
    )

    get_filename_component(LIBMPV_DIRECTORY ${MPV_LIBRARY} DIRECTORY)
    get_filename_component(LIBMPV_BASENAME ${MPV_LIBRARY} NAME_WE)
    get_filename_component(MPV_LIBRARY ${MPV_LIBRARY} NAME)
    file(GLOB MPV_LIBS_TO_INSTALL
        LIST_DIRECTORIES false
        "${LIBMPV_DIRECTORY}/${LIBMPV_BASENAME}*"
        )
    set_target_properties(${PROJECT_NAME}
      PROPERTIES
      LINK_DIRECTORIES "${LIBMPV_DIRECTORY}"
    )
elseif()
    if(${OSSIA_ARCHITECTURE} MATCHES "arm")
        set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".l_arm")
    elseif(${OSSIA_ARCHITECTURE} MATCHES "x86")
        set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".l_i386")
    else()
        set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".pd_linux")
    endif()
    set_target_properties(${PROJECT_NAME}
      PROPERTIES
      INSTALL_RPATH "$ORIGIN"
    )
    file(GLOB MPV_LIBS_TO_INSTALL
      LIST_DIRECTORIES false
      "${MPV_LIBRARY}*")
endif()

message(STATUS "MPV library : ${MPV_LIBRARY}")

target_link_libraries(${PROJECT_NAME} ${MPV_LIBRARY})

install(TARGETS ${PROJECT_NAME}
    DESTINATION ${PD_INSTALL_FOLDER}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
)

message(STATUS "Library to install: ${MPV_LIBS_TO_INSTALL}")
install(FILES
  ${CMAKE_CURRENT_LIST_DIR}/mpv-help.pd
  ${CMAKE_CURRENT_LIST_DIR}/License.txt
  ${MPV_LIBS_TO_INSTALL}
    DESTINATION ${PD_INSTALL_FOLDER}
)
