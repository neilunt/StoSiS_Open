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

    // API mass fraction [%] after the roller compactor ("Kompaktor Totzeit")
    // and at the tablet press ("Tabletten") in every time step.
    bool SimulateLine(const std::string& file, bool particles, size_t cards, double& dt,
        std::vector<double>& rc, std::vector<double>& press, std::string& error)
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
        std::string idRC, idPress;
        for (const SFlowsheetNode& node : graph.GetNodes())
        {
            if (node.name == "Kompaktor Totzeit")
                idRC = node.id;
            if (node.name == "Tabletten")
                idPress = node.id;
        }
        const CStreamRecord* recordRC = idRC.empty() ? nullptr : graph.GetDisplayRecord(idRC, 0);
        const CStreamRecord* recordPress = idPress.empty() ? nullptr : graph.GetDisplayRecord(idPress, 0);
        if (api == compounds.size() || !recordRC || !recordPress)
        {
            error = "Wirkstoff (Urea), Kompaktor Totzeit oder Tabletten fehlen";
            return false;
        }
        const auto fraction = [api](const CStreamRecord& record, size_t k)
        {
            const double total = record.GetTotal().IntegrateOverSize(k);
            return total > 0.0 ? 100.0 * record.GetCompound(api).IntegrateOverSize(k) / total : 0.0;
        };
        rc.clear();
        press.clear();
        for (size_t k = 0; k < recordRC->GetTotal().GetNumTimeSteps(); ++k)
        {
            rc.push_back(fraction(*recordRC, k));
            press.push_back(fraction(*recordPress, k));
        }
        return true;
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
            std::vector<double> rcC, pressC, rcP, pressP;
            std::string error;
            const auto t0 = std::chrono::steady_clock::now();
            const bool okC = SimulateLine(line.file, false, 0, dt, rcC, pressC, error);
            const auto t1 = std::chrono::steady_clock::now();
            const bool particles = !csvDir.empty() || line.label == "10 kg/h";
            const bool okP = !particles || (okC && SimulateLine(line.file, true, cards, dt, rcP, pressP, error));
            if (!particles)
            {
                rcP = rcC;
                pressP = pressC;
            }
            const auto t2 = std::chrono::steady_clock::now();
            if (!okC || !okP)
            {
                Check(false, "B2 " + line.label + ": Datei laden und rechnen - " + error);
                continue;
            }
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
    return Summary();
}
