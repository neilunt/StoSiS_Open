#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>
#include "SizeDistribution.h"

namespace stosis
{
    // Auswertung schwankender Zeitreihen (z.B. Massenstrom eines Ausgangs je
    // Zeitschritt) wie bei Messungen ueblich: Mittel ueber Zeitfenster,
    // optionaler Ausreisserfilter, Kennzahlen, Histogramm und
    // Wahrscheinlichkeitsnetz (Methoden-Dokument Kapitel 12 und 15).
    // Genutzt vom Fenster "Auswertung" der Oberflaeche und vom
    // Auswerteprogramm tests/analysis_dosierwaage.cpp - beide liefern damit
    // dieselben Zahlen.

    // Zeitschritte je Mittelungsfenster: windowTime / dt gerundet, mindestens 1.
    inline size_t StepsPerWindow(double dt, double windowTime)
    {
        if (dt <= 0.0 || windowTime <= dt)
            return 1;
        return static_cast<size_t>(std::max(1.0, std::round(windowTime / dt)));
    }

    // Mittelwerte ueber aufeinanderfolgende Fenster aus stepsPerWindow
    // Zeitschritten ab Schritt firstStep. Schritt k steht fuer das Intervall
    // [t_k, t_k + dt) (Rechteckregel wie eine Waage, die alle dt abliest);
    // der letzte Zeitpunkt liegt am Ende der Simulation und zaehlt deshalb
    // nicht. Unvollstaendige Fenster am Ende fallen weg.
    inline std::vector<double> AverageOverWindows(const std::vector<double>& series, size_t firstStep, size_t stepsPerWindow)
    {
        std::vector<double> means;
        if (stepsPerWindow == 0)
            return means;
        for (size_t k = firstStep; k + stepsPerWindow < series.size(); k += stepsPerWindow)
        {
            double sum = 0.0;
            for (size_t i = 0; i < stepsPerWindow; ++i)
                sum += series[k + i];
            means.push_back(sum / static_cast<double>(stepsPerWindow));
        }
        return means;
    }

    struct SSignalStatistics
    {
        size_t count = 0;
        double mean = 0.0;
        double stdDev = 0.0;          // Stichprobe, Teiler n - 1
        double rsd = 0.0;             // relative Standardabweichung stdDev / mean
        double skewness = 0.0;
        double excessKurtosis = 0.0;  // 0 fuer eine Normalverteilung
        double lag1Correlation = 0.0; // Korrelation benachbarter Werte (Hinweis auf die Korrelationszeit)
    };

