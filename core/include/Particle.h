#pragma once

namespace stosis
{
    // Ein einzelnes (virtuelles) Partikel bzw. Partikelpaket. Bewusst
    // minimal gehalten: nur die drei Felder, die aktuelle
    // Transport-Modelle wirklich brauchen. Weitere Felder
    // (Geschwindigkeit, Temperatur, Druck, Komposition) kommen erst
    // dazu, sobald ein konkreter Prozess sie tatsaechlich benoetigt,
    // z.B. Koaleszenz braucht zusaetzlich kein neues Feld ausser den
    // dreien hier, waehrend z.B. Energiebilanzen Temperatur brauchen
    // wuerden.
    struct SParticle
    {
        double position = 0.0; // axiale Position im Rohr (fuer FPT-Modelle)
        double size = 0.0;     // Partikelgroesse, KEIN Bin-Index mehr,
        // sondern ein freier Fliesskommawert
        double weight = 1.0;   // repraesentierte Partikelanzahl (bei
        // gewichteten MC-Paketen), bei
        // ungewichteten Partikeln einfach 1.0
    };
}