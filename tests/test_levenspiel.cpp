// Kompartiment-Modelle nach Levenspiel (1999) und Mischer nach Engisch und
// Muzzio (2016) aus den Flowsheet-Dateien in Flowsheets/Validierungs-Flowsheets
// (Methodenhandbuch 11.3, 11.4): Tracerpuls von 1 kg im ersten Zeitschritt,
// Ablauf gegen die exakte Verweilzeitverteilung E(t), im Kontinuum und im
// Partikelmodus. Mit einem Ordner als Argument schreibt das Programm je
// Ablauf eine CSV-Datei (t; exakt; Kontinuum; Partikel; jeweils E und F).
//
// B2: dry granulation line after Martinetz et al. (2018), API mass fraction
// after a step 10 -> 20 % at the roller compactor and the tablet press against
// the line model of the paper (lags in series plus dead time, closed form).
//
// B3: disturbances of the API feeder after Kruisz et al. (2017), API mass
// fraction after the blender and the roller compactor against the closed form
// and the diversion times of Tab. 6 there; blender after Eq. 6 with Tab. 4 and
// as one lag of 90 s fitted to Tab. 6.
//
// B4: blender after Gao et al. (2011), measured feed rates (read from Fig. 4b/c)
// through plug flow plus stirred tank with the mean and spread of the
// dispersion model of the paper; standard deviations against Tab. 1 there.
//
// Bezug: Der Puls fuellt das Intervall [0, dt], Ausgang k ist die Masse,
// die in [k dt, (k + 1) dt] austritt, geteilt durch dt. Exakt ist damit
//   r_k = 1/dt^2 * Integral_0^dt (F((k+1) dt - u) - F(k dt - u)) du
// fuer den stetigen Teil und w/dt im Schritt T/dt fuer einen Puls w bei T.
#include "FlowsheetFile.h"
#include "TestSupport.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>

using namespace stosis;
using namespace stosis_test;

namespace
{
    // Exakte Verweilzeitverteilung: Pulse (Zeit, Gewicht) plus stetiger
    // Teil, gegeben als Summenkurve Fc(t) (0 fuer t <= 0).
    struct SExact
    {
        std::vector<std::pair<double, double>> pulses;
        std::function<double(double)> Fc = [](double) { return 0.0; };
    };

    std::vector<double> Reference(const SExact& exact, size_t steps, double dt)
    {
        std::vector<double> r(steps, 0.0);
        const int m = 200;
        for (size_t k = 0; k < steps; ++k)
        {
            double sum = 0.0;
            for (int j = 0; j < m; ++j)
            {
                const double u = (j + 0.5) * dt / m;
                sum += exact.Fc((k + 1) * dt - u) - exact.Fc(k * dt - u);
            }
            r[k] = sum / m / dt;
        }
        for (const auto& pulse : exact.pulses)
        {
            const long long q = std::llround(pulse.first / dt);
            if (q >= 0 && static_cast<size_t>(q) < steps)
                r[static_cast<size_t>(q)] += pulse.second / dt;
        }
        return r;
    }

    // Masse, Mittel und Standardabweichung der Verweilzeit aus einer
    // Schrittfolge (Eingangskasten und Ausgangsintervall herausgerechnet).
    struct SMoments
    {
        double mass = 0.0;
        double mean = 0.0;
        double sd = 0.0;
    };

    SMoments Moments(const std::vector<double>& e, double dt)
    {
        SMoments m;
        double first = 0.0;
        double second = 0.0;
        for (size_t k = 0; k < e.size(); ++k)
        {
            const double t = (k + 0.5) * dt;
            m.mass += e[k] * dt;
            first += e[k] * dt * t;
            second += e[k] * dt * t * t;
        }
        const double meanOut = first / m.mass;
        m.mean = meanOut - 0.5 * dt;
        m.sd = std::sqrt(std::max(0.0, second / m.mass - meanOut * meanOut - dt * dt / 6.0));
        return m;
    }

    struct SRun
    {
        std::vector<double> e;
        bool ok = false;
        std::string error;
    };

    // Datei laden, Darstellung setzen, rechnen, Ablauf der Outlets nach Namen.
    std::map<std::string, SRun> Simulate(const std::string& file, bool particles, double& dt)
    {
        std::map<std::string, SRun> runs;
        CFlowsheetGraph graph;
        const SFlowsheetReadResult read = LoadFlowsheetFile(std::string(STOSIS_VALIDATION_DIR) + "/" + file, graph);
        if (!read.ok || !read.warnings.empty())
        {
            runs[""].error = read.ok ? "Hinweise beim Lesen: " + read.warnings.front() : read.error;
            return runs;
        }
        graph.SetRepresentation(particles ? ERepresentation::Particles : ERepresentation::Continuum);
        graph.RunSimulations(1);
        dt = graph.GetGrid().GetDt();
        for (const SFlowsheetNode& node : graph.GetNodes())
        {
            if (node.type != ENodeType::Outlet)
                continue;
            SRun& run = runs[node.name];
            const CSimulationResults* results = graph.GetDisplayResults(node.id, 0);
            if (!results)
                continue;
            for (size_t k = 0; k < results->GetNumTimeSteps(); ++k)
                run.e.push_back(results->IntegrateOverSize(k));
            run.ok = true;
        }
        return runs;
    }

    double MaxDeviation(const std::vector<double>& a, const std::vector<double>& b)
    {
        double peak = 0.0;
        double dev = 0.0;
        for (size_t k = 0; k < a.size() && k < b.size(); ++k)
        {
            peak = std::max(peak, std::abs(b[k]));
            dev = std::max(dev, std::abs(a[k] - b[k]));
        }
        return peak > 0.0 ? dev / peak : dev;
    }

