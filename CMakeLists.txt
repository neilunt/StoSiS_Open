cmake_minimum_required(VERSION 3.20)
project(StoSiS LANGUAGES CXX CUDA)

# CUDA ist aktiv: Die GPU-Kernel (z.B. First-Passage-Time-Transport) liegen
# in der Bibliothek stosis_cuda, GPU-Architekturen siehe core/CMakeLists.txt
# (CUDA_ARCHITECTURES).

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_STANDARD 17)

# Praktisch fuer Visual Studio: sortiert Core/GUI als eigene "Ordner" im
# Solution Explorer statt alles flach aufzulisten.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

add_subdirectory(core)
add_subdirectory(gui)

# Automatische Tests des Rechenkerns (siehe tests/CMakeLists.txt).
option(STOSIS_BUILD_TESTS "Tests des Rechenkerns bauen" ON)
if(STOSIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
