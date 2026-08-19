cmake_minimum_required(VERSION 3.15)
project(ValMods VERSION 1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
    message(FATAL_ERROR "ValMods utilise l'API Win32 : cible Windows uniquement.")
endif()

# WIN32 = application fenetree (pas de console qui s'ouvre derriere)
add_executable(valmods WIN32
    valmods.cpp
    minijson.h
    valmods.rc
)

target_compile_definitions(valmods PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0600)
target_link_libraries(valmods PRIVATE comctl32 shell32 ole32 advapi32 gdi32 user32)

if(MINGW)
    # -municode : point d'entree wWinMain    -static : exe autonome, aucune DLL a livrer
    target_compile_options(valmods PRIVATE -municode -Wall)
    target_link_options(valmods PRIVATE -municode -static -mwindows)
endif()

if(MSVC)
    target_compile_options(valmods PRIVATE /utf-8 /W3)
endif()

install(TARGETS valmods RUNTIME DESTINATION .)

# ---------------------------------------------------------------------------
#  Build :
#     cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
#     cmake --build build
#  -> build/valmods.exe
#
#  Aucune dependance externe : vcpkg n'est pas necessaire.
# ---------------------------------------------------------------------------
