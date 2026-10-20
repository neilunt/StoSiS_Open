#pragma once
#include <vector>
#include <string>
#include "FlowsheetUnit.h"
#include "ParameterDescriptor.h"

namespace stosis
{
    // Schnittstelle fuer Prozess-Einheiten mit einem Eingang und einem
    // Ausgang (Verzoegerung, spaeter Crusher, Agglomerator, ...), deren
    // Modell im Einstellungsfenster auswaehlbar ist. Gerechnet wird wie
    // bei allen Units Schritt fuer Schritt ueber IFlowsheetUnit; hier
    // kommen nur Name und einstellbare Parameter dazu.
    // (Im Gegensatz zu ITransportUnit, das eine einmalige Startverteilung
    // bei t=0 zu einer Impulsantwort verrechnet.)
    class IStreamProcessor : public IFlowsheetUnit
    {
    public:
        virtual std::string GetTypeName() const = 0;
        virtual std::vector<SParameterDescriptor> GetParameterDescriptors() const = 0;
        virtual void SetParameter(const std::string& id, double value) = 0;
        virtual double GetParameterValue(const std::string& id) const = 0;
    };
}
