#pragma once

// Kleine Hilfen fuer die Tests des Rechenkerns. Bewusst ohne Test-
// Framework: jedes Testprogramm prueft mit Check(...), gibt jede Pruefung
// als OK/FAIL aus und endet mit Exit-Code 0 (alles bestanden) oder 1.
// CTest wertet nur den Exit-Code aus.
//
// Stochastische Tests laufen mit festem Seed und sind damit reproduzierbar:
// Die Toleranzen sind aus dem statistischen Fehler begruendet (meist etwa
// 3 bis 5 Standardfehler), ein Lauf schlaegt aber nie "zufaellig" fehl.

#include <cmath>
#include <cstdio>
#include <string>

namespace stosis_test
{
    inline int& FailureCount()
    {
        static int failures = 0;
        return failures;
    }

    inline void Check(bool ok, const std::string& what)
    {
        std::printf("%s  %s\n", ok ? "OK  " : "FAIL", what.c_str());
        if (!ok)
            ++FailureCount();
    }

    inline bool NearAbs(double value, double expected, double tolerance)
    {
        return std::abs(value - expected) <= tolerance;
    }

    inline bool NearRel(double value, double expected, double relTolerance)
    {
        return std::abs(value - expected) <= relTolerance * std::abs(expected);
    }

    // Pfad einer Datei im Ordner data/ des Projekts (von CMake gesetzt).
    inline std::string DataFile(const char* name)
    {
        return std::string(STOSIS_DATA_DIR) + "/" + name;
    }

    // Am Ende von main(): Zusammenfassung und Exit-Code.
    inline int Summary()
    {
        const int failures = FailureCount();
        std::printf("\n%s (%d Fehler)\n", failures == 0 ? "ALLE TESTS BESTANDEN" : "TESTS FEHLGESCHLAGEN", failures);
        return failures == 0 ? 0 : 1;
    }
}
