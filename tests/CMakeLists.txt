# Automatische Tests des Rechenkerns (stosis_core, ohne Qt). Jedes
# Testprogramm endet mit Exit-Code 0 (bestanden) oder 1 (fehlgeschlagen).
#
# Ausfuehren nach dem Bauen (Release empfohlen, die statistischen Tests
# rechnen lange Einzellaeufe und 2000 Mehrfachlaeufe):
#     cmake --build build --config Release
#     ctest --test-dir build -C Release --output-on-failure

# In Klammern: Kapitel im Methodenhandbuch (docs/StoSiS_Methodenhandbuch.html).
set(STOSIS_TESTS
    test_distributions   # Groessenverteilungen am Inlet (Kap. 5)
    test_graph           # Ports, Splitter/Merger, Schnittregel (Kap. 3.1, 3.2)
    test_recycle         # Rueckfuehrungen, Massenbilanz (Kap. 3.2)
    test_splitter_ou     # OU-Splitter, Zufallszahlen (Kap. 7.2, 8)
    test_ensemble        # Mehrfachlaeufe gegen analytische Loesung (Kap. 8)
    test_materials       # Materialdatei (Kap. 6)
    test_streams         # Stoffe, Temperatur, Druck im Strom (Kap. 6)
    test_sizegrid        # Logarithmisches Groessenraster, SI-Einheiten (Kap. 4)
    test_signal_analysis # Auswertung schwankender Zeitreihen (Kap. 10, 11)
    test_cards           # Karten im Strom, Umrechnung aufs Raster (Kap. 9.3, 9.6)
    test_particle_units  # Merger und Splitter im Partikelmodus (Kap. 9.4, 9.5)
    test_vessel          # Behaelter: vier Mischungen, Anfangsfuellung, Vorrat (Kap. 7.4)
    test_runlog          # Laufzeitprotokoll, Fortschritt, Kartenspeicher (Kap. 9.6, 10)
    test_flowsheet_file  # Flowsheet als Datei: Rundlauf, Fehler, Struktur (Kap. 3.3, 3.4)
    test_levenspiel      # Kompartiment-Modelle und Mischer aus Dateien gegen exakte RTD (Kap. 11.3, 11.4)
    test_regression      # alle Ergebnisse gegen gespeicherte Referenzwerte
)

foreach(name IN LISTS STOSIS_TESTS)
    add_executable(${name} ${name}.cpp TestSupport.h)
    target_link_libraries(${name} PRIVATE stosis_core)
    target_compile_definitions(${name} PRIVATE STOSIS_DATA_DIR="${CMAKE_SOURCE_DIR}/data" STOSIS_VALIDATION_DIR="${CMAKE_SOURCE_DIR}/validierung/flowsheets")
    set_target_properties(${name} PROPERTIES FOLDER "Tests")
    add_test(NAME ${name} COMMAND ${name})
endforeach()

# Auswertung fuer die Konvergenz ueber die Zahl der Laeufe (Methodenhandbuch Kap. 8, kein Test).
add_executable(analysis_convergence analysis_convergence.cpp)
target_link_libraries(analysis_convergence PRIVATE stosis_core)
set_target_properties(analysis_convergence PROPERTIES FOLDER "Tests")

# Auswertung fuer den Vergleich Dosierwaage - StoSiS (Methodenhandbuch Kap. 11, kein Test).
add_executable(analysis_dosierwaage analysis_dosierwaage.cpp)
target_link_libraries(analysis_dosierwaage PRIVATE stosis_core)
set_target_properties(analysis_dosierwaage PROPERTIES FOLDER "Tests")
