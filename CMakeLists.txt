cmake_minimum_required(VERSION 3.20)
project(StoSiS VERSION 0.1.1 LANGUAGES CXX CUDA)

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

# Weitergabe ohne Qt, Visual Studio, CUDA und CMake (Methoden-Dokument
# Kap. 15): Nach dem Bauen legt
#     cmake --build build --config Release --target package
# in build/ zwei Pakete an:
#   StoSiS-<Version>-win64.zip - Ordner zum Entpacken, StoSiS.exe starten;
#   StoSiS-<Version>-win64.exe - Installationsprogramm (nur wenn Inno Setup 6
#                                installiert ist, https://jrsoftware.org).
# Inhalt: StoSiS.exe, Qt-DLLs, Visual-C++-Laufzeit, data/Materialien.json.
# Die CUDA-Laufzeit ist fest eingebunden; die GPU-Teile brauchen nur einen
# NVIDIA-Treiber.
set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION .)
include(InstallRequiredSystemLibraries)

set(CPACK_PACKAGE_NAME "StoSiS")
set(CPACK_PACKAGE_VENDOR "StoSiS")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Stochastische Fliessschemasimulation fuer Feststoffprozesse")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "StoSiS-${PROJECT_VERSION}-win64")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "StoSiS")
set(CPACK_PACKAGE_EXECUTABLES "StoSiS" "StoSiS")
set(CPACK_CREATE_DESKTOP_LINKS "StoSiS")
set(CPACK_GENERATOR "ZIP")
find_program(STOSIS_ISCC_EXE ISCC HINTS "C:/Program Files (x86)/Inno Setup 6" "C:/Program Files/Inno Setup 6")
if(STOSIS_ISCC_EXE)
    list(APPEND CPACK_GENERATOR "INNOSETUP")
    set(CPACK_INNOSETUP_EXECUTABLE "${STOSIS_ISCC_EXE}")
    set(CPACK_INNOSETUP_LANGUAGES "german")
    set(CPACK_INNOSETUP_IGNORE_LICENSE_PAGE ON)
    set(CPACK_INNOSETUP_USE_MODERN_WIZARD ON)
    set(CPACK_INNOSETUP_ARCHITECTURE "x64")
    # Programmsymbol auch fuer das Installationsprogramm und den Eintrag
    # unter Einstellungen -> Apps.
    set(CPACK_INNOSETUP_ICON_FILE "${CMAKE_SOURCE_DIR}/gui/resources/StoSiS.ico")
    set(CPACK_INNOSETUP_SETUP_UninstallDisplayIcon "{app}\\StoSiS.exe")
endif()
# Werte unveraendert an cpack weitergeben: Ohne diese Einstellung landet der
# Backslash aus "{app}\StoSiS.exe" unmaskiert in CPackConfig.cmake, und
# cpack bricht beim Lesen ab ("CPack project name not specified").
set(CPACK_VERBATIM_VARIABLES ON)
include(CPack)

# Automatische Tests des Rechenkerns (siehe tests/CMakeLists.txt).
option(STOSIS_BUILD_TESTS "Tests des Rechenkerns bauen" ON)
if(STOSIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
