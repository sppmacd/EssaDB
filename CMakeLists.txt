cmake_minimum_required(VERSION 3.17)
project(EssaDB)

include(FetchContent)
FetchContent_Declare(Essa
    GIT_REPOSITORY https://github.com/essa-software/EssaGUI.git
    GIT_TAG 39005b66f133881e54180a9fb3a35ff90aa47923
    OVERRIDE_FIND_PACKAGE
)
FetchContent_MakeAvailable(Essa)

# GCC moment
add_compile_options(-Wno-error=restrict)

include_directories(${Essa_SOURCE_DIR}) # FIXME: This should be automatic
include_directories(${CMAKE_SOURCE_DIR})

add_subdirectory(db)
add_subdirectory(gui)
add_subdirectory(repl)
add_subdirectory(tests)
