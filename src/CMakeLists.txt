file(GLOB_RECURSE LIB_SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER LIB_SOURCES EXCLUDE REGEX "main.cpp$")
list(FILTER LIB_SOURCES EXCLUDE REGEX "mtsp_main.cpp$")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/lkh_wrapper_v8/.*\\.cpp$")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/lkh_wrapper_v9/[0-9][0-9]_.*\\.cpp$")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/algorithms/mtsp/2opt\\+greed\\.cpp$")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/algorithms/mtsp/grasp\\.cpp$")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/algorithms/mtsp/rand\\+nn\\.cpp$")
if(NOT WIN32)
    list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/baselines/lkh3_baseline\\.cpp$")
endif()

add_library(mtsp_core OBJECT ${LIB_SOURCES})

target_include_directories(mtsp_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    target_link_libraries(mtsp_core PUBLIC OpenMP::OpenMP_CXX)
endif()

add_executable(tsp main.cpp $<TARGET_OBJECTS:mtsp_core>)
add_executable(mtsp mtsp_main.cpp $<TARGET_OBJECTS:mtsp_core>)

target_link_libraries(tsp PRIVATE mtsp_core)
target_link_libraries(mtsp PRIVATE mtsp_core)
if(OpenMP_CXX_FOUND)
    target_link_libraries(tsp PRIVATE OpenMP::OpenMP_CXX)
    target_link_libraries(mtsp PRIVATE OpenMP::OpenMP_CXX)
endif()
