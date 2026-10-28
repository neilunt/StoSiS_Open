#pragma once
#include <vector>
#include <cstddef>
#include <algorithm>
#include "Materials.h"

namespace stosis
{
    // Wie das Material in den Stroemen dargestellt wird - gilt fuer die ganze
    // Simulation, ab dem Inlet (Methoden-Dokument 13.16: "einmal Partikel
    // heisst ueberall Partikel"):
    //   Continuum: Massenstromdichte je Stoff auf dem Groessenraster,
    //   Particles: Karten (gewichtete MC-Partikel) mit freier Groesse.
    enum class ERepresentation
    {
        Continuum,
        Particles
    };

    inline const char* GetRepresentationLabel(ERepresentation representation)
    {
        return representation == ERepresentation::Particles ? "Partikel" : "Kontinuum";
    }

    // Ein Strom zu EINEM Zeitpunkt: Massenstromdichte je Stoff und
    // Groessenklasse (gleiches knotenbasiertes Groessenraster wie
    // CSimulationResults), dazu Temperatur und Druck des Stroms. Das ist das
    // "Paket", das die Units in jedem Makroschritt austauschen. Bewusst als
    // eigene Klasse statt nacktem Vektor: im Partikelmodus traegt dieselbe
    // Klasse statt der Dichte eine Liste von Karten, ohne dass sich die
    // Schnittstelle der Units aendert.
    //
    // Stoffe sind ueber ihren Index in SSimulationGrid::compounds
    // angesprochen (die aktiven Stoffe des Flowsheets).
    //
    // Karten (nur im Partikelmodus): Jede Karte steht fuer viele gleiche
    // Partikel und hat eine Groesse d [m] und einen Massenstrom je Stoff
    // [kg/s] - die Masse, die sie in einer Sekunde durch den Strom traegt.
    // Mehrere Stoffe in einer Karte entstehen erst durch Koagulation; bis
    // dahin hat jede Karte genau einen Stoff. Die Partikelzahl (das
    // "Gewicht" der Karte) folgt aus Masse, Groesse und Dichte und wird erst
    // fuer die Koagulation gebraucht. Das Alter einer Karte merkt sich erst
    // der Vorrat einer Unit (13.19), im Strom haben alle Karten dieselbe Zeit.
    class CStreamSlice
    {
    public:
        void Resize(size_t numSizeClasses, size_t numCompounds = 1,
            ERepresentation representation = ERepresentation::Continuum)
        {
            m_numSizeClasses = numSizeClasses;
            m_numCompounds = std::max<size_t>(numCompounds, 1);
            m_representation = representation;
            m_values.assign(m_numCompounds * m_numSizeClasses, 0.0);
            m_cardSizes.clear();
            m_cardMassFlows.clear();
            ResetConditions();
        }

        // Kein Material mehr; Temperatur und Druck zurueck auf Raumbedingungen.
        void Clear()
        {
            std::fill(m_values.begin(), m_values.end(), 0.0);
            m_cardSizes.clear(); // Speicher bleibt reserviert
            m_cardMassFlows.clear();
            ResetConditions();
        }

        size_t GetNumSizeClasses() const { return m_numSizeClasses; }
        size_t GetNumCompounds() const { return m_numCompounds; }
        ERepresentation GetRepresentation() const { return m_representation; }
        bool HasCards() const { return m_representation == ERepresentation::Particles; }

        double GetTemperature() const { return m_temperature; }
        double GetPressure() const { return m_pressure; }
        void SetTemperature(double temperature) { m_temperature = temperature; }
        void SetPressure(double pressure) { m_pressure = pressure; }

        // --- Kontinuum ------------------------------------------------------
        // Die Funktionen bis zum Abschnitt "Karten" gelten nur im Kontinuum.

        // Gesamte Massenstromdichte (alle Stoffe) in Groessenklasse sizeIdx.
        double GetValue(size_t sizeIdx) const
        {
            double sum = m_values[sizeIdx];
            for (size_t c = 1; c < m_numCompounds; ++c)
                sum += m_values[c * m_numSizeClasses + sizeIdx];
            return sum;
        }

        double GetValue(size_t compound, size_t sizeIdx) const { return m_values[compound * m_numSizeClasses + sizeIdx]; }
        void SetValue(size_t compound, size_t sizeIdx, double value) { m_values[compound * m_numSizeClasses + sizeIdx] = value; }

        // Massenstrom eines Stoffs: Trapezregel ueber die Groesse mit den
        // Gewichten des Rasters (SSizeGrid::GetWeights, wie CSimulationResults::
        // IntegrateOverSize). Fuer Anteile und Energiebilanzen.
        double GetCompoundWeight(size_t compound, const std::vector<double>& sizeWeights) const
        {
            if (m_numSizeClasses == 0)
                return 0.0;
            const double* row = &m_values[compound * m_numSizeClasses];
            const size_t numS = std::min(m_numSizeClasses, sizeWeights.size());
            double sum = 0.0;
            for (size_t s = 0; s < numS; ++s)
                sum += sizeWeights[s] * row[s];
            return sum;
        }

        double GetTotalWeight(const std::vector<double>& sizeWeights) const
        {
            double sum = 0.0;
            for (size_t c = 0; c < m_numCompounds; ++c)
                sum += GetCompoundWeight(c, sizeWeights);
            return sum;
        }

