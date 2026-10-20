#pragma once
#include <vector>
#include <cstddef>

namespace stosis
{
    // Verbindung zwischen zwei Units. Traegt jetzt einen Wertevektor
    // (ein Wert pro Groessenklasse) statt nur einem einzelnen
    // Massenstrom. Fuer den einfachen linearen Flowsheet-Demo aus
    // Schritt 2 (Source -> Scaler -> Sink) bleibt SetMassFlow/GetMassFlow
    // unveraendert nutzbar - das ist einfach der Spezialfall "ein Wert"
    // (Index 0). Neue, groessenverteilte Units (Diffusion/Drift) nutzen
    // stattdessen SetValues/GetValues fuer den vollen Vektor.
    class CStream
    {
    public:
        // --- Skalar-API (Schritt 2, Source->Scaler->Sink) ---------------
        double GetMassFlow() const { return m_values.empty() ? 0.0 : m_values[0]; }
        void SetMassFlow(double massFlow)
        {
            if (m_values.empty())
                m_values.resize(1);
            m_values[0] = massFlow;
        }

        // --- Vektor-API (groessenverteilte Units) ------------------------
        const std::vector<double>& GetValues() const { return m_values; }
        void SetValues(std::vector<double> values) { m_values = std::move(values); }
        size_t GetNumClasses() const { return m_values.size(); }

    private:
        std::vector<double> m_values;
    };
}
