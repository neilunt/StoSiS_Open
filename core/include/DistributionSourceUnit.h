#pragma once
#include <cmath>
#include <vector>
#include "Unit.h"
#include "Stream.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace stosis
{
    // Input-Unit fuer groessenverteilte Simulationen: erzeugt eine
    // Gauss-Startverteilung ueber ein festes Groessenraster. Analog zu
    // CSourceUnit (Schritt 2), aber liefert einen Vektor statt eines
    // Einzelwerts - deshalb eine eigene Klasse statt Erweiterung von
    // CSourceUnit, um den bestehenden Schritt-2-Workflow nicht
    // anzufassen.
    class CDistributionSourceUnit : public CUnit
    {
    public:
        explicit CDistributionSourceUnit(std::string name) : CUnit(std::move(name)) {}

        void ConfigureGrid(size_t numClasses, double sizeMin, double sizeMax)
        {
            m_numClasses = numClasses;
            m_sizeMin = sizeMin;
            m_sizeMax = sizeMax;
        }

        // totalMass = Gesamtmasse/-anzahl unter der Verteilung. Die
        // Amplitude wird so skaliert, dass die Flaeche unter der
        // diskretisierten Kurve (Summe * dx) ungefaehr totalMass ergibt -
        // sonst haengt die sichtbare Peak-Hoehe willkuerlich von der
        // gewaehlten Klassenzahl ab.
        void SetInitialGaussian(double d0, double sigma0, double totalMass)
        {
            m_d0 = d0;
            m_sigma0 = sigma0;
            m_totalMass = totalMass;
        }

        CStream* GetOutputStream() { return &m_output; }

        void Simulate(double /*timeBeg*/, double /*timeEnd*/) override
        {
            std::vector<double> values(m_numClasses, 0.0);
            const double norm = m_totalMass / std::sqrt(2.0 * M_PI * m_sigma0 * m_sigma0);

            for (size_t s = 0; s < m_numClasses; ++s)
            {
                const double frac = static_cast<double>(s) / static_cast<double>(m_numClasses - 1);
                const double size = m_sizeMin + frac * (m_sizeMax - m_sizeMin);
                const double diff = size - m_d0;
                values[s] = norm * std::exp(-(diff * diff) / (2.0 * m_sigma0 * m_sigma0));
            }
            m_output.SetValues(std::move(values));
        }

    private:
        size_t m_numClasses = 0;
        double m_sizeMin = 0.0;
        double m_sizeMax = 0.0;
        double m_d0 = 0.0;
        double m_sigma0 = 1.0;
        double m_totalMass = 1.0;
        CStream m_output;
    };
}
