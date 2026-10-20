# Automatische Tests des Rechenkerns (stosis_core, ohne Qt). Jedes
# Testprogramm endet mit Exit-Code 0 (bestanden) oder 1 (fehlgeschlagen).
#
# Ausfuehren nach dem Bauen (Release empfohlen, die statistischen Tests
# rechnen lange Einzellaeufe und 2000 Mehrfachlaeufe):
#     cmake --build build --config Release
#     ctest --test-dir build -C Release --output-on-failure

set(STOSIS_TESTS
    test_distributions   # Groessenverteilungen am Inlet (Kapitel 3)
    test_graph           # Ports, Splitter/Merger, Schnittregel (Kapitel 4, 6.2)
    test_recycle         # Rueckfuehrungen, Massenbilanz (Kapitel 6.3)
    test_splitter_ou     # OU-Splitter, Zufallszahlen (Kapitel 8, 9.4)
    test_ensemble        # Mehrfachlaeufe gegen analytische Loesung (Kapitel 10, 11)
    test_materials       # Materialdatei (Kapitel 14.4, Schritt 2)
    test_streams         # Stoffe, Temperatur, Druck im Strom (Kapitel 14.4, Schritt 3)
    test_sizegrid        # Logarithmisches Groessenraster, SI-Einheiten (Kapitel 2.5, 2.6)
    test_regression      # alle Ergebnisse gegen gespeicherte Referenzwerte
)

foreach(name IN LISTS STOSIS_TESTS)
    add_executable(${name} ${name}.cpp TestSupport.h)
    target_link_libraries(${name} PRIVATE stosis_core)
    target_compile_definitions(${name} PRIVATE STOSIS_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
    set_target_properties(${name} PROPERTIES FOLDER "Tests")
    add_test(NAME ${name} COMMAND ${name})
endforeach()

# Auswertung fuer die Konvergenz-Abbildung im Methoden-Dokument (kein Test).
add_executable(analysis_convergence analysis_convergence.cpp)
target_link_libraries(analysis_convergence PRIVATE stosis_core)
set_target_properties(analysis_convergence PROPERTIES FOLDER "Tests")
