#pragma once
#include <cstdint>
#include <random>
#include <string>
#include <cmath>

namespace stosis
{
    // Zufallszahlen fuer alle stochastischen Units.
    //
    // Reproduzierbarkeit auch zwischen Windows (MSVC) und Linux (GCC, z.B.
    // auf dem Grossrechner): Der Generator mt19937_64 ist im C++-Standard
    // exakt festgelegt, liefert also ueberall dieselbe Folge.
    // std::normal_distribution dagegen ist NICHT festgelegt - jede
    // Standardbibliothek formt die Zahlen anders um. Deshalb sind Gleich-
    // und Normalverteilung hier selbst implementiert.

    // FNV-1a: einfacher, ueberall gleicher String-Hash (std::hash ist je
    // nach Compiler verschieden und taugt daher nicht fuer Seeds).
    inline std::uint64_t HashString(const std::string& text)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned char c : text)
        {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    // SplitMix64: verruehrt die Bits einer Zahl gruendlich. Benachbarte
    // Eingaben (Seed 1, Seed 2) ergeben voellig verschiedene Ausgaben.
    inline std::uint64_t MixBits(std::uint64_t x)
    {
        x += 0x9E3779B97F4A7C15ull;
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }

    // Eigener Seed einer Unit aus dem globalen Seed und ihrer Knoten-ID.
    // Bewusst verruehrt statt addiert: bei "Seed + ID" haetten Seed 1 /
    // Knoten 2 und Seed 2 / Knoten 1 denselben Zufallsstrom.
    inline std::uint64_t MakeUnitSeed(std::uint64_t globalSeed, const std::string& nodeId)
    {
        return MixBits(globalSeed ^ MixBits(HashString(nodeId)));
    }

    // Globaler Seed fuer Lauf runIndex (0-basiert) bei mehreren Laeufen.
    // Lauf 0 nutzt genau den eingestellten Seed - ein einzelner Lauf ist
    // damit identisch zu "Seed X, 1 Lauf", und die Splitter-Vorschau
    // zeigt weiterhin exakt Lauf 1.
    inline std::uint64_t MakeRunSeed(std::uint64_t globalSeed, std::uint64_t runIndex)
    {
        return runIndex == 0 ? globalSeed : MixBits(MixBits(globalSeed) ^ runIndex);
    }

    class CRandom
    {
    public:
        explicit CRandom(std::uint64_t seed = 1) : m_engine(seed) {}

        void Seed(std::uint64_t seed)
        {
            m_engine.seed(seed);
            m_hasSpareNormal = false;
        }

        // Gleichverteilt in [0, 1) mit voller double-Genauigkeit (53 Bit).
        double Uniform()
        {
            return static_cast<double>(m_engine() >> 11) * (1.0 / 9007199254740992.0);
        }

        // Standardnormalverteilt N(0, 1), Polarmethode nach Marsaglia.
        // Liefert pro Durchgang zwei Werte; der zweite wird aufgehoben.
        double Normal()
        {
            if (m_hasSpareNormal)
            {
                m_hasSpareNormal = false;
                return m_spareNormal;
            }
            double u = 0.0, v = 0.0, s = 0.0;
            do
            {
                u = 2.0 * Uniform() - 1.0;
                v = 2.0 * Uniform() - 1.0;
                s = u * u + v * v;
            } while (s >= 1.0 || s == 0.0);
            const double factor = std::sqrt(-2.0 * std::log(s) / s);
            m_spareNormal = v * factor;
            m_hasSpareNormal = true;
            return u * factor;
        }

    private:
        std::mt19937_64 m_engine;
        bool m_hasSpareNormal = false;
        double m_spareNormal = 0.0;
    };
}
