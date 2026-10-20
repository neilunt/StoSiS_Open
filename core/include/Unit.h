#pragma once
#include <string>

namespace stosis
{
    // Basisklasse fuer alle Units (Mischer, Transport-Einheiten, etc.).
    // Bewusst noch sehr minimal - erst mal nur genug, damit Flowsheet
    // etwas hat, das es verwalten kann. Die eigentliche Simulate()-Logik
    // und die Partikelgruppen-Anbindung kommen im naechsten Schritt,
    // wenn wir das lineare Flowsheet (Input -> Unit -> Output) bauen.
    class CUnit
    {
    public:
        explicit CUnit(std::string name) : m_name(std::move(name)) {}
        virtual ~CUnit() = default;

        const std::string& GetName() const { return m_name; }

        // Platzhalter - Signatur orientiert sich bewusst an
        // CDynamicUnit::Simulate(double, double) aus Dyssol, damit uns
        // das spaetere Einlesen von Dyssol-Flowsheets leichter faellt.
        virtual void Simulate(double timeBeg, double timeEnd) = 0;

    private:
        std::string m_name;
    };
}
