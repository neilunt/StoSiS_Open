#pragma once
#include <vector>
#include <cstddef>
#include <cstdint>
#include <string>
#include <algorithm>
#include <cmath>
#include "StreamSlice.h"
#include "CardSettings.h"
#include "SizeGrid.h"
#include "Materials.h"

namespace stosis
{
    // Gemeinsames Raster fuer alle Einheiten im Flowsheet: Groessenklassen
    // (knotenbasiert von sizeMin bis sizeMax in m, linear oder logarithmisch,
    // siehe SSizeGrid) und Zeitpunkte ueber die Simulationsdauer totalTime.
    // Die Zeitpunkte t_k = k * dt sind zugleich die Grenzen der Makroschritte.
    // Standard: logarithmisch 1 um bis 1 mm mit 300 Klassen (100 je Dekade).
    struct SSimulationGrid
    {
        ESizeGridType sizeGridType = ESizeGridType::Logarithmic;
        size_t numSizeClasses = 300;
        double sizeMin = 1e-6;
        double sizeMax = 1e-3;
        size_t numTimeSteps = 200;
        double totalTime = 10.0;
        // Globaler Seed fuer alle stochastischen Units: gleicher Seed =
        // gleiches Ergebnis. Jede Unit leitet daraus ihren eigenen
        // Zufallsstrom ab (MakeUnitSeed in Random.h).
        std::uint64_t randomSeed = 1;

        // Kontinuum oder Partikel (Karten) - fuer alle Stroeme dieser
        // Simulation (Methoden-Dokument 13.16, 13.20). Das Groessenraster
        // oben gilt auch im Partikelmodus: darauf werden die Karten fuer
        // Anzeige und Auswertung umgerechnet.
        ERepresentation representation = ERepresentation::Continuum;
        // Kartenzahl, Mindestzahl, Groessenbereiche (nur im Partikelmodus).
        SCardSettings cards;

        // Aktive Stoffe des Flowsheets (Namen aus der Materialdatei); die
        // Reihenfolge ist der Stoffindex in CStreamSlice. Der Graph setzt
        // beides vor jeder Simulation aus den Inlets. materials ist nullptr,
        // wenn die Materialdatei fehlt - gerechnet wird dann trotzdem, nur
        // ohne Stoffwerte (z.B. Mischtemperatur nach Masse gewichtet).
        std::vector<std::string> compounds;
        const CMaterialDatabase* materials = nullptr;

        // Mindestens ein Stoff-Platz, auch ohne Inlet.
        size_t GetNumCompounds() const { return std::max<size_t>(compounds.size(), 1); }

        // Index eines Stoffs; 0, wenn er nicht aktiv ist.
        size_t FindCompound(const std::string& name) const
        {
            const auto it = std::find(compounds.begin(), compounds.end(), name);
            return it != compounds.end() ? static_cast<size_t>(it - compounds.begin()) : 0;
        }

        // Hoechstzahl an Zeitpunkten (Speicher: je Ergebnis Zeitpunkte mal Groessenklassen).
        static constexpr size_t kMaxTimePoints = 1000000;

        // Zeitpunkte 0 ... totalTime fuer eine gewuenschte Schrittweite dt:
        // round(totalTime / dt) + 1, zwischen 2 und kMaxTimePoints. Geht die Dauer
        // nicht genau auf, wird dt leicht angepasst (GetDt liefert den echten Wert).
        static size_t TimePointsFor(double totalTime, double dt)
        {
            if (!(totalTime > 0.0) || !(dt > 0.0))
                return 2;
            const double steps = std::round(totalTime / dt);
            if (steps >= static_cast<double>(kMaxTimePoints - 1))
                return kMaxTimePoints;
            return std::max<size_t>(2, static_cast<size_t>(steps) + 1);
        }

        // Laenge eines Makroschritts.
        double GetDt() const
        {
            return (numTimeSteps > 1) ? totalTime / static_cast<double>(numTimeSteps - 1) : 0.0;
        }

        SSizeGrid GetSizeGrid() const { return { sizeGridType, numSizeClasses, sizeMin, sizeMax }; }

        double GetSizeAt(size_t sizeIdx) const { return GetSizeGrid().GetSizeAt(sizeIdx); }
    };

