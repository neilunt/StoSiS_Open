#pragma once
#include "Unit.h"
#include "Stream.h"

namespace stosis
{
    // Die "Output"-Auswahl im Menue. Nimmt einen Strom entgegen und
    // haelt ihn zum Auslesen bereit (bei Dyssol wuerde das Ergebnis
    // hier z.B. geplottet oder exportiert werden).
    class CSinkUnit : public CUnit
    {
    public:
        explicit CSinkUnit(std::string name) : CUnit(std::move(name)) {}

        void SetInputStream(CStream* input) { m_input = input; }

        double GetMassFlow() const { return m_input ? m_input->GetMassFlow() : 0.0; }

        void Simulate(double /*timeBeg*/, double /*timeEnd*/) override
        {
            // Nichts zu tun - der Sink liest nur passiv seinen Input.
        }

    private:
        CStream* m_input = nullptr;
    };
}
