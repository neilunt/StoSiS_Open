#pragma once
#include <string>

namespace stosis
{
    // Reine C++ Deklaration, absichtlich ohne jeden CUDA-spezifischen
    // Typ, damit diese Datei problemlos auch von normalem, nicht mit
    // nvcc kompiliertem Code (z.B. der Qt-GUI) eingebunden werden kann.
    // Fragt die verfuegbaren CUDA-Geraete ab und schreibt eine
    // lesbare Zusammenfassung nach outInfo. Gibt false zurueck, wenn
    // kein CUDA-faehiges Geraet gefunden wurde.
    bool GetCudaDeviceInfo(std::string& outInfo);
}