    void WriteCsv(const std::string& dir, const std::string& name, double dt, const std::vector<double>& exact,
        const std::vector<double>& continuum, const std::vector<double>& particles, const std::vector<double>& ideal)
    {
        std::ofstream file(dir + "/" + name + ".csv");
        file << "t_s;E_exakt;E_kontinuum;E_partikel;F_exakt;F_kontinuum;F_partikel;F_levenspiel_ohne_dt\n";
        double fe = 0.0, fc = 0.0, fp = 0.0, fi = 0.0;
        for (size_t k = 0; k < exact.size(); ++k)
        {
            fe += exact[k] * dt;
            fc += k < continuum.size() ? continuum[k] * dt : 0.0;
            fp += k < particles.size() ? particles[k] * dt : 0.0;
            fi += k < ideal.size() ? ideal[k] * dt : 0.0;
            file << (k + 1) * dt << ";" << exact[k] << ";" << (k < continuum.size() ? continuum[k] : 0.0) << ";"
                 << (k < particles.size() ? particles[k] : 0.0) << ";" << fe << ";" << fc << ";" << fp << ";" << fi << "\n";
        }
    }

    double Erlang(double t, int n, double theta)
    {
        if (t <= 0.0)
            return 0.0;
        const double x = t / theta;
        double term = 1.0;
        double sum = 1.0;
        for (int i = 1; i < n; ++i)
        {
            term *= x / i;
            sum += term;
        }
        return 1.0 - std::exp(-x) * sum;
    }

    // B2 (Martinetz et al. 2018): step response of first-order lags in series
    // with distinct time constants, F(t) = 1 - sum_i c_i exp(-t / tau_i) with
    // c_i = prod_{j != i} tau_i / (tau_i - tau_j); zero for t <= 0.
    double LagsStep(const std::vector<double>& taus, double t)
    {
        if (t <= 0.0)
            return 0.0;
        double f = 1.0;
        for (size_t i = 0; i < taus.size(); ++i)
        {
            double c = 1.0;
            for (size_t j = 0; j < taus.size(); ++j)
                if (j != i)
                    c *= taus[i] / (taus[i] - taus[j]);
            f -= c * std::exp(-t / taus[i]);
        }
        return f;
    }

    struct SLineCase
    {
        std::string file;
        std::string label;
        std::string csv;
        std::vector<double> lagsRC; // blender T1, T2, roller compactor T1, T2 [s]
        double deadRC = 0.0;        // blender Td + bucket conveyor + roller compactor Td [s]
        double pressLag = 0.0;      // tablet press T [s]
        double deadPress = 0.0;     // pneumatic conveyor + tablet press Td [s]
    };

    // API (Urea) mass fraction [%] at the first output of the named units in
    // every time step; one vector per name.
    bool SimulateFractions(const std::string& file, bool particles, size_t cards, const std::vector<std::string>& names,
        double& dt, std::vector<std::vector<double>>& fractions, std::string& error)
    {
        CFlowsheetGraph graph;
        const SFlowsheetReadResult read = LoadFlowsheetFile(std::string(STOSIS_VALIDATION_DIR) + "/" + file, graph);
        if (!read.ok || !read.warnings.empty())
        {
            error = read.ok ? "Hinweise beim Lesen: " + read.warnings.front() : read.error;
            return false;
        }
        if (particles)
        {
            graph.SetRepresentation(ERepresentation::Particles);
            SCardSettings settings = graph.GetCardSettings();
            settings.cardsPerSlice = cards;
            graph.SetCardSettings(settings);
            graph.SetRecordCardHistory(false);
        }
        graph.RunSimulations(1);
        dt = graph.GetGrid().GetDt();
        const std::vector<std::string> compounds = graph.GetActiveCompounds();
        size_t api = compounds.size();
        for (size_t c = 0; c < compounds.size(); ++c)
            if (compounds[c] == "Urea")
                api = c;
        if (api == compounds.size())
        {
            error = "Wirkstoff (Urea) fehlt";
            return false;
        }
        fractions.assign(names.size(), {});
        for (size_t i = 0; i < names.size(); ++i)
        {
            const CStreamRecord* record = nullptr;
            for (const SFlowsheetNode& node : graph.GetNodes())
                if (node.name == names[i])
                    record = graph.GetDisplayRecord(node.id, 0);
            if (!record)
            {
                error = "Unit \"" + names[i] + "\" fehlt";
                return false;
            }
            for (size_t k = 0; k < record->GetTotal().GetNumTimeSteps(); ++k)
            {
                const double total = record->GetTotal().IntegrateOverSize(k);
                fractions[i].push_back(total > 0.0 ? 100.0 * record->GetCompound(api).IntegrateOverSize(k) / total : 0.0);
            }
        }
        return true;
    }

    // Mass flow [kg/h] of the named compounds at the first output of a unit in
    // every time step; the loaded graph stays available to the caller.
    bool SimulateFlows(const std::string& file, bool particles, size_t cards, const std::string& unitName,
        const std::vector<std::string>& compounds, CFlowsheetGraph& graph, double& dt,
        std::vector<std::vector<double>>& flows, std::string& error)
    {
        const SFlowsheetReadResult read = LoadFlowsheetFile(std::string(STOSIS_VALIDATION_DIR) + "/" + file, graph);
        if (!read.ok || !read.warnings.empty())
        {
            error = read.ok ? "Hinweise beim Lesen: " + read.warnings.front() : read.error;
            return false;
        }
        if (particles)
        {
            graph.SetRepresentation(ERepresentation::Particles);
            SCardSettings settings = graph.GetCardSettings();
            settings.cardsPerSlice = cards;
            graph.SetCardSettings(settings);
            graph.SetRecordCardHistory(false);
        }
        graph.RunSimulations(1);
        dt = graph.GetGrid().GetDt();
        const CStreamRecord* record = nullptr;
        for (const SFlowsheetNode& node : graph.GetNodes())
            if (node.name == unitName)
                record = graph.GetDisplayRecord(node.id, 0);
        if (!record)
        {
            error = "Unit \"" + unitName + "\" fehlt";
            return false;
        }
        const std::vector<std::string> active = graph.GetActiveCompounds();
        flows.assign(compounds.size(), {});
        for (size_t i = 0; i < compounds.size(); ++i)
        {
            const size_t c = static_cast<size_t>(std::find(active.begin(), active.end(), compounds[i]) - active.begin());
            if (c == active.size())
            {
                error = "Stoff " + compounds[i] + " fehlt";
                return false;
            }
            for (size_t k = 0; k < record->GetTotal().GetNumTimeSteps(); ++k)
                flows[i].push_back(3600.0 * record->GetCompound(c).IntegrateOverSize(k));
        }
        return true;
    }