        // this = other * factor (z.B. Splitter-Anteil); T und p bleiben.
        // Nicht fuer Karten: jede Karte zu skalieren waere das "Kopieren"
        // (13.18) - im Partikelmodus verteilt der Splitter die Karten mit dem
        // Konto-Verfahren.
        void AssignScaled(const CStreamSlice& other, double factor)
        {
            m_numSizeClasses = other.m_numSizeClasses;
            m_numCompounds = other.m_numCompounds;
            m_values.resize(other.m_values.size());
            for (size_t i = 0; i < m_values.size(); ++i)
                m_values[i] = other.m_values[i] * factor;
            m_temperature = other.m_temperature;
            m_pressure = other.m_pressure;
        }

        // this += other je Stoff und Groessenklasse (nur die Mengen; T und p
        // bestimmt, wer mischt - siehe CMergerUnit). Bei unterschiedlicher
        // Groesse wird nur der gemeinsame Bereich addiert.
        void Add(const CStreamSlice& other)
        {
            const size_t numC = std::min(m_numCompounds, other.m_numCompounds);
            const size_t numS = std::min(m_numSizeClasses, other.m_numSizeClasses);
            for (size_t c = 0; c < numC; ++c)
                for (size_t s = 0; s < numS; ++s)
                    m_values[c * m_numSizeClasses + s] += other.m_values[c * other.m_numSizeClasses + s];
        }

        const std::vector<double>& GetValues() const { return m_values; }

        // --- Karten (Partikelmodus) -----------------------------------------

        size_t GetNumCards() const { return m_cardSizes.size(); }
        double GetCardSize(size_t card) const { return m_cardSizes[card]; }

        double GetCardMassFlow(size_t card, size_t compound) const
        {
            return m_cardMassFlows[card * m_numCompounds + compound];
        }

        // Massenstrom einer Karte ueber alle Stoffe.
        double GetCardMassFlow(size_t card) const
        {
            double sum = 0.0;
            for (size_t c = 0; c < m_numCompounds; ++c)
                sum += m_cardMassFlows[card * m_numCompounds + c];
            return sum;
        }

        // Neue Karte aus einem einzigen Stoff (so erzeugen sie die Inlets).
        void AddCard(double size, size_t compound, double massFlow)
        {
            m_cardSizes.push_back(size);
            m_cardMassFlows.insert(m_cardMassFlows.end(), m_numCompounds, 0.0);
            if (compound < m_numCompounds)
                m_cardMassFlows[(m_cardSizes.size() - 1) * m_numCompounds + compound] = massFlow;
        }

        // Neue Karte mit Massenstrom je Stoff (nach Koagulation); fehlende
        // Stoffe zaehlen als 0, ueberzaehlige werden ignoriert.
        void AddCard(double size, const std::vector<double>& compoundMassFlows)
        {
            m_cardSizes.push_back(size);
            for (size_t c = 0; c < m_numCompounds; ++c)
                m_cardMassFlows.push_back(c < compoundMassFlows.size() ? compoundMassFlows[c] : 0.0);
        }

        // Eine Karte aus einer anderen Scheibe uebernehmen (Splitter). Bei
        // unterschiedlicher Stoffzahl zaehlt der gemeinsame Teil.
        void AppendCard(const CStreamSlice& from, size_t card)
        {
            m_cardSizes.push_back(from.m_cardSizes[card]);
            for (size_t c = 0; c < m_numCompounds; ++c)
                m_cardMassFlows.push_back(c < from.m_numCompounds ? from.m_cardMassFlows[card * from.m_numCompounds + c] : 0.0);
        }

        // Alle Karten einer anderen Scheibe anhaengen (Merger).
        void AppendCards(const CStreamSlice& from)
        {
            if (from.m_numCompounds == m_numCompounds)
            {
                m_cardSizes.insert(m_cardSizes.end(), from.m_cardSizes.begin(), from.m_cardSizes.end());
                m_cardMassFlows.insert(m_cardMassFlows.end(), from.m_cardMassFlows.begin(), from.m_cardMassFlows.end());
                return;
            }
            for (size_t card = 0; card < from.GetNumCards(); ++card)
                AppendCard(from, card);
        }

        // Alle Karten ersetzen (Grenzen je Stromscheibe): Groessen [m] und
        // Massenstroeme als [Karte * Stoffe + Stoff].
        void SetCards(std::vector<double> sizes, std::vector<double> massFlows)
        {
            m_cardSizes = std::move(sizes);
            m_cardMassFlows = std::move(massFlows);
            m_cardMassFlows.resize(m_cardSizes.size() * m_numCompounds, 0.0);
        }

        // Massenstrom eines Stoffs ueber alle Karten.
        double GetCardsMassFlow(size_t compound) const
        {
            double sum = 0.0;
            for (size_t card = 0; card < m_cardSizes.size(); ++card)
                sum += m_cardMassFlows[card * m_numCompounds + compound];
            return sum;
        }

        // Massenstrom aller Karten und Stoffe.
        double GetCardsMassFlow() const
        {
            double sum = 0.0;
            for (double massFlow : m_cardMassFlows)
                sum += massFlow;
            return sum;
        }

    private:
        void ResetConditions()
        {
            m_temperature = kRoomTemperature;
            m_pressure = kRoomPressure;
        }

        size_t m_numSizeClasses = 0;
        size_t m_numCompounds = 1;
        ERepresentation m_representation = ERepresentation::Continuum;
        // [Stoff * numSizeClasses + Groessenklasse]
        std::vector<double> m_values;
        // Karten: Groesse [m] je Karte, Massenstrom [kg/s] als [Karte * numCompounds + Stoff]
        std::vector<double> m_cardSizes;
        std::vector<double> m_cardMassFlows;
        double m_temperature = kRoomTemperature; // K
        double m_pressure = kRoomPressure;       // Pa
    };
}
