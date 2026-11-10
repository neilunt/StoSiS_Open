// Kompartiment-Modelle nach Levenspiel (1999) und Mischer nach Engisch und
// Muzzio (2016) aus den Flowsheet-Dateien in validierung/flowsheets
// (Methodenhandbuch 11.3, 11.4): Tracerpuls von 1 kg im ersten Zeitschritt,
// Ablauf gegen die exakte Verweilzeitverteilung E(t), im Kontinuum und im
// Partikelmodus. Mit einem Ordner als Argument schreibt das Programm je
// Ablauf eine CSV-Datei (t; exakt; Kontinuum; Partikel; jeweils E und F).
//
// Bezug: Der Puls fuellt das Intervall [0, dt], Ausgang k ist die Masse,
// die in [k dt, (k + 1) dt] austritt, geteilt durch dt. Exakt ist damit
//   r_k = 1/dt^2 * Integral_0^dt (F((k+1) dt - u) - F(k dt - u)) du
// fuer den stetigen Teil und w/dt im Schritt T/dt fuer einen Puls w bei T.
#include "FlowsheetFile.h"
#include "TestSupport.h"
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
    return Summary();
}
