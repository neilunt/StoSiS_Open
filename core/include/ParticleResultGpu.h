#pragma once

namespace stosis
{
    // Ergebnis eines einzelnen simulierten Partikels, wie es von der
    // GPU zurueckkommt. Bewusst ein einfaches POD-Struct (kein CUDA-
    // spezifischer Typ), damit diese Datei auch von normalem C++-Code
    // (z.B. der Qt-GUI) ohne nvcc eingebunden werden kann.
    struct SParticleResultGpu
    {
        double exitTime = 0.0;
        double size = 0.0;
        double weight = 0.0;
        bool exited = false;
    };
}