#pragma once
#include "Unit.h"
#include "Stream.h"

namespace stosis
{
    // Die "Input"-Auswahl im Menue. Erzeugt aktuell einen konstanten
    // Massenstrom. Spaeter koennte es mehrere Source-Typen geben (z.B.
    // zeitabhaengiger oder gepulster Input wie bei Dyssol-Gate-Units) -
    // deshalb als eigene Klasse statt Spezialfall.
    class CSourceUnit : public CUnit
    {
    public:
        explicit CSourceUnit(std::string name) : CUnit(std::move(name)) {}

        void SetMassFlow(double massFlow) { m_massFlow = massFlow; }

        CStream* GetOutputStream() { return &m_output; }

        void Simulate(double /*timeBeg*/, double /*timeEnd*/) override
        {
            m_output.SetMassFlow(m_massFlow);
        }

    private:
        double m_massFlow = 0.0;
        CStream m_output;
    };
}
