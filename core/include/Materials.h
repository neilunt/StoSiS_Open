#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace stosis
{
    // Stoffdaten aus data/Materialien.json (Aufbau: data/README.md). Alle
    // Werte in SI-Einheiten: Temperatur T in K, Druck p in Pa.

    // Universelle Gaskonstante [J/(mol K)], CODATA 2018 (exakt). Dyssol
    // nutzt 8.314459848 (CODATA 2014) - relativer Unterschied 3e-7.
    inline constexpr double kGasConstant = 8.314462618;

    // Startwerte fuer Temperatur und Druck: Raumbedingungen.
    inline constexpr double kRoomTemperature = 293.15; // K
    inline constexpr double kRoomPressure = 101325.0;  // Pa

    // Bezugstemperatur der Enthalpie: h(298.15 K) = 0. Fuer Energiebilanzen
    // ohne Stoffumwandlung zaehlen nur Differenzen, der Bezug kuerzt sich.
    inline constexpr double kEnthalpyReferenceTemperature = 298.15; // K

    // Art eines Eintrags; in der Datei steht der Name als "type".
    enum class ECorrelationType
    {
        Constant,              // "constant":      y = value
        TableT,                // "table_T":       Messpunkte [T, y], linear interpoliert
        TableRelativeHumidity, // "table_relative_humidity": Messpunkte [rel. Luftfeuchte, y]
        IdealGas,              // "ideal_gas":     y = M p / (R T)
        Sutherland,            // "sutherland":    y = y_ref (T_ref + S)/(T + S) (T/T_ref)^1.5
        Antoine,               // "antoine":       y = 1e5 * 10^(A - B/(T + C))
        Polynomial,            // "polynomial":    y = a0 + a1 T + a2 T^2 + ...
        PolynomialCp           // "polynomial_cp": y = a + b T + c T^2 + d T^3 + e/T^2
    };

    // Ein Eintrag einer temperatur-/druckabhaengigen Eigenschaft: Formel oder
    // Messpunkt-Tabelle mit Gueltigkeitsbereich.
    struct SCorrelation
    {
        ECorrelationType type = ECorrelationType::Constant;
        std::string typeName; // wie in der Datei, z.B. "sutherland"
        std::string formula;  // lesbare Formel aus der Datei (optional)

        // constant: {value}; ideal_gas: {M}; sutherland: {y_ref, T_ref, S};
        // antoine: {A, B, C}; polynomial / polynomial_cp: Koeffizienten.
        std::vector<double> parameters;
        // Tabellen: Punkte [x, y], nach x sortiert.
        std::vector<std::pair<double, double>> points;

        double TMin = 0.0;
        double TMax = 0.0;
        // Druckbereich; bei TableRelativeHumidity der Feuchtebereich.
        double pMin = 0.0;
        double pMax = 0.0;

        std::string unit;
        std::string source;
        std::string status; // "unchecked" / "checked"
        std::string reference;
        std::string note;

        bool DependsOnHumidity() const { return type == ECorrelationType::TableRelativeHumidity; }
        bool IsTInRange(double T) const { return T >= TMin && T <= TMax; }
        bool IsPInRange(double p) const { return DependsOnHumidity() || (p >= pMin && p <= pMax); }
        bool IsInRange(double T, double p) const { return IsTInRange(T) && IsPInRange(p); }
        // Bei Tabellen decken die Messpunkte den Gueltigkeitsbereich nicht
        // immer ab (z.B. Wasserdichte in Dyssol: Bereich bis 1973 K, Punkte
        // bis 372.76 K); jenseits des letzten Punkts gilt dessen Wert.
        bool IsOutsidePoints(double T) const
        {
            return type == ECorrelationType::TableT && !points.empty() &&
                (T < points.front().first || T > points.back().first);
        }

        // Wert bei T und p. Ausserhalb des Gueltigkeitsbereichs wird wie in
        // Dyssol der Randwert genommen (kein Extrapolieren). Eintraege, die
        // von der Luftfeuchte abhaengen, liefern hier NaN.
        double Evaluate(double T, double p) const
        {
            if (DependsOnHumidity())
                return std::numeric_limits<double>::quiet_NaN();
            T = std::clamp(T, TMin, TMax);
            p = std::clamp(p, pMin, pMax);

            const std::vector<double>& a = parameters;
            switch (type)
            {
            case ECorrelationType::Constant:
                return a[0];
            case ECorrelationType::TableT:
                return InterpolateTable(T);
            case ECorrelationType::IdealGas:
                return a[0] * p / (kGasConstant * T);
            case ECorrelationType::Sutherland:
                return a[0] * (a[1] + a[2]) / (T + a[2]) * std::pow(T / a[1], 1.5);
            case ECorrelationType::Antoine:
                return 1.0e5 * std::pow(10.0, a[0] - a[1] / (T + a[2]));
            case ECorrelationType::Polynomial:
            {
                double value = 0.0;
                for (size_t i = a.size(); i-- > 0;) // Horner-Schema
                    value = value * T + a[i];
                return value;
            }
            case ECorrelationType::PolynomialCp:
                return a[0] + a[1] * T + a[2] * T * T + a[3] * T * T * T + a[4] / (T * T);
            case ECorrelationType::TableRelativeHumidity:
                break;
            }
            return std::numeric_limits<double>::quiet_NaN();
        }

        // Lineare Interpolation in den Punkten; jenseits des ersten/letzten
        // Punkts gilt dessen Wert.
        double InterpolateTable(double x) const
        {
            if (points.empty())
                return std::numeric_limits<double>::quiet_NaN();
            if (x <= points.front().first)
                return points.front().second;
            if (x >= points.back().first)
                return points.back().second;
            const auto upper = std::upper_bound(points.begin(), points.end(), x,
                [](double value, const std::pair<double, double>& point) { return value < point.first; });
            const auto lower = upper - 1;
            const double f = (x - lower->first) / (upper->first - lower->first);
            return lower->second + f * (upper->second - lower->second);
        }
    };

    // Ergebnis einer Abfrage: Wert, ob vorhanden, ob ausserhalb des
    // Gueltigkeitsbereichs bzw. der Messpunkte (dann Randwert) und welcher
    // Eintrag benutzt wurde.
    struct SPropertyValue
    {
        double value = std::numeric_limits<double>::quiet_NaN();
        bool found = false;
        bool outOfRange = false;
        const SCorrelation* correlation = nullptr;
    };

    // Eine Eigenschaft = Liste von Eintraegen fuer verschiedene Bereiche.
    struct SProperty
    {
        std::vector<SCorrelation> correlations;

        // Auswahl wie in Dyssol: zuerst ein Eintrag mit T und p im Bereich,
        // dann einer mit T im Bereich, sonst der mit der naechstgelegenen
        // T-Grenze.
        const SCorrelation* Select(double T, double p) const
        {
            for (const SCorrelation& c : correlations)
                if (c.IsInRange(T, p))
                    return &c;
            for (const SCorrelation& c : correlations)
                if (c.IsTInRange(T))
                    return &c;
            const SCorrelation* nearest = nullptr;
            double nearestDistance = std::numeric_limits<double>::infinity();
            for (const SCorrelation& c : correlations)
            {
                const double distance = std::min(std::abs(c.TMin - T), std::abs(c.TMax - T));
                if (distance < nearestDistance)
                {
                    nearestDistance = distance;
                    nearest = &c;
                }
            }
            return nearest;
        }

        SPropertyValue Evaluate(double T, double p) const
        {
            SPropertyValue result;
            result.correlation = Select(T, p);
            if (!result.correlation)
                return result;
            result.found = true;
            result.outOfRange = !result.correlation->IsInRange(T, p) || result.correlation->IsOutsidePoints(T);
            result.value = result.correlation->Evaluate(T, p);
            return result;
        }
    };

    // Integral von c_p(T) zwischen T0 und T1 [J/kg] (Simpson-Regel mit 64
    // Teilintervallen - exakt fuer konstantes bis kubisches c_p; Tabellen
    // und Bereichsgrenzen werden wie bei jeder Abfrage behandelt).
    inline double IntegrateHeatCapacity(const SProperty& heatCapacity, double T0, double T1, double p)
    {
        constexpr int kIntervals = 64;
        const double h = (T1 - T0) / kIntervals;
        if (h == 0.0)
            return 0.0;
        double sum = heatCapacity.Evaluate(T0, p).value + heatCapacity.Evaluate(T1, p).value;
        for (int i = 1; i < kIntervals; ++i)
            sum += (i % 2 ? 4.0 : 2.0) * heatCapacity.Evaluate(T0 + i * h, p).value;
        return sum * h / 3.0;
    }

    struct SConstant
    {
        double value = 0.0;
        std::string unit;
        std::string source;
        std::string status;
        std::string reference;
    };

    struct SMaterial
    {
        std::string name;
        std::string dyssolKey; // Dyssol-Flowsheets (.dflw) sprechen Stoffe darueber an
        std::string state;     // "solid" / "liquid" / "gas"
        std::string stateSource;
        std::vector<std::string> warnings;
        std::vector<std::string> notes;
        // Reihenfolge wie in der Datei.
        std::vector<std::pair<std::string, SConstant>> constants;
        std::vector<std::pair<std::string, SProperty>> properties;

        const SConstant* FindConstant(const std::string& key) const
        {
            for (const auto& entry : constants)
                if (entry.first == key)
                    return &entry.second;
            return nullptr;
        }

        const SProperty* FindProperty(const std::string& key) const
        {
            for (const auto& entry : properties)
                if (entry.first == key)
                    return &entry.second;
            return nullptr;
        }

        // Z.B. GetProperty("density", T, p).value; found == false, wenn der
        // Stoff die Eigenschaft nicht hat.
        SPropertyValue GetProperty(const std::string& key, double T, double p) const
        {
            const SProperty* property = FindProperty(key);
            return property ? property->Evaluate(T, p) : SPropertyValue{};
        }

        // Spezifische Enthalpie h(T) [J/kg] relativ zu 298.15 K, berechnet
        // aus der Waermekapazitaet (gespeichert wird nur c_p). false, wenn
        // der Stoff keine Waermekapazitaet hat.
        bool GetSpecificEnthalpy(double T, double p, double& h) const
        {
            const SProperty* heatCapacity = FindProperty("heat_capacity");
            if (!heatCapacity)
                return false;
            h = IntegrateHeatCapacity(*heatCapacity, kEnthalpyReferenceTemperature, T, p);
            return true;
        }
    };

    // Eigenschaft zweier Stoffe (z.B. Grenzflaechenspannung Wasser/Luft).
    struct SInteraction
    {
        std::string material1;
        std::string material2;
        std::vector<std::pair<std::string, SProperty>> properties;
    };

    // Liest data/Materialien.json (Implementierung in MaterialDatabase.cpp,
    // nur dort wird die JSON-Bibliothek gebraucht).
    class CMaterialDatabase
    {
    public:
        // Bei Fehlern bleibt der bisherige Inhalt erhalten, und error
        // beschreibt das Problem (Syntaxfehler mit Zeile und Spalte, sonst
        // Stoff, Eigenschaft und Feld).
        bool LoadFromFile(const std::string& path, std::string& error);
        bool LoadFromText(const std::string& text, std::string& error);

        const std::vector<SMaterial>& GetMaterials() const { return m_materials; }
        const std::vector<SInteraction>& GetInteractions() const { return m_interactions; }
        const std::string& GetFilePath() const { return m_filePath; }

        const SMaterial* FindByName(const std::string& name) const
        {
            for (const SMaterial& material : m_materials)
                if (material.name == name)
                    return &material;
            return nullptr;
        }

        const SMaterial* FindByDyssolKey(const std::string& key) const
        {
            for (const SMaterial& material : m_materials)
                if (material.dyssolKey == key)
                    return &material;
            return nullptr;
        }

    private:
        std::vector<SMaterial> m_materials;
        std::vector<SInteraction> m_interactions;
        std::string m_filePath;
    };
}
