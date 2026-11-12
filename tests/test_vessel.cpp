// Behaelter (Methodenhandbuch 7.4): vier Mischungen gegen ihre geschlossene
// Verweilzeitverteilung, Kontinuum und Partikelmodus, Massenbilanz,
// Anfangsfuellung, Vorrat, Temperatur, die Daempfung einer OU-Schwankung
// und die Losnummer (gleich grosse Karten je Schritt neu ausgelost).
//
// Schrittformel: Jeder Schritt bringt den Zulauf mal dt, der Ausgang zeigt,
// was im Schritt austritt, geteilt durch dt. Fuer konstanten Zulauf in einen
// leeren Behaelter ist der Ablauf im Schritt k dann genau der Mittelwert der
// Summenkurve F(t) ueber [t_k, t_k + dt] - fuer alle vier Mischungen.
#include "FlowsheetGraph.h"
#include "VesselUnit.h"
#include "TestSupport.h"

#include <algorithm>
#include <functional>

using namespace stosis;
using namespace stosis_test;

namespace
{
    SSimulationGrid MakeGrid(ERepresentation representation, size_t cards, double totalTime, double dt)
    {
        SSimulationGrid grid;
        grid.sizeGridType = ESizeGridType::Logarithmic;
        grid.numSizeClasses = 30;
        grid.sizeMin = 1e-6;
        grid.sizeMax = 1e-3;
        grid.totalTime = totalTime;
        grid.numTimeSteps = SSimulationGrid::TimePointsFor(totalTime, dt);
        grid.representation = representation;
        grid.cards.cardsPerSlice = cards;
        grid.compounds = { "A", "B" };
        return grid;
    }

    // Zulauf mit 1 kg/s aus Stoff compound: Log-Normal d50 = 100 um, sigma_ln = 0,5.
    CStreamSlice MakeFeed(const SSimulationGrid& grid, size_t compound)
    {
        SSizeDistribution distribution;
        distribution.type = EDistributionType::LogNormal;
        distribution.position = 100e-6;
        distribution.width = 0.5;
        CStreamSlice slice;
        slice.Resize(grid.numSizeClasses, grid.GetNumCompounds(), grid.representation);
        if (slice.HasCards())
        {
            const SCardPlacement placement = PlaceCardsAtQuantiles(distribution, grid.cards, grid.GetCardCountMin());
            for (size_t k = 0; k < placement.sizes.size(); ++k)
                slice.AddCard(placement.sizes[k], compound, placement.massFractions[k]);
        }
        else
        {
            const SSizeGrid sizes = grid.GetSizeGrid();
            const std::vector<double> masses = distribution.MassesAtNodes(sizes);
            for (size_t s = 0; s < grid.numSizeClasses; ++s)
                slice.SetValue(compound, s, masses[s] / sizes.GetWeight(s));
        }
        return slice;
    }

    CStreamSlice ScaledFeed(const CStreamSlice& feed, double factor, double temperature)
    {
        CStreamSlice slice;
        slice.Resize(feed.GetNumSizeClasses(), feed.GetNumCompounds(), feed.GetRepresentation());
        if (feed.HasCards())
        {
            if (factor != 0.0)
                for (size_t k = 0; k < feed.GetNumCards(); ++k)
                {
                    std::vector<double> flows(feed.GetNumCompounds());
                    for (size_t c = 0; c < flows.size(); ++c)
                        flows[c] = factor * feed.GetCardMassFlow(k, c);
                    slice.AddCard(feed.GetCardSize(k), flows);
                }
        }
        else
            slice.AssignScaled(feed, factor);
        slice.SetTemperature(temperature);
        return slice;
    }

    double MassFlow(const CStreamSlice& slice, const std::vector<double>& weights)
    {
        return slice.HasCards() ? slice.GetCardsMassFlow() : slice.GetTotalWeight(weights);
    }

    double MassFlow(const CStreamSlice& slice, size_t compound, const std::vector<double>& weights)
    {
        return slice.HasCards() ? slice.GetCardsMassFlow(compound) : slice.GetCompoundWeight(compound, weights);
    }

    // Massengewichtete mittlere Groesse der Karten einer Scheibe.
    double MeanCardSize(const CStreamSlice& slice)
    {
        double mass = 0.0, moment = 0.0;
        for (size_t k = 0; k < slice.GetNumCards(); ++k)
        {
            mass += slice.GetCardMassFlow(k);
            moment += slice.GetCardMassFlow(k) * slice.GetCardSize(k);
        }
        return mass > 0.0 ? moment / mass : 0.0;
    }

    struct SRun
    {
        std::vector<double> in, out, outB, outT, outSize;
        double initial = 0.0, final = 0.0, finalB = 0.0, feed = 0.0, feedSize = 0.0;
        size_t maxCards = 0, finalCards = 0, cap = 0, merged = 0;
        std::vector<std::string> warnings;
        std::string log;
    };

