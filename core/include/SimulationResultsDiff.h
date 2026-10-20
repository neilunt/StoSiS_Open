#pragma once
#include <algorithm>
#include "SimulationResults.h"

namespace stosis 
{

    // Berechnet die Differenz zweier Ergebnis-Matrizen Zelle fuer Zelle
    // (a minus b). Setzt voraus, dass beide dasselbe Zeit- und
    // Groessengitter haben - das ist bei der Vergleichsansicht
    // garantiert, weil dort beide Modelle mit demselben gemeinsamen
    // Gitter simuliert werden. Rein rechnerisch, kennt keine GUI.

    inline CSimulationResults ComputeDifference(const CSimulationResults& a, const CSimulationResults& b)
    {
        CSimulationResults diff;

        // Falls die Gitter doch nicht zusammenpassen (z.B. weil eine
        // Seite noch gar nicht simuliert wurde), lieber eine leere
        // Matrix zurueckgeben als mit falschen Indizes zuzugreifen.
        if (a.GetNumTimeSteps() != b.GetNumTimeSteps() || a.GetNumSizeClasses() != b.GetNumSizeClasses())
        {
            return diff;
        }

        diff.Configure(a.GetNumTimeSteps(), a.GetDt(),
            a.GetNumSizeClasses(), a.GetSizeMin(), a.GetSizeMax());

        for (size_t t = 0; t < a.GetNumTimeSteps(); ++t)
            for (size_t s = 0; s < a.GetNumSizeClasses(); ++s)
                diff.SetValue(t, s, a.GetValue(t, s) - b.GetValue(t, s));

        return diff;
    }

}