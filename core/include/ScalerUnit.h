#pragma once
#include "Unit.h"
#include "Stream.h"

namespace stosis
{
    // Die "Unit"-Auswahl im Menue. Aktuell simpelste denkbare
    // Verarbeitung: skaliert den Massenstrom mit einem Faktor. Das ist
    // absichtlich trivial - der Punkt dieses Schritts ist der
    // Datenfluss Input -> Unit -> Output, nicht die Physik. Deine
    // echten Transport-Units (TIS, StochasticRTD, ...) ersetzen das
    // hier spaeter als weitere CUnit-Subklassen, ohne dass sich am
    // Flowsheet-Aufbau etwas aendert.
    class CScalerUnit : public CUnit
    {
    public:
        explicit CScalerUnit(std::string name) : CUnit(std::move(name)) {}

        void SetInputStream(CStream* input) { m_input = input; }
        void SetFactor(double factor) { m_factor = factor; }

        CStream* GetOutputStream() { return &m_output; }

        void Simulate(double /*timeBeg*/, double /*timeEnd*/) override
        {
            if (m_input)
                m_output.SetMassFlow(m_input->GetMassFlow() * m_factor);
        }

    private:
        CStream* m_input = nullptr;
        double m_factor = 1.0;
        CStream m_output;
    };
}
