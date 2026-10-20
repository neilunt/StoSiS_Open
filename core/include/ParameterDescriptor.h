#pragma once
#include <string>

namespace stosis
{
    // Beschreibt einen einzelnen einstellbaren Parameter einer Einheit.
    // Gemeinsam genutzt von ITransportUnit und IStreamProcessor.
    struct SParameterDescriptor
    {
        std::string id;
        std::string label;
        double minValue;
        double maxValue;
        double defaultValue;
        // Anzeige in der Oberflaeche = Wert * displayFactor (z.B. Groessen:
        // intern m, angezeigt in um mit kSizeDisplayFactor). minValue,
        // maxValue und defaultValue sind immer in der internen Einheit.
        double displayFactor = 1.0;
    };
}