    // Behaelter allein, Zulauf inflow(k) [kg/s] aus Stoff compound.
    SRun Run(const SVesselSettings& settings, const SSimulationGrid& grid, const std::function<double(size_t)>& inflow,
        size_t compound = 0, double temperature = kRoomTemperature, size_t stockLimit = 0)
    {
        SRun run;
        CVesselUnit vessel;
        if (!vessel.SetSettings(settings))
        {
            Check(false, "Einstellungen gueltig: " + settings.Check());
            return run;
        }
        if (stockLimit > 0)
            vessel.SetStockLimit(stockLimit);
        vessel.Initialize(grid, 4711);
        run.log = vessel.DescribeForLog();
        const std::vector<double> weights = grid.GetSizeGrid().GetWeights();
        const CStreamSlice feed = MakeFeed(grid, compound);
        run.feed = MassFlow(feed, weights);
        run.feedSize = feed.HasCards() ? MeanCardSize(feed) : 0.0;
        const double dt = grid.GetDt();
        std::vector<CStreamSlice> outputs(1);
        for (size_t k = 0; k < grid.numTimeSteps; ++k)
        {
            const CStreamSlice input = ScaledFeed(feed, inflow(k), temperature);
            outputs[0].Resize(grid.numSizeClasses, grid.GetNumCompounds(), grid.representation);
            SStepContext context;
            context.stepIndex = k;
            context.dt = dt;
            context.time = static_cast<double>(k) * dt;
            vessel.Step(context, { &input }, outputs);
            run.in.push_back(MassFlow(input, weights));
            run.out.push_back(MassFlow(outputs[0], weights));
            run.outB.push_back(MassFlow(outputs[0], 1, weights));
            run.outT.push_back(outputs[0].GetTemperature());
            run.outSize.push_back(outputs[0].HasCards() ? MeanCardSize(outputs[0]) : 0.0);
            run.maxCards = std::max(run.maxCards, vessel.GetContentCards());
        }
        run.initial = vessel.GetInitialContentMass();
        run.final = vessel.GetContentMass();
        run.finalB = vessel.GetContentMass(1);
        run.finalCards = vessel.GetContentCards();
        run.cap = vessel.GetCardCap();
        run.merged = vessel.GetMergedCards();
        run.warnings = vessel.GetWarnings();
        return run;
    }

    // Relativer Fehler der Bilanz Zulauf + Anfang = Ablauf + Ende.
    double BalanceError(const SRun& run, double dt)
    {
        double in = run.initial, out = run.final;
        for (double v : run.in)
            in += v * dt;
        for (double v : run.out)
            out += v * dt;
        return std::abs(in - out) / std::max(in, 1e-300);
    }

    // Erlang-Summenkurve mit n Stufen und Rate rate.
    double ErlangCdf(size_t n, double rate, double t)
    {
        const double x = rate * t;
        double term = 1.0, sum = 1.0;
        for (size_t i = 1; i < n; ++i)
        {
            term *= x / static_cast<double>(i);
            sum += term;
        }
        return 1.0 - std::exp(-x) * sum;
    }

    // Integral der Summenkurve F von 0 bis t (geschlossen).
    double CdfIntegral(const SVesselSettings& s, double t)
    {
        const double tau = s.residenceTime;
        switch (s.mixing)
        {
        case EVesselMixing::Plug:
            return std::max(0.0, t - tau);
        case EVesselMixing::Cascade:
        {
            const double rate = static_cast<double>(s.cells) / tau;
            // int_0^t F_n = t F_n(t) - (n / rate) F_(n+1)(t)
            return t * ErlangCdf(s.cells, rate, t) - static_cast<double>(s.cells) / rate * ErlangCdf(s.cells + 1, rate, t);
        }
        case EVesselMixing::CoreFlow:
        {
            const double tk = s.coreTime * tau, tr = s.GetWallTime();
            return s.coreShare * std::max(0.0, t - tk) + (1.0 - s.coreShare) * (t + tr * std::expm1(-t / tr));
        }
        case EVesselMixing::Mixed:
        default:
            return t + tau * std::expm1(-t / tau);
        }
    }

    double StepMean(const SVesselSettings& s, double t0, double t1)
    {
        return (CdfIntegral(s, t1) - CdfIntegral(s, t0)) / (t1 - t0);
    }

    SVesselSettings Settings(EVesselMixing mixing, double tau, EVesselFill fill)
    {
        SVesselSettings s;
        s.mixing = mixing;
        s.residenceTime = tau;
        s.fill = fill;
        s.cells = 3;
        s.coreShare = 0.6;
        s.coreTime = 0.3;
        return s;
    }

    const char* Name(const SVesselSettings& s) { return GetVesselMixingName(s.mixing); }
}