    // Standard deviation of v over the steps whose midpoints lie in [t0, t1].
    double StdDev(const std::vector<double>& v, double dt, double t0, double t1)
    {
        double n = 0.0, sum = 0.0, sq = 0.0;
        for (size_t k = 0; k < v.size(); ++k)
        {
            const double t = (static_cast<double>(k) + 0.5) * dt;
            if (t < t0 || t > t1)
                continue;
            n += 1.0;
            sum += v[k];
            sq += v[k] * v[k];
        }
        const double mean = sum / n;
        return std::sqrt(std::max(0.0, sq / n - mean * mean));
    }

    // B3 (Kruisz et al. 2017): a path from the API feeder to an outlet as
    // parallel branches (share, first-order lags in series) plus dead time.
    struct SPath
    {
        std::vector<std::pair<double, std::vector<double>>> branches;
        double dead = 0.0;
    };

    double PathStep(const SPath& path, double t)
    {
        double f = 0.0;
        for (const auto& branch : path.branches)
            f += branch.first * LagsStep(branch.second, t - path.dead);
        return f;
    }

    // Outlet flow of a path at the midpoints of the time steps for a feed that
    // starts in steady state at feed(0). The inlet holds the feed value of the
    // step start, so the input is a staircase; its jumps pass the path as step
    // responses (B3: feed relative to the nominal flow; B4: in kg/h).
    std::vector<double> FeedResponse(const std::function<double(double)>& feed, const SPath& path, size_t steps, double dt)
    {
        std::vector<std::pair<double, double>> jumps;
        const double start = feed(0.0);
        double previous = start;
        for (size_t j = 0; j < steps; ++j)
        {
            const double u = feed(static_cast<double>(j) * dt);
            if (u != previous)
                jumps.push_back({ static_cast<double>(j) * dt, u - previous });
            previous = u;
        }
        std::vector<double> y(steps, start);
        for (size_t k = 0; k < steps; ++k)
        {
            const double t = (static_cast<double>(k) + 0.5) * dt;
            for (const auto& jump : jumps)
                if (t > jump.first)
                    y[k] += jump.second * PathStep(path, t - jump.first);
        }
        return y;
    }

    struct SDisturbance
    {
        std::string file;
        std::string label;
        std::string csv;
        std::function<double(double)> feed; // API feed relative to 0.25 kg/h at time t [s]
        double paperBlender = -1.0;         // Tab. 6: diversion time after the blender [s], -1 = none
        double paperRC = -1.0;              // Tab. 6: diversion time after the roller compactor [s]
    };

    // Time [s] with the API fraction more than 5 % (relative) away from 5 %,
    // and the first such time after the disturbance at 100 s (-1 = never).
    double OutOfSpec(const std::vector<double>& c, double dt, double& first)
    {
        size_t n = 0;
        first = -1.0;
        for (size_t k = 0; k < c.size(); ++k)
            if (std::abs(c[k] / 5.0 - 1.0) > 0.05)
            {
                ++n;
                if (first < 0.0)
                    first = (static_cast<double>(k) + 0.5) * dt - 100.0;
            }
        return static_cast<double>(n) * dt;
    }

    struct SCase
    {
        std::string file;
        std::string outlet;
        std::string label;
        SExact exact;
        SExact levenspiel; // ohne Totzeit dt der Rueckfuehrung (nur A3)
        double toleranceContinuum = 1e-7; // groesste Abweichung relativ zum Maximum
        double toleranceMeanParticles = 0.01; // Mittel relativ
    };
}

