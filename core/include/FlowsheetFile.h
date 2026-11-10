#pragma once
#include <string>
#include <vector>
#include "FlowsheetGraph.h"

namespace stosis
{
    // Flowsheet als Datei (Methodenhandbuch Kap. 3): JSON mit allen
    // Eingaben, jeder Wert ausgeschrieben (auch Standardwerte, damit eine
    // Datei auch dann gleich rechnet, wenn sich ein Standardwert aendert).
    //  - Simulation: Dauer, dt, Seed, Laeufe, Darstellung, Groessenraster,
    //    Karten; Ausfuehrung: ausfuehrliches Laufzeitprotokoll.
    //  - Units: ID (bestimmt den Zufallsstrom der Unit), Typ, Name, Lage,
    //    Zahl der Ports bei Splitter und Merger, alle Einstellungen.
    //  - Verbindungen: von Unit/Ausgang zu Unit/Eingang (ab 1 gezaehlt) mit
    //    "recycle": "manual" = von Hand als Rueckfuehrung markiert (wird
    //    uebernommen), "auto" = vom Graphen aufgeschnitten (nur zur
    //    Information, beim Laden neu bestimmt, Hinweis bei Abweichung),
    //    "no" = nicht aufgeschnitten.
    // Nicht in der Datei: Rechenreihenfolge (der Graph bestimmt sie beim
    // Laden neu), Ergebnisse, Karten, Stoffwerte (nur Stoffnamen aus
    // Materialien.json), Auswertung, Kartenansicht, Fensterlage.
    //
    // Formatversion 2; Version 1 (recycle als true/false, true = von Hand
    // markiert) wird weiter gelesen.
    //
    // Neue Einstellung in einer Unit oder im Raster? Hier schreiben und
    // lesen (FlowsheetFile.cpp) und in den Rundlauftest aufnehmen
    // (tests/test_flowsheet_file.cpp).
    inline constexpr const char* kFlowsheetFormatName = "StoSiS-Flowsheet";
    inline constexpr int kFlowsheetFormatVersion = 2;

    // Was ausser dem Graphen in die Datei gehoert (in der Oberflaeche
    // steht es in der Simulationsleiste).
    struct SFlowsheetFileSettings
    {
        size_t runs = 20; // Anzahl Laeufe
    };

    // Ergebnis des Lesens.
    struct SFlowsheetReadResult
    {
        bool ok = false;
        std::string error;                 // bei !ok: was nicht passt und wo
        std::vector<std::string> warnings; // fehlende Felder (Standardwert genommen), unbekannte Felder
        SFlowsheetFileSettings settings;
    };

    // Das Flowsheet als JSON-Text, so wie es in der Datei steht.
    std::string WriteFlowsheetJson(const CFlowsheetGraph& graph, const SFlowsheetFileSettings& settings);

    // Liest JSON-Text in einen LEEREN Graphen (Stoffdatenbank setzt der
    // Aufrufer). Bei einem Fehler ist der Graph unvollstaendig und sollte
    // verworfen werden.
    SFlowsheetReadResult ReadFlowsheetJson(const std::string& text, CFlowsheetGraph& graph);

    // Dasselbe mit einer Datei; Pfade als UTF-8.
    bool SaveFlowsheetFile(const std::string& path, const CFlowsheetGraph& graph,
        const SFlowsheetFileSettings& settings, std::string& error);
    SFlowsheetReadResult LoadFlowsheetFile(const std::string& path, CFlowsheetGraph& graph);
}
