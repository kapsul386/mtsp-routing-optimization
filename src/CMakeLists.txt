file(GLOB_RECURSE LIB_SOURCES "*.cpp")
list(FILTER LIB_SOURCES EXCLUDE REGEX "main.cpp$")
list(FILTER LIB_SOURCES EXCLUDE REGEX "mtsp_main.cpp$")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/algorithms/mtsp/2opt\\+greed\\.cpp$")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/algorithms/mtsp/grasp\\.cpp$")
list(FILTER LIB_SOURCES EXCLUDE REGEX ".*/algorithms/mtsp/rand\\+nn\\.cpp$")

add_library(mtsp_core OBJECT ${LIB_SOURCES})

target_include_directories(mtsp_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

add_executable(tsp main.cpp $<TARGET_OBJECTS:mtsp_core>)
add_executable(mtsp mtsp_main.cpp $<TARGET_OBJECTS:mtsp_core>)

target_link_libraries(tsp PRIVATE mtsp_core)
target_link_libraries(mtsp PRIVATE mtsp_core)