int main(int argc, char* argv[])
{
    const std::string csvDir = argc > 1 ? argv[1] : "";
    const auto exponential = [](double weight, double tau)
    {
        return [weight, tau](double t) { return t <= 0.0 ? 0.0 : weight * (1.0 - std::exp(-t / tau)); };
    };

    std::vector<SCase> cases;
    {
        SCase c{ "Levenspiel_A1_Bypass_Totzone.json", "Ablauf", "A1 Bypass und Totzone" };
        c.exact.pulses = { { 0.0, 0.2 } };
        c.exact.Fc = exponential(0.8, 10.0);
        cases.push_back(c);
    }
    {
        SCase c{ "Levenspiel_A2_Parallele_Wege.json", "Ablauf", "A2 parallele Wege" };
        c.exact.pulses = { { 5.0, 0.4 } };
        c.exact.Fc = exponential(0.6, 12.0);
        cases.push_back(c);
    }
    {
        // Pulse bei k * 2 s; StoSiS: jeder weitere Durchgang dt laenger.
        SCase c{ "Levenspiel_A3_Rohr_mit_Rueckfuehrung.json", "Ablauf", "A3 Rohr mit Rueckfuehrung" };
        for (int k = 1; k <= 40; ++k)
        {
            const double w = 0.4 * std::pow(0.6, k - 1);
            c.exact.pulses.push_back({ 2.0 * k + 0.1 * (k - 1), w });
            c.levenspiel.pulses.push_back({ 2.0 * k, w });
        }
        cases.push_back(c);
    }
    {
        SCase c{ "Levenspiel_A4_Kessel_in_Reihe.json", "Ablauf Kaskade", "A4 Kaskade (ein Behaelter)" };
        c.exact.Fc = [](double t) { return Erlang(t, 3, 3.0); };
        cases.push_back(c);
        SCase s = c;
        s.outlet = "Ablauf Reihe";
        s.label = "A4 drei Kessel in Reihe";
        s.toleranceContinuum = 0.02; // Folgekessel sehen den Zulauf je Schritt als konstant
        cases.push_back(s);
    }
    {
        SCase c{ "Engisch2016_B1_Mischer.json", "Ablauf Grundfall", "B1 Mischer Grundfall (12 Kessel)" };
        c.exact.Fc = [](double t) { return Erlang(t, 12, 41.6 / 12.0); };
        cases.push_back(c);
        SCase f = c;
        f.outlet = "Ablauf Abb. 17";
        f.label = "B1 Mischer Abb. 17 (9 Kessel)";
        f.exact.Fc = [](double t) { return Erlang(t, 9, 68.8 / 9.0); };
        cases.push_back(f);
    }

    std::map<std::string, std::map<std::string, SRun>> continuumRuns;
    std::map<std::string, std::map<std::string, SRun>> particleRuns;
    std::map<std::string, double> dts;
    std::printf("%-34s %9s %9s %9s %9s %9s %9s %9s\n", "Fall", "Mittel", "Mittel", "Mittel", "Std.abw.", "Std.abw.",
        "Std.abw.", "Abw.K.");
    std::printf("%-34s %9s %9s %9s %9s %9s %9s %9s\n", "", "exakt", "Kontin.", "Partikel", "exakt", "Kontin.", "Partikel",
        "max/Gipfel");
    for (const SCase& c : cases)
    {
        double dt = 0.1;
        if (!continuumRuns.count(c.file))
        {
            continuumRuns[c.file] = Simulate(c.file, false, dt);
            particleRuns[c.file] = Simulate(c.file, true, dt);
            dts[c.file] = dt;
        }
        dt = dts[c.file];
        const SRun& cont = continuumRuns[c.file][c.outlet];
        const SRun& part = particleRuns[c.file][c.outlet];
        if (!cont.ok || !part.ok)
        {
            Check(false, c.label + ": Datei laden und rechnen - " + continuumRuns[c.file][""].error);
            continue;
        }
        const std::vector<double> ref = Reference(c.exact, cont.e.size(), dt);
        const SMoments me = Moments(ref, dt);
        const SMoments mc = Moments(cont.e, dt);
        const SMoments mp = Moments(part.e, dt);
        const double devC = MaxDeviation(cont.e, ref);
        const double devP = MaxDeviation(part.e, ref);
        std::printf("%-34s %9.4f %9.4f %9.4f %9.4f %9.4f %9.4f %9.2e  (Partikel %.3f)\n", c.label.c_str(), me.mean,
            mc.mean, mp.mean, me.sd, mc.sd, mp.sd, devC, devP, me.mass, mc.mass, mp.mass);
        Check(devC <= c.toleranceContinuum, c.label + ": Kontinuum folgt der exakten Loesung je Schritt");
        Check(std::abs(mc.mass - me.mass) < 1e-9 && std::abs(mp.mass - me.mass) < 2e-3,
            c.label + ": Masse des Pulses kommt an (Kontinuum exakt, Partikelmodus bis auf den Rest im Behaelter am Ende)");
        Check(std::abs(mp.mean - me.mean) <= c.toleranceMeanParticles * me.mean,
            c.label + ": Partikelmodus trifft die mittlere Verweilzeit auf 1 %");
        if (!c.levenspiel.pulses.empty())
        {
            const SMoments ml = Moments(Reference(c.levenspiel, cont.e.size(), dt), dt);
            std::printf("      ohne Totzeit der Rueckfuehrung (Levenspiel): Mittel %.4f s, Std.abw. %.4f s; StoSiS +%.4f s\n",
                ml.mean, ml.sd, mc.mean - ml.mean);
            Check(std::abs((mc.mean - ml.mean) - 1.5 * dt) < 1e-3, c.label + ": Totzeit der Rueckfuehrung = R * dt im Mittel");
        }
        if (!csvDir.empty())
        {
            std::string name = c.label;
            for (char& ch : name)
                if (ch == ' ' || ch == '(' || ch == ')' || ch == '.')
                    ch = '_';
            WriteCsv(csvDir, name, dt, ref, cont.e, part.e,
                c.levenspiel.pulses.empty() ? ref : Reference(c.levenspiel, cont.e.size(), dt));
        }
    }

    // Vergleich mit den im Paper angegebenen Kennzahlen des Mischers.
    {
        double dt = dts["Engisch2016_B1_Mischer.json"];
        const SMoments g = Moments(continuumRuns["Engisch2016_B1_Mischer.json"]["Ablauf Grundfall"].e, dt);
        const SMoments f = Moments(continuumRuns["Engisch2016_B1_Mischer.json"]["Ablauf Abb. 17"].e, dt);
        std::printf("      Engisch/Muzzio 2016: Grundfall 41,6 / 12,0 s, StoSiS %.2f / %.2f s; Abb. 17: 68,8 / 22,4 s, StoSiS %.2f / %.2f s\n",
            g.mean, g.sd, f.mean, f.sd);
        Check(std::abs(g.mean - 41.6) < 0.05 && std::abs(g.sd - 12.0) < 0.1, "B1 Grundfall: Mittel und Standardabweichung wie im Paper");
        Check(std::abs(f.mean - 68.8) < 0.05 && std::abs(f.sd - 22.4) < 0.6, "B1 Abb. 17: Mittel wie im Paper, Standardabweichung mit 9 statt 9,4 Kesseln");
    }

    // B2: dry granulation line after Martinetz et al. (2018), Tab. 2 there.
    // API mass fraction after a step 10 -> 20 % against the line model of the
    // paper (lags in series plus dead time). The step lies between the
    // samples 99.5 s and 100 s; time in the paper is counted from 99.75 s.
    {
        const double stepTime = 99.75;
        const std::vector<SLineCase> lines = {
            { "Martinetz2018_B2_Linie_08kgh.json", "8 kg/h", "B2_Martinetz_08kgh", { 24.41, 0.80, 0.34, 770.12 }, 6.96 + 45.0 + 190.79, 174.32, 7.5 + 110.23 },
            { "Martinetz2018_B2_Linie_10kgh.json", "10 kg/h", "B2_Martinetz_10kgh", { 21.50, 0.64, 0.29, 641.43 }, 6.13 + 30.0 + 181.83, 172.33, 7.5 + 101.83 },
            { "Martinetz2018_B2_Linie_12_5kgh.json", "12,5 kg/h", "B2_Martinetz_12_5kgh", { 18.27, 0.45, 0.24, 498.43 }, 5.20 + 30.0 + 171.86, 170.12, 7.5 + 88.63 },
            { "Martinetz2018_B2_Linie_15kgh.json", "15 kg/h", "B2_Martinetz_15kgh", { 14.71, 0.25, 0.19, 341.13 }, 4.19 + 22.5 + 160.90, 167.68, 7.5 + 76.12 },
        };
        // Particle mode: all lines with the cards of the file (2000) when writing
        // the CSV files for the figures; in the regular test run only 10 kg/h
        // with 250 cards (a line takes about 30 s with 500 cards).
        const size_t cards = csvDir.empty() ? 250 : 2000;
        for (const SLineCase& line : lines)
        {
            double dt = 0.5;
            const std::vector<std::string> names = { "Kompaktor Totzeit", "Tabletten" };
            std::vector<std::vector<double>> cont, part;
            std::string error;
            const auto t0 = std::chrono::steady_clock::now();
            const bool okC = SimulateFractions(line.file, false, 0, names, dt, cont, error);
            const auto t1 = std::chrono::steady_clock::now();
            const bool particles = !csvDir.empty() || line.label == "10 kg/h";
            const bool okP = !particles || (okC && SimulateFractions(line.file, true, cards, names, dt, part, error));
            if (!particles)
                part = cont;
            const auto t2 = std::chrono::steady_clock::now();
            if (!okC || !okP)
            {
                Check(false, "B2 " + line.label + ": Datei laden und rechnen - " + error);
                continue;
            }
            const std::vector<double>& rcC = cont[0];
            const std::vector<double>& pressC = cont[1];
            const std::vector<double>& rcP = part[0];
            const std::vector<double>& pressP = part[1];
            std::vector<double> lagsPress = line.lagsRC;
            lagsPress.push_back(line.pressLag);
            double before = 0.0, devRC = 0.0, devPress = 0.0, sqP = 0.0, meanP = 0.0;
            size_t nP = 0;
            std::vector<double> modelRC(rcC.size()), modelPress(rcC.size());
            for (size_t k = 0; k < rcC.size(); ++k)
            {
                const double t = static_cast<double>(k) * dt - stepTime;
                modelRC[k] = 10.0 + 10.0 * LagsStep(line.lagsRC, t - line.deadRC);
                modelPress[k] = 10.0 + 10.0 * LagsStep(lagsPress, t - line.deadRC - line.deadPress);
                if (t < 0.0)
                    before = std::max({ before, std::abs(rcC[k] - 10.0), std::abs(pressC[k] - 10.0) });
                else if (t <= 2000.0)
                {
                    devRC = std::max(devRC, std::abs(rcC[k] - modelRC[k]));
                    devPress = std::max(devPress, std::abs(pressC[k] - modelPress[k]));
                    sqP += (rcP[k] - modelRC[k]) * (rcP[k] - modelRC[k]) + (pressP[k] - modelPress[k]) * (pressP[k] - modelPress[k]);
                    meanP += (rcP[k] - modelRC[k]) + (pressP[k] - modelPress[k]);
                    nP += 2;
                }
            }
            const double rmsP = std::sqrt(sqP / static_cast<double>(nP));
            meanP /= static_cast<double>(nP);
            std::printf("      B2 %-9s Kontinuum gegen Linienmodell: Kompaktor %.2e, Presse %.2e %%-Punkte; vor dem Sprung %.1e; Rechenzeit %.1f s\n",
                line.label.c_str(), devRC, devPress, before, std::chrono::duration<double>(t1 - t0).count());
            if (particles)
                std::printf("         Partikelmodus (%zu Karten): RMS %.3f, Mittel %+.3f %%-Punkte gegen das Linienmodell; Rechenzeit %.1f s\n",
                    cards, rmsP, meanP, std::chrono::duration<double>(t2 - t1).count());
            Check(before < 1e-9, "B2 " + line.label + ": vor dem Sprung im Dauerbetrieb mit 10 % Wirkstoff");
            Check(devRC < 0.05 && devPress < 0.05, "B2 " + line.label + ": Kontinuum folgt dem Linienmodell des Papers (0,05 %-Punkte)");
            if (particles)
                Check(rmsP < 2.0 && std::abs(meanP) < 0.5, "B2 " + line.label + ": Partikelmodus streut um das Linienmodell ohne Versatz");
            if (!csvDir.empty())
            {
                std::ofstream file(csvDir + "/" + line.csv + ".csv");
                file << "t_s;Modell_Kompaktor;Modell_Presse;Kontinuum_Kompaktor;Kontinuum_Presse;Partikel_Kompaktor;Partikel_Presse\n";
                for (size_t k = 0; k < rcC.size(); ++k)
                {
                    const double t = static_cast<double>(k) * dt - stepTime;
                    if (t < -50.0 || t > 2000.0)
                        continue;
                    file << t << ";" << modelRC[k] << ";" << modelPress[k] << ";" << rcC[k] << ";" << pressC[k] << ";"
                         << rcP[k] << ";" << pressP[k] << "\n";
                }
            }
        }
    }

    // B3: disturbances of the API feeder after Kruisz et al. (2017), 5 kg/h
    // with 5 % API, disturbance from t = 100 s. Two lines per file: blender
    // after Eq. 6 with Tab. 4 (1000 rpm, run 1) as two parallel lags, and the
    // blender as one lag of 90 s (fitted to Tab. 6); roller compactor after
    // Eq. 7 with Tab. 5 (run 1). API fraction after the blender and after the
    // roller compactor against the closed form, diversion times (more than
    // 5 % relative away from 5 %) against Tab. 6 of the paper.
    {
        const double share329 = (329.38 - 155.27) / (329.38 - 81.62);
        const SPath blenderTab4 = { { { share329, { 329.38 } }, { 1.0 - share329, { 81.62 } } }, 6.54 };
        const SPath compactorTab4 = { { { share329, { 329.38, 1.85, 93.21 } }, { 1.0 - share329, { 81.62, 1.85, 93.21 } } }, 6.54 + 18.15 };
        const SPath blenderFit = { { { 1.0, { 90.0 } } }, 6.54 };
        const SPath compactorFit = { { { 1.0, { 90.0, 1.85, 93.21 } } }, 6.54 + 18.15 };
        const std::vector<SPath> paths = { blenderTab4, compactorTab4, blenderFit, compactorFit };
        const std::vector<std::string> names = { "Mischer Totzeit (Tab. 4)", "Kompaktor Totzeit (Tab. 4)",
            "Mischer Totzeit (angepasst)", "Kompaktor Totzeit (angepasst)" };
        const auto stop = [](double length) { return [length](double t) { return t >= 100.0 && t < 100.0 + length ? 0.0 : 1.0; }; };
        const std::vector<SDisturbance> cases = {
            { "Kruisz2017_B3_Impuls.json", "Impuls", "B3_Kruisz_Impuls", [](double t) { return t >= 100.0 && t < 101.0 ? 2.0 : 1.0; } },
            { "Kruisz2017_B3_Rampe.json", "Rampe", "B3_Kruisz_Rampe", [](double t) { return t < 100.0 ? 1.0 : t < 600.0 ? 1.0 - (t - 100.0) / 500.0 : 0.0; } },
            { "Kruisz2017_B3_Dauerstopp.json", "Dauerstopp", "B3_Kruisz_Dauerstopp", [](double t) { return t < 100.0 ? 1.0 : 0.0; } },
            { "Kruisz2017_B3_Stopp_120s.json", "Stopp 120 s", "B3_Kruisz_Stopp_120s", stop(120.0), 370.0, 460.0 },
            { "Kruisz2017_B3_Stopp_60s.json", "Stopp 60 s", "B3_Kruisz_Stopp_60s", stop(60.0), 270.0, 350.0 },
            { "Kruisz2017_B3_Stopp_10s.json", "Stopp 10 s", "B3_Kruisz_Stopp_10s", stop(10.0), 75.0, 0.0 },
        };
        // Particle mode: every case with the cards of the file (2000) when
        // writing the CSV files; in the regular test run only the 120-s stop
        // with 250 cards.
        const size_t cards = csvDir.empty() ? 250 : 2000;
        std::printf("      B3 Ausschleuszeit [s] Mischer / Kompaktor: Paper Tab. 6 | nach Tab. 4: wie im Paper, exakt, Kontinuum"
                    " | angepasst: wie im Paper, exakt, Kontinuum\n");
        for (const SDisturbance& c : cases)
        {
            double dt = 0.5;
            std::vector<std::vector<double>> cont, part;
            std::string error;
            const auto t0 = std::chrono::steady_clock::now();
            const bool okC = SimulateFractions(c.file, false, 0, names, dt, cont, error);
            const auto t1 = std::chrono::steady_clock::now();
            const bool particles = !csvDir.empty() || c.label == "Stopp 120 s";
            const bool okP = !particles || (okC && SimulateFractions(c.file, true, cards, names, dt, part, error));
            if (!particles)
                part = cont;
            const auto t2 = std::chrono::steady_clock::now();
            if (!okC || !okP)
            {
                Check(false, "B3 " + c.label + ": Datei laden und rechnen - " + error);
                continue;
            }
            // exact: mass fraction as in StoSiS (the excipient flow stays at
            // 4.75 kg/h); linear: the paper convolves the concentration, i.e.
            // 5 % times the relative API flow (total flow taken as constant).
            const size_t steps = cont[0].size();
            std::vector<std::vector<double>> exact, linear;
            for (const SPath& path : paths)
            {
                std::vector<double> y = FeedResponse(c.feed, path, steps, dt);
                std::vector<double> lin = y;
                for (double& v : lin)
                    v *= 5.0;
                for (double& v : y)
                    v = 100.0 * 0.25 * v / (0.25 * v + 4.75);
                exact.push_back(y);
                linear.push_back(lin);
            }
            double dev = 0.0, sqP = 0.0, meanP = 0.0;
            for (size_t i = 0; i < 4; ++i)
                for (size_t k = 0; k < steps; ++k)
                {
                    dev = std::max(dev, std::abs(cont[i][k] - exact[i][k]));
                    sqP += (part[i][k] - exact[i][k]) * (part[i][k] - exact[i][k]);
                    meanP += part[i][k] - exact[i][k];
                }
            const double rmsP = std::sqrt(sqP / static_cast<double>(4 * steps));
            meanP /= static_cast<double>(4 * steps);
            double oosExact[4], oosCont[4], oosLinear[4], firstExact[4], firstCont[4], firstLinear[4];
            for (size_t i = 0; i < 4; ++i)
            {
                oosExact[i] = OutOfSpec(exact[i], dt, firstExact[i]);
                oosCont[i] = OutOfSpec(cont[i], dt, firstCont[i]);
                oosLinear[i] = OutOfSpec(linear[i], dt, firstLinear[i]);
            }
            const auto paper = [](double v) { return v < 0.0 ? std::string("-") : std::to_string(static_cast<int>(v)); };
            std::printf("      B3 %-11s %4s / %4s | %5.1f %5.1f %5.1f / %5.1f %5.1f %5.1f | %5.1f %5.1f %5.1f / %5.1f %5.1f %5.1f; Abweichung Kontinuum %.1e %%-Punkte; %.1f s\n",
                c.label.c_str(), paper(c.paperBlender).c_str(), paper(c.paperRC).c_str(),
                oosLinear[0], oosExact[0], oosCont[0], oosLinear[1], oosExact[1], oosCont[1],
                oosLinear[2], oosExact[2], oosCont[2], oosLinear[3], oosExact[3], oosCont[3], dev, std::chrono::duration<double>(t1 - t0).count());
            if (c.label == "Dauerstopp" || c.label == "Rampe")
                std::printf("         ausserhalb ab [s] wie im Paper gerechnet: nach Tab. 4 %.1f / %.1f, angepasst %.1f / %.1f (Paper: %s)\n",
                    firstLinear[0], firstLinear[1], firstLinear[2], firstLinear[3], c.label == "Rampe" ? "95 / 160" : "10 / 50");
            if (c.label == "Impuls")
                std::printf("         hoechster Anteil nach dem Mischer [%%] wie im Paper gerechnet: nach Tab. 4 %.3f, angepasst %.3f (Paper Abb. 9: etwa 5,06)\n",
                    *std::max_element(linear[0].begin(), linear[0].end()), *std::max_element(linear[2].begin(), linear[2].end()));
            if (particles)
                std::printf("         Partikelmodus (%zu Karten): RMS %.3f, Mittel %+.4f %%-Punkte gegen die exakte Loesung; %.1f s\n",
                    cards, rmsP, meanP, std::chrono::duration<double>(t2 - t1).count());
            Check(dev < 0.005, "B3 " + c.label + ": Kontinuum folgt der exakten Loesung (0,005 %-Punkte)");
            bool sameOos = true;
            for (size_t i = 0; i < 4; ++i)
                sameOos = sameOos && std::abs(oosCont[i] - oosExact[i]) <= dt + 1e-9;
            Check(sameOos, "B3 " + c.label + ": Ausschleuszeiten im Kontinuum wie exakt (ein Zeitschritt)");
            if (c.paperBlender >= 0.0)
            {
                const auto matches = [](double v, double ref) { return ref > 0.0 ? std::abs(v / ref - 1.0) < 0.05 : v == 0.0; };
                Check(matches(oosLinear[2], c.paperBlender) && matches(oosLinear[3], c.paperRC),
                    "B3 " + c.label + ": Mischer mit 90 s trifft Tab. 6 des Papers, gerechnet wie dort (5 %)");
            }
            if (particles)
                Check(rmsP < 0.5 && std::abs(meanP) < 0.05, "B3 " + c.label + ": Partikelmodus streut um die exakte Loesung ohne Versatz");
            if (!csvDir.empty())
            {
                std::ofstream file(csvDir + "/" + c.csv + ".csv");
                file << "t_s;exakt_Tab4_Mischer;exakt_Tab4_Kompaktor;exakt_90s_Mischer;exakt_90s_Kompaktor;"
                        "Kontinuum_Tab4_Mischer;Kontinuum_Tab4_Kompaktor;Kontinuum_90s_Mischer;Kontinuum_90s_Kompaktor;"
                        "Partikel_Tab4_Mischer;Partikel_Tab4_Kompaktor;Partikel_90s_Mischer;Partikel_90s_Kompaktor\n";
                for (size_t k = 0; k < steps; ++k)
                {
                    file << (static_cast<double>(k) + 0.5) * dt - 100.0;
                    for (const auto* set : { &exact, &cont, &part, &linear })
                        for (size_t i = 0; i < 4; ++i)
                            file << ";" << (*set)[i][k];
                    file << "\n";
                }
            }
        }
    }

    // B4: blender after Gao, Muzzio & Ierapetritou (2011), Fig. 4: the feed
    // rates read from Fig. 4b/c (150-300 s) pass the blender. The paper
    // describes the blender with the dispersion model (Pe = 1.76, tau = 3.66 s);
    // StoSiS has no such unit and uses plug flow 0.76 s plus a stirred tank
    // 7.06 s with the same mean (7.82 s) and standard deviation (7.06 s).
    // Continuum against the closed form of this substitute, the substitute
    // against the dispersion model, standard deviations against Tab. 1 there.
    {
        const std::string file = "Gao2011_B4_Mischer.json";
        const double pe = 1.76, tau = 3.66;
        const SPath substitute = { { { 1.0, { 7.06 } } }, 0.76 };
        const std::vector<std::string> compounds = { "Cellets", "Urea" };
        const std::vector<std::string> labels = { "Hilfsstoff", "Wirkstoff" };
        const double paperIn[2] = { 0.164, 0.051 }, paperOut[2] = { 0.067, 0.023 };
        // Particle mode with the cards of the file (2000) when writing the CSV
        // files, else 250 cards.
        const size_t cards = csvDir.empty() ? 250 : 2000;
        CFlowsheetGraph graphC, graphP;
        double dt = 0.1;
        std::vector<std::vector<double>> cont, part;
        std::string error;
        const auto t0 = std::chrono::steady_clock::now();
        const bool okC = SimulateFlows(file, false, 0, "Ausgang", compounds, graphC, dt, cont, error);
        const auto t1 = std::chrono::steady_clock::now();
        const bool okP = okC && SimulateFlows(file, true, cards, "Ausgang", compounds, graphP, dt, part, error);
        const auto t2 = std::chrono::steady_clock::now();
        if (!okC || !okP)
            Check(false, "B4 Gao: Datei laden und rechnen - " + error);
        else
        {
            const size_t steps = cont[0].size();
            // dispersion model (open at both ends, Eq. 27 there) as E(t) [1/s]
            const auto dispersion = [pe, tau](double t)
            {
                if (t <= 0.0)
                    return 0.0;
                const double theta = t / tau;
                return std::sqrt(pe / (4.0 * 3.14159265358979 * theta)) * std::exp(-pe * (1.0 - theta) * (1.0 - theta) / (4.0 * theta)) / tau;
            };
            std::vector<std::vector<double>> input(2), exact(2), disp(2);
            for (size_t i = 0; i < 2; ++i)
            {
                const CInletUnit* inlet = nullptr;
                for (const SFlowsheetNode& node : graphC.GetNodes())
                    if (node.name == labels[i])
                        inlet = graphC.GetInlet(node.id);
                if (!inlet)
                {
                    Check(false, "B4 Gao: Inlet " + labels[i] + " fehlt");
                    continue;
                }
                // the staircase the inlet delivers: value at the step start, in kg/h
                const auto feed = [inlet, dt](double t)
                {
                    const double start = std::max(0.0, std::floor(t / dt + 1e-9) * dt);
                    return 3600.0 * inlet->GetMassFlowAtTime(start);
                };
                exact[i] = FeedResponse(feed, substitute, steps, dt);
                disp[i].assign(steps, 0.0);
                input[i].assign(steps, 0.0);
                const double h = 0.01;
                for (size_t k = 0; k < steps; ++k)
                {
                    const double t = (static_cast<double>(k) + 0.5) * dt;
                    input[i][k] = feed(t);
                    double sum = 0.0, weight = 0.0;
                    for (double u = 0.5 * h; u < 120.0; u += h)
                    {
                        const double e = dispersion(u);
                        sum += e * feed(t - u);
                        weight += e;
                    }
                    disp[i][k] = sum / weight;
                }
            }
            if (exact[0].empty() || exact[1].empty())
                Check(false, "B4 Gao: Zulauf nicht lesbar");
            else
            {
                const double w0 = 190.0, w1 = 300.0; // output window: 40 s after the read input starts
                std::printf("      B4 Gao 2011, Std.abw. [kg/h] %.0f-%.0f s: Zulauf | Ausgang Dispersionsmodell, Ersatz exakt, Kontinuum, Partikel | VRR; Paper Tab. 1\n", w0, w1);
                for (size_t i = 0; i < 2; ++i)
                {
                    double dev = 0.0, sqD = 0.0, sqP = 0.0, meanP = 0.0, n = 0.0;
                    for (size_t k = 0; k < steps; ++k)
                    {
                        dev = std::max(dev, std::abs(cont[i][k] - exact[i][k]));
                        const double t = (static_cast<double>(k) + 0.5) * dt;
                        if (t < w0 || t > w1)
                            continue;
                        sqD += (exact[i][k] - disp[i][k]) * (exact[i][k] - disp[i][k]);
                        sqP += (part[i][k] - exact[i][k]) * (part[i][k] - exact[i][k]);
                        meanP += part[i][k] - exact[i][k];
                        n += 1.0;
                    }
                    const double sIn = StdDev(input[i], dt, w0, w1), sDisp = StdDev(disp[i], dt, w0, w1);
                    const double sExact = StdDev(exact[i], dt, w0, w1), sCont = StdDev(cont[i], dt, w0, w1), sPart = StdDev(part[i], dt, w0, w1);
                    std::printf("      B4 %-10s %.4f | %.4f %.4f %.4f %.4f | %.2f (Dispersion %.2f); Tab. 1: %.3f | %.3f, VRR %.2f\n",
                        labels[i].c_str(), sIn, sDisp, sExact, sCont, sPart, sIn * sIn / (sCont * sCont), sIn * sIn / (sDisp * sDisp),
                        paperIn[i], paperOut[i], paperIn[i] * paperIn[i] / (paperOut[i] * paperOut[i]));
                    std::printf("         Kontinuum gegen Ersatz exakt hoechstens %.1e kg/h; Ersatz gegen Dispersionsmodell RMS %.4f kg/h;"
                                " Partikelmodus (%zu Karten) RMS %.4f, Mittel %+.5f kg/h\n",
                        dev, std::sqrt(sqD / n), cards, std::sqrt(sqP / n), meanP / n);
                    Check(dev < 0.01 * sExact, "B4 " + labels[i] + ": Kontinuum folgt dem Ersatz (Pfropfen + Kessel), hoechstens 1 % der Schwankung daneben");
                    Check(std::abs(sExact / sDisp - 1.0) < 0.05,
                        "B4 " + labels[i] + ": Ersatz glaettet wie das Dispersionsmodell des Papers (Standardabweichung auf 5 %)");
                    Check(std::abs(meanP / n) < 0.1 * sExact, "B4 " + labels[i] + ": Partikelmodus ohne Versatz");
                }
                std::printf("         Rechenzeit Kontinuum %.1f s, Partikelmodus %.1f s\n",
                    std::chrono::duration<double>(t1 - t0).count(), std::chrono::duration<double>(t2 - t1).count());
                if (!csvDir.empty())
                {
                    std::ofstream out(csvDir + "/B4_Gao_Mischer.csv");
                    out << "t_s;Zulauf_Hilfsstoff;Zulauf_Wirkstoff;Ersatz_Hilfsstoff;Ersatz_Wirkstoff;Dispersion_Hilfsstoff;Dispersion_Wirkstoff;"
                           "Kontinuum_Hilfsstoff;Kontinuum_Wirkstoff;Partikel_Hilfsstoff;Partikel_Wirkstoff\n";
                    for (size_t k = 0; k < steps; ++k)
                    {
                        out << (static_cast<double>(k) + 0.5) * dt;
                        for (const auto* set : { &input, &exact, &disp, &cont, &part })
                            for (size_t i = 0; i < 2; ++i)
                                out << ";" << (*set)[i][k];
                        out << "\n";
                    }
                }
            }
        }
    }
    return Summary();
}
