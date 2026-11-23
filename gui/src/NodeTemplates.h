#pragma once
#include <QString>
#include <QColor>
#include <vector>
#include "FlowsheetGraph.h"

// Alles, was im Flowsheet als Block angelegt werden kann - gemeinsame
// Liste fuer die Block-Palette (Drag & Drop) und das Rechtsklick-Menue.
// Ein neuer Unit-Typ braucht hier nur einen weiteren Eintrag.
struct SNodeTemplate
{
    QString tileText; // Kurzname auf der Kachel in der Palette
    QString menuText; // Text im Rechtsklick-Menue
    QString group;    // Gruppe in der Palette
    stosis::ENodeType type;
    size_t numPorts;  // Ausgaenge beim Splitter und Sieb, Eingaenge beim Merger
};

inline const std::vector<SNodeTemplate>& GetNodeTemplates()
{
    static const std::vector<SNodeTemplate> templates = {
        { "Inlet",      "Inlet",                  "Quellen & Senken", stosis::ENodeType::Inlet,    0 },
        { "Outlet",     "Outlet",                 "Quellen & Senken", stosis::ENodeType::Outlet,   0 },
        { "Splitter 2", "Splitter (2 Ausgaenge)", "Stromfuehrung",    stosis::ENodeType::Splitter, 2 },
        { "Splitter 3", "Splitter (3 Ausgaenge)", "Stromfuehrung",    stosis::ENodeType::Splitter, 3 },
        { "Merger 2",   "Merger (2 Eingaenge)",   "Stromfuehrung",    stosis::ENodeType::Merger,   2 },
        { "Merger 3",   "Merger (3 Eingaenge)",   "Stromfuehrung",    stosis::ENodeType::Merger,   3 },
        { "Prozess",    "Prozess-Einheit",        "Prozesse",         stosis::ENodeType::Process,  0 },
        { "Behaelter",  "Behaelter (Hold-up)",    "Prozesse",         stosis::ENodeType::Vessel,   0 },
        { "Sieb 2",     "Sieb (2 Ausgaenge: Grob- und Feingut)", "Trennen", stosis::ENodeType::Screen, 2 },
        { "Sieb 3",     "Sieb (3 Ausgaenge, 2 Decks)", "Trennen",   stosis::ENodeType::Screen,   3 },
        { "Sieb 4",     "Sieb (4 Ausgaenge, 3 Decks)", "Trennen",   stosis::ENodeType::Screen,   4 },
        { "Sichter",    "Sichter (Schwer- und Leichtgut)", "Trennen", stosis::ENodeType::Classifier, 0 },
        { "Stofftrenner", "Stofftrenner (Anteil je Stoff)", "Trennen", stosis::ENodeType::CompoundSeparator, 0 },
    };
    return templates;
}

// Fuellfarbe der Bloecke - Palette und Flowsheet sehen so gleich aus.
inline QColor GetNodeColor(stosis::ENodeType type)
{
    switch (type)
    {
    case stosis::ENodeType::Inlet:    return QColor(120, 195, 120);
    case stosis::ENodeType::Outlet:   return QColor(120, 165, 220);
    case stosis::ENodeType::Process:  return QColor(200, 140, 60);
    case stosis::ENodeType::Splitter: return QColor(150, 110, 190);
    case stosis::ENodeType::Merger:   return QColor(80, 160, 160);
    case stosis::ENodeType::Vessel:   return QColor(175, 105, 85);
    case stosis::ENodeType::Screen:   return QColor(120, 120, 200);
    case stosis::ENodeType::Classifier: return QColor(95, 150, 205);
    case stosis::ENodeType::CompoundSeparator: return QColor(160, 120, 170);
    }
    return QColor(150, 150, 150);
}

// Drag & Drop von der Palette ins Flowsheet: Nutzlast = Index in
// GetNodeTemplates().
inline constexpr const char* kNodeTemplateMimeType = "application/x-stosis-node-template";
