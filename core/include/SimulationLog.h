#pragma once
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace stosis
{
    // Stoppuhr fuer Laufzeitmessungen (monotone Uhr).
    class CStopwatch
    {
    public:
        CStopwatch() : m_start(std::chrono::steady_clock::now()) {}
        void Restart() { m_start = std::chrono::steady_clock::now(); }
        double GetSeconds() const
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
        }

    private:
        std::chrono::steady_clock::time_point m_start;
    };

    // Ohne Optimierung uebersetzt (Debug-Fassung)? Im Partikelmodus rechnet
    // sie etwa sechsmal langsamer als die Release-Fassung (Methoden Kap. 15).
    inline bool IsDebugBuild()
    {
#ifdef NDEBUG
        return false;
#else
        return true;
#endif
    }

    // Zahl mit Dezimalkomma, z.B. FormatDecimal(1.25, 2) = "1,25".
    inline std::string FormatDecimal(double value, int decimals)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "%.*f", decimals, value);
        std::string result = text;
        for (char& c : result)
            if (c == '.')
                c = ',';
        return result;
    }

    // Zahl mit hoechstens digits gueltigen Stellen und Dezimalkomma, ohne
    // angehaengte Nullen, z.B. FormatNumber(0.2, 6) = "0,2".
    inline std::string FormatNumber(double value, int digits)
    {
        char text[64];
        std::snprintf(text, sizeof(text), "%.*g", digits, value);
        std::string result = text;
        for (char& c : result)
            if (c == '.')
                c = ',';
        return result;
    }

    // Dauer lesbar: unter 1 ms in us, unter 1 s in ms, sonst in s.
    inline std::string FormatDuration(double seconds)
    {
        if (seconds < 1e-3)
            return FormatDecimal(seconds * 1e6, 0) + " \xC2\xB5s";
        if (seconds < 1.0)
            return FormatDecimal(seconds * 1e3, seconds < 0.1 ? 1 : 0) + " ms";
        return FormatDecimal(seconds, seconds < 10.0 ? 2 : 1) + " s";
    }

    // Feste Zeiteinheit fuer mehrere Dauern nebeneinander, z.B. eine Zeile
    // der Tabelle "Zeiten je Unit": us, solange der groesste Wert unter 1 ms
    // liegt, sonst ms.
    enum class ETimeUnit
    {
        Microseconds,
        Milliseconds
    };

    inline ETimeUnit ChooseTimeUnit(double maxSeconds)
    {
        return maxSeconds < 1e-3 ? ETimeUnit::Microseconds : ETimeUnit::Milliseconds;
    }

    // Dauer in fester Einheit, us mit einer und ms mit drei Nachkommastellen
    // (also ebenfalls auf 1 us genau), z.B. "12,3 us" oder "12,346 ms".
    inline std::string FormatDurationIn(double seconds, ETimeUnit unit)
    {
        if (unit == ETimeUnit::Microseconds)
            return FormatDecimal(seconds * 1e6, 1) + " \xC2\xB5s";
        return FormatDecimal(seconds * 1e3, 3) + " ms";
    }

    // Stand einer laufenden Simulation (RunSimulations): fertige Laeufe und
    // Zeitschritte des laufenden Laufs.
    struct SSimulationProgress
    {
        size_t runsDone = 0;
        size_t runs = 0;
        size_t stepsDone = 0;     // im laufenden Lauf
        size_t steps = 0;         // Zeitpunkte je Lauf
        double elapsedSeconds = 0.0;
        bool runFinished = false; // Meldung am Ende eines Laufs

        // Anteil der ganzen Rechnung, 0 ... 1.
        double GetFraction() const
        {
            const double total = static_cast<double>(runs) * static_cast<double>(steps);
            if (!(total > 0.0))
                return 0.0;
            return (static_cast<double>(runsDone) * static_cast<double>(steps) + static_cast<double>(stepsDone)) / total;
        }

        // Geschaetzte Restzeit aus dem bisherigen Tempo; -1 = noch unbekannt.
        double GetRemainingSeconds() const
        {
            const double fraction = GetFraction();
            if (!(fraction > 0.0) || !(elapsedSeconds > 0.0))
                return -1.0;
            return elapsedSeconds * (1.0 - fraction) / fraction;
        }
    };

    // Rechenzeit einer Unit, aufsummiert ueber alle Laeufe einer Simulation.
    struct SUnitTiming
    {
        std::string nodeId;
        std::string name;
        std::string type;
        double initSeconds = 0.0;   // Initialize
        double stepSeconds = 0.0;   // Step (Outlet: Aufzeichnen des Eingangs samt Umrechnung)
        double recordSeconds = 0.0; // Ausgaenge aufzeichnen (Partikelmodus: Karten aufs Raster)
        size_t steps = 0;
        double cards = 0.0;         // Karten an den Ausgaengen (Outlet: am Eingang), ueber alle Schritte

        double GetTotalSeconds() const { return initSeconds + stepSeconds + recordSeconds; }
        double GetCardsPerStep() const { return steps > 0 ? cards / static_cast<double>(steps) : 0.0; }
    };

    // Laufzeiten der letzten Simulation.
    struct SSimulationTiming
    {
        double totalSeconds = 0.0;
        double statisticsSeconds = 0.0; // Statistik ueber die Laeufe sammeln
        double historySeconds = 0.0;    // Karten fuer die Kartenansicht speichern
        size_t runs = 0;                // fertige Laeufe
        size_t steps = 0;               // Zeitpunkte je Lauf
        bool particles = false;
        bool cancelled = false;
        std::vector<SUnitTiming> units; // in Ausfuehrungsreihenfolge

        double GetUnitsSeconds() const
        {
            double sum = 0.0;
            for (const SUnitTiming& unit : units)
                sum += unit.GetTotalSeconds();
            return sum;
        }
    };

    // Textprotokoll einer Simulation: je Zeile die Zeit seit dem Start und
    // was gerade beginnt oder fertig ist (Lauf, Initialisierung, Unit,
    // Statistik). Sehr lange Protokolle werden nach kMaxLines gekappt.
    class CSimulationLog
    {
    public:
        struct SLine
        {
            double seconds = 0.0;
            std::string text;
        };

        static constexpr size_t kMaxLines = 20000;

        void Start()
        {
            m_lines.clear();
            m_omitted = 0;
            m_clock.Restart();
        }

        void Add(const std::string& text)
        {
            if (m_lines.size() >= kMaxLines)
            {
                ++m_omitted;
                return;
            }
            m_lines.push_back({ m_clock.GetSeconds(), text });
        }

        double GetElapsedSeconds() const { return m_clock.GetSeconds(); }
        const std::vector<SLine>& GetLines() const { return m_lines; }
        size_t GetOmittedLines() const { return m_omitted; }

        // Alle Zeilen als Text, je Zeile "[  0,012 s] ..."; mit decimals = 6
        // stehen die Zeitstempel auf 1 us genau da ("[  0,012345 s] ...").
        std::string ToText(int decimals = 3) const
        {
            std::string text;
            char stamp[48];
            for (const SLine& line : m_lines)
            {
                std::snprintf(stamp, sizeof(stamp), "[%*.*f s] ", decimals + 6, decimals, line.seconds);
                for (char* c = stamp; *c; ++c)
                    if (*c == '.')
                        *c = ',';
                text += stamp;
                text += line.text;
                text += '\n';
            }
            if (m_omitted > 0)
                text += "... " + std::to_string(m_omitted) + " weitere Zeilen ausgelassen\n";
            return text;
        }

    private:
        std::vector<SLine> m_lines;
        size_t m_omitted = 0;
        CStopwatch m_clock;
    };
}