    // Was eine Unit in einem Makroschritt ueber die Zeit wissen muss.
    // Schritt k rechnet die Unit vom Zeitpunkt time - dt auf den Zeitpunkt
    // time = k * dt weiter. Schritt 0 ist der Anfangszustand bei t = 0.
    struct SStepContext
    {
        size_t stepIndex = 0;
        double time = 0.0;
        double dt = 0.0;
    };

    // Grundschnittstelle ALLER Einheiten im Flowsheet (Inlet, Outlet,
    // Prozesse, Splitter, Merger, ...). Die Simulation laeuft in
    // Makroschritten: In jedem Schritt ruft der Graph die Units in
    // Flussrichtung nacheinander auf, jede bekommt die Stroeme an ihren
    // Eingaengen und schreibt die Stroeme an ihren Ausgaengen.
    //
    // Was zwischen zwei Schritten in der Unit steckt (Hold-up: Partikel
    // in der Muehle, Material in einer Verzoegerungsstrecke, ...), ist
    // ihr innerer Zustand. Initialize() setzt ihn vor jeder Simulation
    // zurueck. Eine Unit darf innerhalb eines Makroschritts beliebig viele
    // eigene kleinere Teilschritte machen (Multi-Rate, z.B. ereignis-
    // gesteuertes MC) - nach aussen zaehlt nur der Zustand am Schrittende.
    class IFlowsheetUnit
    {
    public:
        virtual ~IFlowsheetUnit() = default;

        // Vor jeder Simulation: Raster uebernehmen, Zustand leeren.
        // unitSeed = eigener Seed dieser Unit (aus globalem Seed und
        // Knoten-ID); deterministische Units ignorieren ihn.
        virtual void Initialize(const SSimulationGrid& grid, std::uint64_t unitSeed) = 0;

        // Ein Makroschritt. inputs[i] gehoert zu Eingangs-Port i und ist
        // nullptr, wenn der Port nicht verbunden ist (zaehlt als leerer
        // Strom). outputs hat bereits die richtige Groesse (ein Eintrag pro
        // Ausgangs-Port, passend zum Raster, mit 0 gefuellt). Im
        // Partikelmodus tragen alle Scheiben Karten statt Dichten.
        virtual void Step(const SStepContext& context,
            const std::vector<const CStreamSlice*>& inputs,
            std::vector<CStreamSlice>& outputs) = 0;

        // Kann die Unit im Partikelmodus rechnen, also Karten lesen und
        // schreiben? Welche Units es koennen, steht im
        // Methoden-Dokument (Kap. 15). Solange eine Unit im Flowsheet es
        // nicht kann, startet der Graph keine Simulation im Partikelmodus und
        // sagt, woran es liegt - statt still falsch zu rechnen.
        virtual bool SupportsParticles() const { return false; }

        // Haengt das Ergebnis vom Zufall ab? Nur dann lohnen sich mehrere
        // Laeufe - ein rein deterministisches Flowsheet rechnet der Graph
        // nur einmal.
        virtual bool IsStochastic() const { return false; }

        // Hinweise aus der letzten Simulation (z.B. fehlende Stoffwerte),
        // je Hinweis ein Satz. Der Graph sammelt sie mit dem Knotennamen ein.
        virtual std::vector<std::string> GetWarnings() const { return {}; }

        // Kurzbeschreibung nach Initialize fuer das Laufzeitprotokoll, z.B.
        // "2000 Karten je Zeitschritt in 3 Groessenbereichen". Leer = nichts.
        virtual std::string DescribeForLog() const { return {}; }

        // Passt ein Groessenraster zu dieser Unit? Je Problem ein Satz (z.B.
        // Inlet: Teil der Verteilung liegt ausserhalb). Der Reiter "Raster"
        // ruft das schon vor der Simulation mit den eingegebenen Werten auf.
        // Spaeter melden hier auch Keimbildung (Keimgroesse) und Bruch
        // (kleinste Bruchstuecke) ihren Bedarf (Methoden-Dokument 2.6).
        virtual std::vector<std::string> CheckSizeGrid(const SSizeGrid& /*sizes*/) const { return {}; }
    };
}
