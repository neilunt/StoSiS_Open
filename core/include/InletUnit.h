#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <cstdio>
#include "FlowsheetUnit.h"
#include "SizeDistribution.h"
#include "CardPlacement.h"

namespace stosis
{
    // Ein einzelner Stuetzpunkt im Zeitprofil des Inlets: zu Zeitpunkt
    // "time" betraegt der Massenstrom "massFlow". Zwischen zwei
    // benachbarten Stuetzpunkten wird linear interpoliert.
    struct SInletTimePoint
    {
        double time = 0.0;
        double massFlow = 0.0;
    };

    // Startprofil jedes neuen Inlets - und das Profil, auf das der Knopf
    // "Anfangsprofil wiederherstellen" im Inlet-Editor zuruecksetzt.
    // Hier aendern, wenn ein anderes Standardprofil gewuenscht ist.
    inline std::vector<SInletTimePoint> GetDefaultInletTimeProfile()
    {
        return {
            {0.0, 0.0},
            {2.0, 20.0},
            {4.0, 0.0},
            {6.0, 15.0},
            {7.0, 10.0},
            {12.0, 0.0},
            {18.0, 10.0},
            {20.0, 0.0},
            {30.0, 20.0},
            {33.0, 10.0},
        };
    }

    // Stoff neuer Inlets (Name aus der Materialdatei).
    inline constexpr const char* kDefaultInletMaterial = "Sand";

    // Quelle eines Flowsheets: liefert in jedem Makroschritt den
    // Eingangsstrom. Der Massenstrom folgt einem stueckweise-linearen
    // Zeitprofil, die Groessenverteilung (Normal, Log-Normal oder RRSB)
    // ist zu jedem Zeitpunkt dieselbe Form, nur ihre Gesamthoehe skaliert
    // mit dem aktuellen Massenstrom. Ein Inlet liefert genau EINEN Stoff mit
    // fester Temperatur und festem Druck - Mischungen entstehen ueber mehrere
    // Inlets und Merger (so laesst sich das Mischverhaeltnis zeitlich
    // steuern). Kein Eingang, ein Ausgang.
    //
    // Im Partikelmodus liefert das Inlet in jedem Schritt N Karten an den
    // Quantilen der Verteilung mit Mindestzahl je Groessenbereich
    // (PlaceCardsAtQuantiles, Methoden-Dokument 13.21). Groessen und
    // Massenanteile stehen einmal fest, jeder Schritt skaliert nur mit dem
    // Massenstrom - die Karten tragen ihn exakt.
    class CInletUnit : public IFlowsheetUnit
    {
    public:
        void SetTimeProfile(std::vector<SInletTimePoint> points)
        {
            m_timePoints = std::move(points);
        }

        const std::vector<SInletTimePoint>& GetTimeProfile() const { return m_timePoints; }

        // Kurzform fuer eine Normalverteilung (Mittelwert d0, Breite sigma0, beide in m).
        // Fuer Partikelgroessen meist ungeeignet (GetDistributionWarning); genutzt in Tests.
        void SetSizeDistribution(double d0, double sigma0)
        {
            m_distribution = { EDistributionType::Normal, d0, sigma0 };
        }

        void SetSizeDistribution(const SSizeDistribution& distribution)
        {
            m_distribution = distribution;
        }

        const SSizeDistribution& GetSizeDistribution() const { return m_distribution; }

        // Stoff (Name aus der Materialdatei), Temperatur [K], Druck [Pa].
        void SetMaterial(const std::string& name) { m_material = name; }
        const std::string& GetMaterial() const { return m_material; }
        void SetTemperature(double temperature) { m_temperature = temperature; }
        double GetTemperature() const { return m_temperature; }
        void SetPressure(double pressure) { m_pressure = pressure; }
        double GetPressure() const { return m_pressure; }

        // Lineare Interpolation zwischen den Stuetzpunkten. Vor dem
        // ersten bzw. nach dem letzten Stuetzpunkt wird der jeweilige
        // Randwert konstant fortgesetzt.
        double GetMassFlowAtTime(double time) const
        {
            if (m_timePoints.empty())
                return 0.0;
            if (time <= m_timePoints.front().time)
                return m_timePoints.front().massFlow;
            if (time >= m_timePoints.back().time)
                return m_timePoints.back().massFlow;

            for (size_t i = 0; i + 1 < m_timePoints.size(); ++i)
            {
                const SInletTimePoint& a = m_timePoints[i];
                const SInletTimePoint& b = m_timePoints[i + 1];
                if (time >= a.time && time <= b.time)
                {
                    const double frac = (time - a.time) / (b.time - a.time);
                    return a.massFlow + frac * (b.massFlow - a.massFlow);
                }
            }
            return 0.0; // sollte wegen der Randpruefungen oben nie erreicht werden
        }

        void Initialize(const SSimulationGrid& grid, std::uint64_t /*unitSeed*/) override
        {
            // Form der Verteilung ist zeitlich konstant -> einmal pro
            // Groessenklasse auswerten statt in jedem Schritt. Masse je
            // Stuetzstelle exakt aus Q3 (MassesAtNodes), geteilt durch das
            // Trapezgewicht: So traegt der Strom genau Q3(d_max) - Q3(d_min)
            // des Massenstroms, auch bei Spruengen der Dichte (GGS, Tabelle).
            const SSizeGrid sizes = grid.GetSizeGrid();
            const std::vector<double> masses = m_distribution.MassesAtNodes(sizes);
            m_shape.assign(grid.numSizeClasses, 0.0);
            for (size_t s = 0; s < grid.numSizeClasses && s < masses.size(); ++s)
                m_shape[s] = masses[s] / sizes.GetWeight(s);
            m_compound = grid.FindCompound(m_material);

            // Karten: Partikelzahl wird im Bereich des Rasters gezaehlt (ab der
            // kleinsten positiven Stuetzstelle), siehe PlaceCardsAtQuantiles.
            m_particles = grid.representation == ERepresentation::Particles;
            m_cards = SCardPlacement();
            m_cardSettings = grid.cards;
            if (m_particles)
            {
                const double countMin = (grid.sizeMin > 0.0 || grid.numSizeClasses < 2) ? grid.sizeMin : grid.GetSizeAt(1);
                m_cards = PlaceCardsAtQuantiles(m_distribution, grid.cards, countMin, grid.sizeMax);
            }
        }