int main()
{
    const double dt = 0.2;
    const size_t N = 400;

    // Faelle: Pfropfen und Kernfluss mit nicht ganzzahligem tau/dt, dazu
    // Pfropfen mit tau < dt.
    std::vector<SVesselSettings> cases = {
        Settings(EVesselMixing::Mixed, 10.0, EVesselFill::Empty),
        Settings(EVesselMixing::Plug, 10.1, EVesselFill::Empty),
        Settings(EVesselMixing::Cascade, 10.0, EVesselFill::Empty),
        Settings(EVesselMixing::CoreFlow, 10.1, EVesselFill::Empty),
        Settings(EVesselMixing::Plug, 0.1, EVesselFill::Empty),
    };

    // 1) Konstanter Zulauf in den leeren Behaelter: Ablauf je Schritt gegen
    //    den Mittelwert der geschlossenen Summenkurve ueber den Schritt.
    for (const SVesselSettings& s : cases)
    {
        const SSimulationGrid grid = MakeGrid(ERepresentation::Continuum, N, 60.0, dt);
        const SRun run = Run(s, grid, [](size_t) { return 10.0; });
        double worst = 0.0;
        for (size_t k = 0; k < run.out.size(); ++k)
        {
            const double expected = StepMean(s, k * dt, (k + 1) * dt);
            worst = std::max(worst, std::abs(run.out[k] / (10.0 * run.feed) - expected));
        }
        std::printf("      %s, tau = %g s: groesste Abweichung %.2e\n", Name(s), s.residenceTime, worst);
        Check(worst < 1e-9, std::string("Kontinuum, ") + Name(s) + ": Ablauf = Mittel der Summenkurve je Schritt (1e-9)");
        Check(BalanceError(run, dt) < 1e-12, std::string("Kontinuum, ") + Name(s) + ": Massenbilanz exakt");

        const SSimulationGrid cards = MakeGrid(ERepresentation::Particles, N, 60.0, dt);
        const SRun cardRun = Run(s, cards, [](size_t) { return 10.0; });
        const double card = 10.0 * cardRun.feed / static_cast<double>(N); // Massenstrom einer Zulaufkarte
        double worstStep = 0.0, worstSum = 0.0, sum = 0.0;
        for (size_t k = 0; k < cardRun.out.size(); ++k)
        {
            const double expected = 10.0 * cardRun.feed * StepMean(s, k * dt, (k + 1) * dt);
            worstStep = std::max(worstStep, std::abs(cardRun.out[k] - expected));
            sum += cardRun.out[k] - expected;
            worstSum = std::max(worstSum, std::abs(sum));
        }
        std::printf("      Karten: je Schritt hoechstens %.2f Karten daneben, aufsummiert %.2f\n", worstStep / card, worstSum / card);
        Check(worstStep < 6.0 * card && worstSum < 12.0 * card,
            std::string("Partikelmodus, ") + Name(s) + ": je Schritt hoechstens 6 Karten, aufsummiert 12 Karten neben der Summenkurve");
        Check(BalanceError(cardRun, dt) < 1e-12, std::string("Partikelmodus, ") + Name(s) + ": Massenbilanz exakt");
    }

    // Kaskade mit einem Kessel = ideal durchmischt.
    {
        const SSimulationGrid grid = MakeGrid(ERepresentation::Continuum, N, 30.0, dt);
        SVesselSettings one = Settings(EVesselMixing::Cascade, 7.0, EVesselFill::Empty);
        one.cells = 1;
        const SRun a = Run(one, grid, [](size_t k) { return k < 50 ? 4.0 : 1.0; });
        const SRun b = Run(Settings(EVesselMixing::Mixed, 7.0, EVesselFill::Empty), grid, [](size_t k) { return k < 50 ? 4.0 : 1.0; });
        double worst = 0.0;
        for (size_t k = 0; k < a.out.size(); ++k)
            worst = std::max(worst, std::abs(a.out[k] - b.out[k]));
        Check(worst < 1e-12, "Kaskade mit n = 1 rechnet wie ideal durchmischt");
    }

    // 2) Puls bei t = 0: mittlere Verweilzeit genau tau, fuer alle Mischungen.
    //    Im Partikelmodus mit gleich schweren Karten (Kartendichte nur nach
    //    Masse, ohne Abflachung): Das Konto zaehlt Karten, und bei verschieden
    //    schweren Karten streut die nach Masse gewichtete Verweilzeit eines
    //    einzelnen Pulses aus N Karten mit etwa sigma_T * CV / sqrt(N) - je
    //    nachdem, welche schweren Karten zufaellig laenger bleiben.
    for (size_t i = 0; i < 4; ++i)
    {
        const SVesselSettings& s = cases[i];
        for (ERepresentation representation : { ERepresentation::Continuum, ERepresentation::Particles })
        {
            SSimulationGrid grid = MakeGrid(representation, N, 600.0, dt);
            grid.cards.numberShare = 0.0;
            grid.cards.exponent = 1.0;
            const SRun run = Run(s, grid, [](size_t k) { return k == 0 ? 10.0 : 0.0; });
            double mass = 0.0, moment = 0.0;
            for (size_t k = 0; k < run.out.size(); ++k)
            {
                mass += run.out[k];
                moment += static_cast<double>(k) * dt * run.out[k];
            }
            const double mean = moment / mass;
            const bool particles = representation == ERepresentation::Particles;
            std::printf("      Puls, %s, %s: mittlere Verweilzeit %.6f s (tau = %g s)\n", Name(s), particles ? "Karten" : "Kontinuum",
                mean, s.residenceTime);
            Check(NearRel(mean, s.residenceTime, particles ? 0.02 : 1e-6),
                std::string(particles ? "Partikelmodus, gleich schwere Karten" : "Kontinuum") + ", " + Name(s) + ": mittlere Verweilzeit = tau"
                + (particles ? " (2 %)" : " (1e-6)"));
        }
    }

    // 3) Anfangsfuellung "wie im Dauerbetrieb": konstanter Zulauf kommt vom
    //    ersten Schritt an unveraendert heraus; Karten im Ablauf mit der
    //    Groessenverteilung des Zulaufs.
    for (size_t i = 0; i < 4; ++i)
    {
        SVesselSettings s = cases[i];
        s.fill = EVesselFill::SteadyState;
        const SSimulationGrid grid = MakeGrid(ERepresentation::Continuum, N, 40.0, dt);
        const SRun run = Run(s, grid, [](size_t) { return 10.0; });
        double worst = 0.0;
        for (double v : run.out)
            worst = std::max(worst, std::abs(v / (10.0 * run.feed) - 1.0));
        Check(worst < 1e-12 && NearRel(run.initial, 10.0 * run.feed * s.residenceTime, 1e-12),
            std::string("Kontinuum, ") + Name(s) + ": wie im Dauerbetrieb, Inhalt Zulauf mal tau, Ablauf = Zulauf");

        const SSimulationGrid cards = MakeGrid(ERepresentation::Particles, N, 40.0, dt);
        const SRun cardRun = Run(s, cards, [](size_t) { return 10.0; });
        const double card = 10.0 * cardRun.feed / static_cast<double>(N);
        double worstStep = 0.0, worstSize = 0.0;
        for (size_t k = 0; k < cardRun.out.size(); ++k)
        {
            worstStep = std::max(worstStep, std::abs(cardRun.out[k] - 10.0 * cardRun.feed));
            worstSize = std::max(worstSize, std::abs(cardRun.outSize[k] / cardRun.feedSize - 1.0));
        }
        std::printf("      %s, Karten: je Schritt hoechstens %.2f Karten daneben, mittlere Groesse bis %.3f %%, %zu Karten im Vorrat\n",
            Name(s), worstStep / card, 100.0 * worstSize, cardRun.finalCards);
        Check(worstStep < 4.0 * card && worstSize < 0.02 && NearRel(cardRun.initial, 10.0 * cardRun.feed * s.residenceTime, 1e-9),
            std::string("Partikelmodus, ") + Name(s) + ": wie im Dauerbetrieb, je Schritt hoechstens 4 Karten daneben, Groesse auf 2 %");
        Check(cardRun.merged == 0 && cardRun.warnings.empty(), std::string("Partikelmodus, ") + Name(s) + ": kein Zusammenlegen, keine Hinweise");
    }

    // Little: im Dauerbetrieb N tau / dt Karten im Vorrat (ideal durchmischt).
    {
        const SSimulationGrid cards = MakeGrid(ERepresentation::Particles, N, 150.0, dt);
        const SRun run = Run(Settings(EVesselMixing::Mixed, 10.0, EVesselFill::Empty), cards, [](size_t) { return 10.0; });
        std::printf("      Little: %zu Karten im Vorrat, erwartet %.0f\n", run.finalCards, N * 10.0 / dt);
        Check(NearRel(static_cast<double>(run.finalCards), N * 10.0 / dt, 0.01), "Partikelmodus: Karten im Vorrat nach Little N tau/dt (1 %)");
    }

    // Leerer Zulauf bei t = 0 und "wie im Dauerbetrieb": Hinweis, startet leer.
    {
        const SSimulationGrid grid = MakeGrid(ERepresentation::Continuum, N, 10.0, dt);
        const SRun run = Run(Settings(EVesselMixing::Mixed, 5.0, EVesselFill::SteadyState), grid, [](size_t k) { return k == 0 ? 0.0 : 3.0; });
        Check(run.initial == 0.0 && run.warnings.size() == 1 && run.warnings[0].find("startet leer") != std::string::npos,
            "wie im Dauerbetrieb ohne Zulauf bei t = 0: startet leer, mit Hinweis");
    }

    // 4) Vorgegebene Fuellung mit Stoff B, Zulauf Stoff A: Auswaschen. Der
    //    Inhalt an B faellt je Schritt um den Faktor e^(-dt/tau).
    for (ERepresentation representation : { ERepresentation::Continuum, ERepresentation::Particles })
    {
        SVesselSettings s = Settings(EVesselMixing::Mixed, 10.0, EVesselFill::Prescribed);
        s.fillMass = 50.0;
        s.fillMaterial = "B";
        const SSimulationGrid grid = MakeGrid(representation, N, 40.0, dt);
        const SRun run = Run(s, grid, [](size_t) { return 10.0; }, 0);
        const double a = VesselExitFraction(dt, 10.0);
        double outB = 0.0;
        for (double v : run.outB)
            outB += v * dt;
        const double expectedLeft = run.initial * std::pow(1.0 - a, static_cast<double>(run.out.size()));
        const bool particles = representation == ERepresentation::Particles;
        std::printf("      Auswaschen (%s): B uebrig %.4f kg, erwartet %.4f kg (Anfang %.4f kg)\n", particles ? "Karten" : "Kontinuum",
            run.finalB, expectedLeft, run.initial);
        Check(NearRel(run.initial, 50.0, particles ? 1e-9 : 1e-3), std::string(particles ? "Partikelmodus" : "Kontinuum")
            + ": vorgegebene Fuellung 50 kg (Kontinuum: Anteil im Raster)");
        Check(NearAbs(run.finalB, expectedLeft, particles ? 0.01 * run.initial : 1e-9 * run.initial)
                && NearRel(outB + run.finalB, run.initial, 1e-12),
            std::string(particles ? "Partikelmodus" : "Kontinuum") + ": Auswaschen von B nach (1 - a)^k, Masse von B erhalten");
    }

    // 5) Charge: kein Ablauf, alles bleibt drin.
    {
        SVesselSettings s = Settings(EVesselMixing::Mixed, 10.0, EVesselFill::Empty);
        s.batch = true;
        const SSimulationGrid cards = MakeGrid(ERepresentation::Particles, N, 10.0, dt);
        const SRun run = Run(s, cards, [](size_t) { return 2.0; });
        double out = 0.0;
        for (double v : run.out)
            out += v;
        Check(out == 0.0 && BalanceError(run, dt) < 1e-12, "Charge: kein Ablauf, Zulauf bleibt vollstaendig im Behaelter");
    }

    // 6) Vorrat voll: tau/dt = 100, stock limited to 10 x N cards -> Zulauf
    //    etwa 10 : 1 zusammengelegt. Masse exakt, Ablauf im Mittel = Zulauf.
    {
        SVesselSettings s = Settings(EVesselMixing::Mixed, 20.0, EVesselFill::SteadyState);
        const size_t few = 100;
        const SSimulationGrid cards = MakeGrid(ERepresentation::Particles, few, 80.0, dt);
        const SRun run = Run(s, cards, [](size_t) { return 10.0; }, 0, kRoomTemperature, 10 * few);
        double mean = 0.0;
        const size_t from = run.out.size() / 2;
        for (size_t k = from; k < run.out.size(); ++k)
            mean += run.out[k];
        mean /= static_cast<double>(run.out.size() - from);
        std::printf("      Vorrat voll: hoechstens %zu Karten (Grenze %zu), %zu zusammengelegt, Ablauf im Mittel %.4f statt %.4f\n",
            run.maxCards, run.cap, run.merged, mean, 10.0 * run.feed);
        Check(run.maxCards <= run.cap + 2 && run.merged > 0, "Vorrat voll: Kartenzahl bleibt an der Grenze, Zulauf wird zusammengelegt");
        Check(BalanceError(run, dt) < 1e-12 && NearRel(mean, 10.0 * run.feed, 0.03), "Vorrat voll: Masse exakt, Ablauf im Mittel = Zulauf (3 %)");
        Check(run.warnings.size() == 1 && run.warnings[0].find("Vorrat voll") != std::string::npos, "Vorrat voll: Hinweis");

        // Without the limit the stock grows with tau/dt: room for
        // (tau/dt + 2) x 2 N cards, nothing merged, N tau/dt cards inside.
        const SRun free = Run(s, cards, [](size_t) { return 10.0; });
        std::printf("      Vorrat nach tau/dt: Grenze %zu Karten (erwartet %d), %zu zusammengelegt, %zu Karten im Vorrat\n",
            free.cap, (100 + 2) * 2 * static_cast<int>(few), free.merged, free.finalCards);
        Check(free.cap + 1 >= (100 + 2) * 2 * few && free.cap <= (100 + 2) * 2 * few && free.merged == 0 && free.warnings.empty(),
            "Vorrat waechst mit tau/dt: (tau/dt + 2) x 2 N Karten, nichts zusammengelegt");
        Check(NearRel(static_cast<double>(free.finalCards), 100.0 * few, 0.02), "Vorrat nach tau/dt: N tau/dt Karten im Behaelter (2 %)");
    }

    // 7) Temperatur: Behaelter mit 293,15 K gefuellt, Zulauf mit 393,15 K
    //    (ohne Stoffdaten nach Masse gemischt): Ablauf naehert sich dem Zulauf.
    {
        SVesselSettings s = Settings(EVesselMixing::Mixed, 5.0, EVesselFill::Prescribed);
        s.fillMass = 50.0;
        s.fillMaterial = "A";
        const SSimulationGrid grid = MakeGrid(ERepresentation::Continuum, N, 60.0, dt);
        const SRun run = Run(s, grid, [](size_t) { return 10.0; }, 0, 393.15);
        bool rising = true;
        for (size_t k = 1; k < run.outT.size(); ++k)
            rising = rising && run.outT[k] >= run.outT[k - 1] - 1e-9;
        std::printf("      Temperatur am Ausgang: %.2f K bei t = 0, %.2f K am Ende\n", run.outT.front(), run.outT.back());
        Check(rising && run.outT.front() < 300.0 && NearAbs(run.outT.back(), 393.15, 0.1), "Temperatur steigt vom Inhalt zum Zulauf");
    }

    // 8) Im Flowsheet: Stoff der Anfangsfuellung zaehlt zu den aktiven
    //    Stoffen, der Behaelter rechnet im Partikelmodus, Massenbilanz.
    {
        CFlowsheetGraph graph;
        const std::string inlet = graph.AddInletNode("Inlet1", 0, 0);
        const std::string vessel = graph.AddVesselNode("Behaelter1", 1, 0);
        const std::string outlet = graph.AddOutletNode("Outlet1", 2, 0);
        graph.AddConnection(inlet, 0, vessel, 0);
        graph.AddConnection(vessel, 0, outlet, 0);
        graph.ConfigureGrid(30, 1e-6, 1e-3, 201, 40.0);
        graph.GetInlet(inlet)->SetMaterial("Sand");
        graph.GetInlet(inlet)->SetTimeProfile({ { 0.0, 10.0 } });
        SVesselSettings s = Settings(EVesselMixing::Cascade, 8.0, EVesselFill::Prescribed);
        s.fillMaterial = "Kalk";
        s.fillMass = 30.0;
        Check(graph.GetVessel(vessel)->SetSettings(s), "Einstellungen im Graphen uebernommen");
        const std::vector<std::string> compounds = graph.GetActiveCompounds();
        Check(compounds.size() == 2 && std::find(compounds.begin(), compounds.end(), "Kalk") != compounds.end(),
            "Stoff der Anfangsfuellung ist aktiver Stoff");
        graph.SetRepresentation(ERepresentation::Particles);
        SCardSettings cardSettings = graph.GetCardSettings();
        cardSettings.cardsPerSlice = N;
        graph.SetCardSettings(cardSettings);
        Check(graph.GetUnitsWithoutParticleSupport().empty() && graph.RunSimulations(1) == 1, "Partikelmodus: Behaelter rechnet mit");
        const CSimulationResults* in = graph.GetOutputResults(inlet, 0);
        const CSimulationResults* out = graph.GetDisplayResults(outlet, 0);
        double sumIn = 0.0, sumOut = 0.0;
        for (size_t k = 0; k < in->GetNumTimeSteps(); ++k)
        {
            sumIn += in->IntegrateOverSize(k) * graph.GetGrid().GetDt();
            sumOut += out->IntegrateOverSize(k) * graph.GetGrid().GetDt();
        }
        const CVesselUnit* unit = graph.GetVessel(vessel);
        const double error = std::abs(sumIn + unit->GetInitialContentMass() - sumOut - unit->GetContentMass()) / sumIn;
        std::printf("      Flowsheet: Zulauf %.3f kg + Anfang %.3f kg = Ablauf %.3f kg + Inhalt %.3f kg (Fehler %.1e)\n",
            sumIn, unit->GetInitialContentMass(), sumOut, unit->GetContentMass(), error);
        Check(error < 1e-6, "Flowsheet im Partikelmodus: Massenbilanz mit Inhalt (1e-6, Umrechnung aufs Raster)");
    }

    // 9) Daempfung einer OU-Schwankung: Inlet 10 kg/s -> OU-Splitter (mu 0,3,
    //    sigma 0,05, tau_OU 2 s) -> Behaelter (tau 10 s, wie im Dauerbetrieb)
    //    -> Outlet. Streuung des Ablaufs ueber 1000 Laeufe gegen die exakte
    //    Streuung der Schrittformel; der Partikelmodus folgt bei gleichem
    //    Seed dem Kontinuum.
    {
        const double mu = 0.3, sigma = 0.05, tauOu = 2.0, tau = 10.0, T = 60.0;
        const auto build = [&](CFlowsheetGraph& graph, std::string& outlet)
        {
            const std::string inlet = graph.AddInletNode("Inlet1", 0, 0);
            const std::string splitter = graph.AddSplitterNode("Splitter1", 2, 1, 0);
            const std::string vessel = graph.AddVesselNode("Behaelter1", 2, 0);
            outlet = graph.AddOutletNode("Outlet1", 3, 0);
            const std::string rest = graph.AddOutletNode("Outlet2", 3, 1);
            graph.AddConnection(inlet, 0, splitter, 0);
            graph.AddConnection(splitter, 0, vessel, 0);
            graph.AddConnection(vessel, 0, outlet, 0);
            graph.AddConnection(splitter, 1, rest, 0);
            graph.GetInlet(inlet)->SetTimeProfile({ { 0.0, 10.0 } });
            graph.GetSplitter(splitter)->SetFractions({ mu, 1.0 - mu });
            graph.GetSplitter(splitter)->SetStochastic(true);
            graph.GetSplitter(splitter)->SetFluctuations({ { sigma, tauOu } });
            SVesselSettings s = Settings(EVesselMixing::Mixed, tau, EVesselFill::SteadyState);
            graph.GetVessel(vessel)->SetSettings(s);
            graph.ConfigureGrid(12, 1e-6, 1e-3, SSimulationGrid::TimePointsFor(T, dt), T);
        };
        CFlowsheetGraph graph;
        std::string outlet;
        build(graph, outlet);
        const size_t runs = 1000;
        Check(graph.RunSimulations(runs) == runs, "OU-Daempfung: 1000 Laeufe im Kontinuum");
        const std::vector<std::vector<double>>& series = graph.GetStatistics(outlet, 0)->GetRunTimeSeries();
        const size_t k = static_cast<size_t>(std::llround(50.0 / dt));
        std::vector<double> values;
        for (const std::vector<double>& run : series)
            values.push_back(run[k]);
        double mean = 0.0, spread = 0.0;
        CEnsembleStatistics::MeanAndStdDev(values, mean, spread);

        // Exakte Streuung: y_k = b g_k + a (1 - b) sum_{m>=1} (1 - a)^(m-1) g_(k-m),
        // g mit Korrelation rho^|i-j|, rho = e^(-dt/tau_OU).
        const double a = VesselExitFraction(dt, tau), b = 1.0 - a * tau / dt, rho = std::exp(-dt / tauOu);
        const size_t M = 3000;
        std::vector<double> c(M), rhoPower(M);
        c[0] = b;
        rhoPower[0] = 1.0;
        for (size_t m = 1; m < M; ++m)
        {
            c[m] = a * (1.0 - b) * std::pow(1.0 - a, static_cast<double>(m - 1));
            rhoPower[m] = rhoPower[m - 1] * rho;
        }
        double variance = 0.0;
        for (size_t i = 0; i < M; ++i)
            for (size_t j = 0; j < M; ++j)
                variance += c[i] * c[j] * rhoPower[i > j ? i - j : j - i];
        const double feed = graph.GetDisplayResults(graph.GetNodes().front().id, 0)->IntegrateOverSize(k);
        const double expected = feed * sigma * std::sqrt(variance);
        const double continuous = feed * sigma * std::sqrt(tauOu / (tauOu + tau));
        std::printf("      OU-Daempfung: Streuung %.4f kg/s, Schrittformel %.4f kg/s, stetig %.4f kg/s (Mittel %.4f, erwartet %.4f)\n",
            spread, expected, continuous, mean, mu * feed);
        // Standardfehler der Streuung 1/sqrt(2 * 1000) = 2,2 %: Toleranz 4 Standardfehler.
        Check(NearRel(spread, expected, 0.09) && NearRel(mean, mu * feed, 0.01), "OU-Daempfung: Streuung wie die Schrittformel (9 %), Mittel 0,3 x Zulauf");

        // Gleicher Seed im Partikelmodus: derselbe OU-Verlauf, Ablauf je Schritt nahe am Kontinuum.
        graph.RunSimulation();
        const CSimulationResults* continuum = graph.GetDisplayResults(outlet, 0);
        std::vector<double> reference;
        for (size_t t = 0; t < continuum->GetNumTimeSteps(); ++t)
            reference.push_back(continuum->IntegrateOverSize(t));
        CFlowsheetGraph cardGraph;
        std::string cardOutlet;
        build(cardGraph, cardOutlet);
        cardGraph.SetRepresentation(ERepresentation::Particles);
        SCardSettings cardSettings = cardGraph.GetCardSettings();
        cardSettings.cardsPerSlice = 1000;
        cardGraph.SetCardSettings(cardSettings);
        cardGraph.RunSimulation();
        const CSimulationResults* cardsOut = cardGraph.GetDisplayResults(cardOutlet, 0);
        double worst = 0.0;
        for (size_t t = 0; t < cardsOut->GetNumTimeSteps(); ++t)
            worst = std::max(worst, std::abs(cardsOut->IntegrateOverSize(t) / reference[t] - 1.0));
        std::printf("      Partikelmodus mit gleichem Seed: je Schritt hoechstens %.2f %% neben dem Kontinuum\n", 100.0 * worst);
        Check(worst < 0.03, "OU-Daempfung im Partikelmodus: je Schritt hoechstens 3 % neben dem Kontinuum");
    }

    // 10) Losnummer: gleich grosse, verschieden schwere Karten. Pfropfen mit
    //     tau/dt = 50,5 (je Zulaufschritt die Haelfte nach 50, der Rest nach
    //     51 Schritten); Zulauf konstant, je Groesse eine schwere und eine
    //     leichte Karte (10 : 1). Das Konto schickt jede zweite Karte nach
    //     "frueh"; ohne neues Auslosen gingen in einem Schritt alle schweren
    //     frueh und der Ablauf laege in jedem zweiten Schritt 82 % daneben.
    {
        const SSimulationGrid grid = MakeGrid(ERepresentation::Particles, N, 30.0, dt);
        CVesselUnit vessel;
        Check(vessel.SetSettings(Settings(EVesselMixing::Plug, 10.1, EVesselFill::Empty)), "Losnummer: Einstellungen gueltig");
        vessel.Initialize(grid, 4711);
        const CStreamSlice single = MakeFeed(grid, 0);
        CStreamSlice pairs;
        pairs.Resize(grid.numSizeClasses, grid.GetNumCompounds(), grid.representation);
        for (size_t k = 0; k < single.GetNumCards(); ++k)
            for (double factor : { 10.0, 1.0 })
            {
                std::vector<double> flows(single.GetNumCompounds());
                for (size_t c = 0; c < flows.size(); ++c)
                    flows[c] = factor * single.GetCardMassFlow(k, c);
                pairs.AddCard(single.GetCardSize(k), flows);
            }
        const double in = pairs.GetCardsMassFlow();
        std::vector<CStreamSlice> outputs(1);
        double worst = 0.0;
        for (size_t k = 0; k < grid.numTimeSteps; ++k)
        {
            outputs[0].Resize(grid.numSizeClasses, grid.GetNumCompounds(), grid.representation);
            SStepContext context;
            context.stepIndex = k;
            context.dt = dt;
            context.time = static_cast<double>(k) * dt;
            vessel.Step(context, { &pairs }, outputs);
            if (k > 51)
                worst = std::max(worst, std::abs(outputs[0].GetCardsMassFlow() / in - 1.0));
        }
        std::printf("      Losnummer, Pfropfen mit Kartenpaaren 10 : 1: Ablauf je Schritt hoechstens %.1f %% neben dem Zulauf\n", 100.0 * worst);
        Check(worst < 0.2, "Losnummer: gleich grosse Karten 10 : 1 bei 50/50, Ablauf je Schritt hoechstens 20 % neben dem Zulauf");
    }

    // 11) Schwankender Zulauf direkt in den Behaelter (OU, sigma/mu = 0,167,
    //     tau_OU = 2 s; ideal durchmischt, tau = 10 s): Der Ablauf je Schritt
    //     streut um das Soll a * Inhalt / dt + b * Zulauf etwa so wie bei
    //     zufaelliger Auswahl (Rechnung mit 400 Karten je Schritt: 0,8 bis
    //     0,9 %); gleich grosse Karten in der Reihenfolge ihres Eintritts
    //     gaeben etwa 2 %.
    {
        const double tauOu = 2.0, sigmaRel = 0.167, tau = 10.0;
        const SSimulationGrid grid = MakeGrid(ERepresentation::Particles, N, 140.0, dt);
        std::vector<double> g(grid.numTimeSteps, 1.0);
        CRandom random(99);
        const double rho = std::exp(-dt / tauOu);
        for (size_t k = 1; k < g.size(); ++k)
            g[k] = 1.0 + (g[k - 1] - 1.0) * rho + sigmaRel * std::sqrt(1.0 - rho * rho) * random.Normal();
        const SRun run = Run(Settings(EVesselMixing::Mixed, tau, EVesselFill::Empty), grid, [&g](size_t k) { return 10.0 * g[k]; });
        const double a = VesselExitFraction(dt, tau), b = 1.0 - a * tau / dt;
        double content = run.initial, sum = 0.0, sum2 = 0.0;
        size_t count = 0;
        for (size_t k = 0; k < run.out.size(); ++k)
        {
            const double expected = a * content / dt + b * run.in[k];
            if (k >= 300 && expected > 0.0)
            {
                const double r = run.out[k] / expected - 1.0;
                sum += r;
                sum2 += r * r;
                ++count;
            }
            content += (run.in[k] - run.out[k]) * dt;
        }
        const double mean = count > 0 ? sum / static_cast<double>(count) : 1.0;
        const double spread = count > 0 ? std::sqrt(std::max(0.0, sum2 / static_cast<double>(count) - mean * mean)) : 1.0;
        std::printf("      Losnummer, schwankender Zulauf: Ablauf je Schritt um %.2f %% gestreut, im Mittel %+.3f %% neben dem Soll\n",
            100.0 * spread, 100.0 * mean);
        Check(spread < 0.014 && std::abs(mean) < 0.002,
            "Losnummer: schwankender Zulauf, Ablauf je Schritt hoechstens 1,4 % um das Soll gestreut, im Mittel auf 0,2 %");
    }

    return Summary();
}