    inline SSignalStatistics ComputeSignalStatistics(const std::vector<double>& x)
    {
        SSignalStatistics s;
        s.count = x.size();
        if (x.empty())
            return s;
        const double n = static_cast<double>(x.size());
        double sum = 0.0;
        for (double v : x)
            sum += v;
        s.mean = sum / n;
        if (x.size() < 2)
            return s;
        double m2 = 0.0, m3 = 0.0, m4 = 0.0, c = 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double d = x[i] - s.mean;
            m2 += d * d; m3 += d * d * d; m4 += d * d * d * d;
            if (i + 1 < x.size())
                c += d * (x[i + 1] - s.mean);
        }
        m2 /= n; m3 /= n; m4 /= n;
        s.stdDev = std::sqrt(m2 * n / (n - 1.0));
        s.rsd = s.mean != 0.0 ? s.stdDev / s.mean : 0.0;
        if (m2 > 0.0)
        {
            s.skewness = m3 / std::pow(m2, 1.5);
            s.excessKurtosis = m4 / (m2 * m2) - 3.0;
            s.lag1Correlation = c / (n - 1.0) / m2;
        }
        return s;
    }

    // Ausreisserfilter nach Engisch & Muzzio (2012, Abschnitt 3.6): erst alle
    // Werte ausserhalb setpoint +- band * setpoint verwerfen, dann wiederholt
    // alle ausserhalb mean +- sigmaLimit * stdDev, bis sich nichts mehr
    // aendert. removed = Zahl der verworfenen Werte.
    inline std::vector<double> FilterLikeEngischMuzzio(const std::vector<double>& values, double setpoint,
        double band, double sigmaLimit, size_t& removed)
    {
        std::vector<double> kept;
        for (double v : values)
            if (std::abs(v - setpoint) <= band * std::abs(setpoint))
                kept.push_back(v);
        for (;;)
        {
            const SSignalStatistics s = ComputeSignalStatistics(kept);
            std::vector<double> next;
            for (double v : kept)
                if (std::abs(v - s.mean) <= sigmaLimit * s.stdDev)
                    next.push_back(v);
            if (next.size() == kept.size())
                break;
            kept.swap(next);
        }
        removed = values.size() - kept.size();
        return kept;
    }

    // Histogramm als Dichte: Anteil je Klasse / Klassenbreite (Flaeche 1).
    struct SHistogram
    {
        double start = 0.0;
        double width = 1.0;
        std::vector<double> density;

        double GetLower(size_t i) const { return start + static_cast<double>(i) * width; }
    };

    inline SHistogram MakeHistogram(const std::vector<double>& values, double start, double width, size_t numBins)
    {
        SHistogram h;
        h.start = start;
        h.width = width;
        h.density.assign(numBins, 0.0);
        if (values.empty() || width <= 0.0)
            return h;
        const double perValue = 1.0 / (static_cast<double>(values.size()) * width);
        for (double v : values)
        {
            const double pos = (v - start) / width;
            if (pos >= 0.0 && pos < static_cast<double>(numBins))
                h.density[static_cast<size_t>(pos)] += perValue;
        }
        return h;
    }

    // Glatte Klassenbreite (1, 2, 2,5 oder 5 mal 10^k) fuer etwa
    // 2 * n^(1/3) Klassen (Rice-Regel, 10 bis 60) ueber [minValue, maxValue].
    inline void ChooseHistogramBins(double minValue, double maxValue, size_t numValues,
        double& start, double& width, size_t& numBins)
    {
        const double range = maxValue - minValue;
        if (!(range > 0.0) || numValues == 0)
        {
            width = std::max(std::abs(minValue) * 1e-3, 1e-12);
            start = minValue - 0.5 * width;
            numBins = 1;
            return;
        }
        const double wanted = std::clamp(2.0 * std::cbrt(static_cast<double>(numValues)), 10.0, 60.0);
        const double raw = range / wanted;
        const double decade = std::pow(10.0, std::floor(std::log10(raw)));
        width = decade * 5.0;
        for (double f : { 1.0, 2.0, 2.5, 5.0 })
            if (raw <= f * decade)
            {
                width = f * decade;
                break;
            }
        start = std::floor(minValue / width) * width;
        numBins = static_cast<size_t>(std::floor((maxValue - start) / width)) + 1;
    }

    // Quantil der Standardnormalverteilung (StandardNormalQuantile in
    // SizeDistribution.h, etwa 1e-15 genau); p wird auf (0, 1) begrenzt.
    inline double NormalQuantile(double p)
    {
        return StandardNormalQuantile(std::clamp(p, 1e-300, 1.0 - 1e-16));
    }

    // Wahrscheinlichkeitsnetz: sortierte Werte gegen das Normalquantil ihres
    // Rangs, z_i = Phi^-1((i + 0.5) / n). Liegen die Punkte auf einer
    // Geraden, sind die Werte normalverteilt.
    inline std::vector<std::pair<double, double>> MakeProbabilityPlot(std::vector<double> values)
    {
        std::sort(values.begin(), values.end());
        std::vector<std::pair<double, double>> points;
        points.reserve(values.size());
        const double n = static_cast<double>(values.size());
        for (size_t i = 0; i < values.size(); ++i)
            points.emplace_back(values[i], NormalQuantile((static_cast<double>(i) + 0.5) / n));
        return points;
    }
}