        bool SupportsParticles() const override { return true; }

        // Karten der letzten Initialisierung im Partikelmodus (sonst leer).
        const SCardPlacement& GetCardPlacement() const { return m_cards; }

        std::vector<std::string> GetWarnings() const override
        {
            std::vector<std::string> warnings;
            if (!m_particles)
                return warnings;
            char text[260];
            if (m_cards.sizes.empty())
                warnings.push_back("Keine Karten erzeugt - Groessenverteilung oder Karteneinstellungen ungueltig.");
            else if (m_cards.minPerRange < kMinCardsPerRange)
            {
                std::snprintf(text, sizeof(text), "Nur %zu Karten je belegtem Groessenbereich (%zu Karten je Zeitschritt, "
                    "%zu belegte Bereiche) - zu wenig fuer eine brauchbare Statistik. Karten je Zeitschritt erhoehen.",
                    m_cards.minPerRange, m_cardSettings.cardsPerSlice, m_cards.numOccupied);
                warnings.push_back(text);
            }
            return warnings;
        }

        // Die Karten entstehen mit Mindestzahl und Sollzahl je Bereich.
        bool OutputsWithinCardLimits() const override { return true; }

        std::string DescribeForLog() const override
        {
            char text[200];
            const char* type = GetDistributionParameterInfo(m_distribution.type).typeName;
            if (m_particles)
                std::snprintf(text, sizeof(text), "%s, %zu Karten je Zeitschritt in %zu Groessenbereichen (Mindestzahl %zu)",
                    type, m_cards.sizes.size(), m_cards.numOccupied, m_cards.minPerRange);
            else
                std::snprintf(text, sizeof(text), "%s auf dem Groessenraster", type);
            return text;
        }

        // Darunter warnt das Inlet (Unsicherheit eines Bereichs etwa 1/sqrt(n), 13.19).
        static constexpr size_t kMinCardsPerRange = 10;

        // Meldet, wenn mehr als 0,1 % der Verteilung unter d_min oder ueber
        // d_max liegen (exakt aus der Verteilungssumme Q3).
        std::vector<std::string> CheckSizeGrid(const SSizeGrid& sizes) const override
        {
            std::vector<std::string> notes;
            const auto note = [&](double fraction, const char* where, double size)
            {
                if (fraction <= kSizeGridTolerance)
                    return;
                // Ab 10 % ganze Prozent (sonst schriebe %g 100 % als 1e+02).
                const double percent = fraction * 100.0;
                char text[160];
                std::snprintf(text, sizeof(text), percent >= 10.0 ? "%.0f %% der Groessenverteilung liegen %s (%g %s)."
                    : "%.2g %% der Groessenverteilung liegen %s (%g %s).",
                    percent, where, size * kSizeDisplayFactor, kSizeDisplayUnit);
                notes.push_back(text);
            };
            note(m_distribution.FractionBelow(sizes.sizeMin), "unter d_min", sizes.sizeMin);
            note(m_distribution.FractionAbove(sizes.sizeMax), "ueber d_max", sizes.sizeMax);
            return notes;
        }

        // Ab diesem Anteil ausserhalb meldet CheckSizeGrid etwas.
        static constexpr double kSizeGridTolerance = 1e-3;

        void Step(const SStepContext& context, const std::vector<const CStreamSlice*>& /*inputs*/,
            std::vector<CStreamSlice>& outputs) override
        {
            if (outputs.empty())
                return;
            const double massFlow = GetMassFlowAtTime(context.time);
            CStreamSlice& out = outputs[0];
            out.SetTemperature(m_temperature);
            out.SetPressure(m_pressure);
            if (out.HasCards())
            {
                if (massFlow != 0.0)
                    for (size_t k = 0; k < m_cards.sizes.size(); ++k)
                        out.AddCard(m_cards.sizes[k], m_compound, massFlow * m_cards.massFractions[k]);
                return;
            }
            for (size_t s = 0; s < m_shape.size() && s < out.GetNumSizeClasses(); ++s)
                out.SetValue(m_compound, s, massFlow * m_shape[s]);
        }

    private:
        std::vector<SInletTimePoint> m_timePoints = GetDefaultInletTimeProfile();
        SSizeDistribution m_distribution; // Standard: Log-Normal, d50 = 100 um, sigma_ln = 0,25
        std::string m_material = kDefaultInletMaterial;
        double m_temperature = kRoomTemperature; // K
        double m_pressure = kRoomPressure;       // Pa
        size_t m_compound = 0; // Index in SSimulationGrid::compounds

        std::vector<double> m_shape; // Verteilungsdichte pro Groessenklasse

        bool m_particles = false;     // Partikelmodus in der letzten Initialisierung
        SCardSettings m_cardSettings;
        SCardPlacement m_cards;       // Groessen und Massenanteile der Karten
    };
}
