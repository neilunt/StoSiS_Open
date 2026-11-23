// Screen (Methodenhandbuch 7.5): grade efficiency T(d) of Plitt,
// Molerus-Hoffmann and Teipel-Hennig against the expressions of Dyssol,
// continuum exactly T(d) at every node, particle mode with the account
// method per compound and size range.
#include "ScreenUnit.h"
#include "CardPlacement.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "TestSupport.h"

using namespace stosis;
using namespace stosis_test;

// Cards of one distribution for one compound, scaled to mass [kg].
static void AddCards(CStreamSlice& slice, const SSizeDistribution& distribution, size_t numCards,
    size_t compound, double mass)
{
    SCardSettings settings;
    settings.cardsPerSlice = numCards;
    const SCardPlacement placement = PlaceCardsAtQuantiles(distribution, settings, 1e-6);
    for (size_t k = 0; k < placement.sizes.size(); ++k)
        slice.AddCard(placement.sizes[k], compound, mass * placement.massFractions[k]);
}

static SScreenSettings Make(EScreenModel model, double cutSize, double alpha, double beta = 0.5, double offset = 0.0)
{
    SScreenSettings s;
    s.model = model;
    s.cutSize = cutSize;
    s.alpha = alpha;
    s.beta = beta;
    s.offset = offset;
    return s;
}

int main()
{
    // 1) Formulas as in Dyssol (Example Units/Screen/Screen.cpp), Dyssol's
    //    default parameters: x_cut = 2 mm, alpha = 8, beta = 0.5, offset = 0.2.
    {
        const double xcut = 0.002, alpha = 8.0, beta = 0.5, offset = 0.2;
        const SScreenSettings plitt = Make(EScreenModel::Plitt, xcut, alpha);
        const SScreenSettings molerus = Make(EScreenModel::MolerusHoffmann, xcut, alpha);
        const SScreenSettings teipel = Make(EScreenModel::TeipelHennig, xcut, alpha, beta, offset);
        double worst[3] = { 0.0, 0.0, 0.0 };
        bool monotone = true;
        double last[3] = { -1.0, -1.0, -1.0 };
        for (int i = 0; i <= 400; ++i)
        {
            const double d = 1e-5 * std::pow(10.0, 3.0 * i / 400.0); // 10 um to 10 mm
            const double dyssol[3] = {
                1 - std::exp(-0.693 * std::pow(d / xcut, alpha)),
                1 / (1 + std::pow(xcut / d, 2.) * std::exp(alpha * (1 - std::pow(d / xcut, 2.0)))),
                (1 - std::pow(1 + 3 * std::pow(d / xcut, (d / xcut + alpha) * beta), -0.5)) * (1 - offset) + offset };
            const double ours[3] = { plitt.GradeEfficiency(d), molerus.GradeEfficiency(d), teipel.GradeEfficiency(d) };
            for (int m = 0; m < 3; ++m)
            {
                worst[m] = std::max(worst[m], std::abs(ours[m] - dyssol[m]));
                monotone = monotone && ours[m] >= last[m] - 1e-15;
                last[m] = ours[m];
            }
        }
        std::printf("      groesste Abweichung zu Dyssol: Plitt %.1e, Molerus-Hoffmann %.1e, Teipel-Hennig %.1e\n",
            worst[0], worst[1], worst[2]);
        Check(worst[0] < 1e-14 && worst[1] < 1e-14 && worst[2] < 1e-14,
            "Formeln: Plitt, Molerus-Hoffmann und Teipel-Hennig wie in Dyssol (10 um bis 10 mm)");
        Check(monotone, "Formeln: T(d) steigt mit d in allen drei Modellen");
        Check(NearAbs(plitt.GradeEfficiency(xcut), 0.5, 1e-4) && NearAbs(molerus.GradeEfficiency(xcut), 0.5, 1e-15)
            && NearAbs(Make(EScreenModel::TeipelHennig, xcut, alpha, beta, 0.0).GradeEfficiency(xcut), 0.5, 1e-15),
            "Formeln: T(x_cut) = 0,5 (Plitt bis auf 1e-4, Teipel-Hennig ohne Offset)");
        Check(NearAbs(teipel.GradeEfficiency(1e-9), offset, 1e-12) && NearAbs(teipel.GradeEfficiency(1.0), 1.0, 1e-15),
            "Formeln: Teipel-Hennig beginnt beim Offset und endet bei 1");

        CScreenUnit screen;
        Check(!screen.SetSettings(Make(EScreenModel::Plitt, 0.0, 8.0))
            && !screen.SetSettings(Make(EScreenModel::MolerusHoffmann, xcut, 0.0))
            && !screen.SetSettings(Make(EScreenModel::TeipelHennig, xcut, 8.0, 0.5, 1.5))
            && screen.SetSettings(Make(EScreenModel::Plitt, xcut, 0.0)),
            "Einstellungen: x_cut = 0, Molerus-Hoffmann mit alpha = 0 und Offset > 1 abgelehnt");
    }

    // 2) Continuum: coarse = T(d) * feed at every node, coarse + fines = feed.
    {
        SSimulationGrid grid;
        grid.numSizeClasses = 120;
        grid.sizeMin = 1e-6;
        grid.sizeMax = 1e-2;
        grid.compounds = { "Beton", "Ziegel" };
        CScreenUnit screen;
        screen.SetSettings(Make(EScreenModel::MolerusHoffmann, 200e-6, 3.0));
        screen.Initialize(grid, 1);
        CStreamSlice input;
        input.Resize(grid.numSizeClasses, 2);
        for (size_t c = 0; c < 2; ++c)
            for (size_t s = 0; s < grid.numSizeClasses; ++s)
                input.SetValue(c, s, (1.0 + c) * std::exp(-0.01 * static_cast<double>(s)));
        input.SetTemperature(320.0);
        input.SetPressure(1.5e5);
        std::vector<CStreamSlice> outputs(2);
        screen.Step({ 0, 0.0, 0.1 }, { &input }, outputs);
        double worstShare = 0.0, worstSum = 0.0;
        for (size_t c = 0; c < 2; ++c)
            for (size_t s = 0; s < grid.numSizeClasses; ++s)
            {
                const double v = input.GetValue(c, s);
                const double t = screen.GetSettings().GradeEfficiency(grid.GetSizeAt(s));
                worstShare = std::max(worstShare, std::abs(outputs[0].GetValue(c, s) - t * v));
                worstSum = std::max(worstSum, std::abs(outputs[0].GetValue(c, s) + outputs[1].GetValue(c, s) - v));
            }
        Check(worstShare == 0.0, "Kontinuum: Grobgut = T(d) x Zulauf an jedem Stuetzpunkt, jeder Stoff");
        Check(worstSum <= 1e-15, "Kontinuum: Grobgut + Feingut = Zulauf (Masse bleibt)");
        Check(outputs[0].GetTemperature() == 320.0 && outputs[1].GetTemperature() == 320.0
            && outputs[0].GetPressure() == 1.5e5 && outputs[1].GetPressure() == 1.5e5,
            "Kontinuum: Temperatur und Druck in beiden Ausgaengen wie im Zulauf");
    }

    // 3) Particle mode: two compounds around x_cut, Plitt alpha = 3. Over
    //    1000 steps every compound and size range goes with T(d) into the
    //    coarse product (expected: sum of T(d) * m over its cards); no card
    //    is copied; the accounts stay below half the largest card share.
    {
        const size_t steps = 1000;
        SSimulationGrid grid;
        grid.representation = ERepresentation::Particles;
        grid.numTimeSteps = steps;
        grid.totalTime = 100.0;
        grid.compounds = { "Beton", "Ziegel" };
        CScreenUnit screen;
        screen.SetSettings(Make(EScreenModel::Plitt, 100e-6, 3.0));
        screen.Initialize(grid, 42);
        CStreamSlice input;
        input.Resize(300, 2, ERepresentation::Particles);
        AddCards(input, SSizeDistribution{ EDistributionType::LogNormal, 100e-6, 0.6 }, 2000, 0, 3.0);
        AddCards(input, SSizeDistribution{ EDistributionType::RRSB, 150e-6, 3.0 }, 2000, 1, 1.0);

        std::vector<double> sizes, masses;
        for (size_t k = 0; k < input.GetNumCards(); ++k)
        {
            sizes.push_back(input.GetCardSize(k));
            masses.push_back(input.GetCardMass(k));
        }
        // The screen's account works with card shares without flattening.
        const std::vector<double> shares = ComputeAccountShares(sizes, masses, grid.cards, grid.GetCardCountMin());
        const double largestShare = *std::max_element(shares.begin(), shares.end());

        const auto compoundOf = [](const CStreamSlice& slice, size_t card)
        { return slice.GetCardMass(card, 1) > slice.GetCardMass(card, 0) ? 1 : 0; };
        std::map<std::pair<int, int>, double> rangeIn, rangeExpected, rangeCoarse;
        for (size_t k = 0; k < input.GetNumCards(); ++k)
        {
            const std::pair<int, int> key{ compoundOf(input, k), GetCardRangeIndex(sizes[k], grid.cards) };
            rangeIn[key] += masses[k];
            rangeExpected[key] += screen.GetSettings().GradeEfficiency(sizes[k]) * masses[k];
        }
        double expectedTotal = 0.0;
        for (const auto& e : rangeExpected)
            expectedTotal += e.second;

        std::vector<CStreamSlice> outputs(2);
        bool countsOk = true, massOk = true;
        size_t sureCoarse = 0, sureCoarseWrong = 0;
        double coarseTotal = 0.0, worstAccount = 0.0;
        std::map<std::pair<int, int>, double> coarseSum;
        for (size_t k = 0; k < steps; ++k)
        {
            for (CStreamSlice& o : outputs)
                o.Resize(300, 2, ERepresentation::Particles);
            screen.Step({ k, 0.1 * k, 0.1 }, { &input }, outputs);
            countsOk = countsOk && outputs[0].GetNumCards() + outputs[1].GetNumCards() == input.GetNumCards();
            massOk = massOk && NearRel(outputs[0].GetCardsMass() + outputs[1].GetCardsMass(), input.GetCardsMass(), 1e-12);
            coarseTotal += outputs[0].GetCardsMass();
            worstAccount = std::max(worstAccount, screen.GetLargestAccount());
            for (size_t c = 0; c < outputs[0].GetNumCards(); ++c)
            {
                coarseSum[{ compoundOf(outputs[0], c), GetCardRangeIndex(outputs[0].GetCardSize(c), grid.cards) }]
                    += outputs[0].GetCardMass(c);
            }
            for (size_t c = 0; c < outputs[1].GetNumCards(); ++c)
                if (screen.GetSettings().GradeEfficiency(outputs[1].GetCardSize(c)) >= 1.0)
                    ++sureCoarseWrong;
            for (size_t c = 0; c < outputs[0].GetNumCards(); ++c)
                if (screen.GetSettings().GradeEfficiency(outputs[0].GetCardSize(c)) >= 1.0)
                    ++sureCoarse;
        }
        double worstRange = 0.0;
        for (const auto& e : rangeIn)
        {
            if (e.second < 0.01 * input.GetCardsMass())
                continue;
            const double share = coarseSum[e.first] / (e.second * steps);
            const double expected = rangeExpected[e.first] / e.second;
            worstRange = std::max(worstRange, std::abs(share - expected));
        }
        const double totalShare = coarseTotal / (input.GetCardsMass() * steps);
        const double expectedShare = expectedTotal / input.GetCardsMass();
        std::printf("      Grobgut %.5f (erwartet %.5f); je Stoff und Groessenbereich hoechstens %.1e neben T;\n"
                    "      groesstes Konto %.2e, groesster Kartenanteil %.2e; Karten mit T = 1 je Schritt: %zu\n",
            totalShare, expectedShare, worstRange, worstAccount, largestShare, sureCoarse / steps);
        Check(countsOk, "Partikel: Grobgut + Feingut = Zulauf an Karten (nichts kopiert)");
        Check(massOk, "Partikel: Masse bleibt in jedem Schritt");
        Check(NearAbs(totalShare, expectedShare, 1e-3), "Partikel: Grobgut ueber 1000 Schritte wie im Kontinuum (1e-3)");
        Check(worstRange < 5e-3, "Partikel: jeder Stoff und Groessenbereich geht mit T(d) ins Grobgut (5e-3)");
        Check(worstAccount <= 0.5 * largestShare + 1e-15, "Partikel: Konten bleiben unter einem halben Kartenanteil");
        Check(sureCoarse > 0 && sureCoarseWrong == 0, "Partikel: Karten mit T = 1 immer ins Grobgut");
    }

    // 4) T = 0 and T = 1 exactly (Plitt, alpha = 8, d = x_cut / 1000 and
    //    x_cut * 1000): these cards never go through an account.
    {
        SSimulationGrid grid;
        grid.representation = ERepresentation::Particles;
        CScreenUnit screen;
        screen.SetSettings(Make(EScreenModel::Plitt, 100e-6, 8.0));
        screen.Initialize(grid, 3);
        CStreamSlice input;
        input.Resize(300, 1, ERepresentation::Particles);
        input.AddCard(0.1e-6, 0, 1.0);
        input.AddCard(100e-6, 0, 1.0);
        input.AddCard(100e-3, 0, 1.0);
        bool ok = screen.GetSettings().GradeEfficiency(0.1e-6) == 0.0 && screen.GetSettings().GradeEfficiency(100e-3) == 1.0;
        std::vector<CStreamSlice> outputs(2);
        for (size_t k = 0; k < 200; ++k)
        {
            for (CStreamSlice& o : outputs)
                o.Resize(300, 1, ERepresentation::Particles);
            screen.Step({ k, 0.1 * k, 0.1 }, { &input }, outputs);
            for (size_t c = 0; c < outputs[0].GetNumCards(); ++c)
                ok = ok && outputs[0].GetCardSize(c) != 0.1e-6;
            for (size_t c = 0; c < outputs[1].GetNumCards(); ++c)
                ok = ok && outputs[1].GetCardSize(c) != 100e-3;
        }
        Check(ok, "Partikel: T = 0 immer ins Feingut, T = 1 immer ins Grobgut (200 Schritte)");
    }

    // 5) Several decks: settings and shares of the outputs.
    {
        CScreenUnit screen(4);
        const std::vector<SScreenSettings> defaults = screen.GetDecks();
        const bool structure = screen.GetNumOutputs() == 4 && screen.GetNumDecks() == 3
            && CheckScreenDecks(defaults, 3).empty() && defaults[0].cutSize > defaults[1].cutSize
            && defaults[1].cutSize > defaults[2].cutSize && CScreenUnit().GetSettings().cutSize == 100e-6;
        std::vector<SScreenSettings> rising = defaults;
        std::swap(rising[0].cutSize, rising[1].cutSize);
        std::vector<SScreenSettings> equal = defaults;
        equal[2].cutSize = equal[1].cutSize;
        std::vector<SScreenSettings> invalid = defaults;
        invalid[1].alpha = -1.0;
        const bool rejected = !screen.SetDecks(rising) && !screen.SetDecks(equal) && !screen.SetDecks(invalid)
            && !screen.SetDecks({ defaults[0], defaults[1] }) && screen.GetDecks()[0].cutSize == defaults[0].cutSize;
        double worstSum = 0.0;
        bool inRange = true;
        std::vector<double> shares;
        for (int i = 0; i <= 400; ++i)
        {
            const double d = 1e-6 * std::pow(10.0, 3.0 * i / 400.0); // 1 um to 1 mm
            ComputeScreenShares(defaults, d, shares);
            double sum = 0.0;
            for (double t : shares)
            {
                inRange = inRange && t >= 0.0 && t <= 1.0;
                sum += t;
            }
            worstSum = std::max(worstSum, std::abs(sum - 1.0));
        }
        Check(structure, "Mehrdeck: Sieb mit 4 Ausgaengen hat 3 Decks, Trenngroessen fallen von oben nach unten (100, 50, 25 um)");
        Check(rejected, "Mehrdeck: steigende oder gleiche Trenngroessen, ungueltiges Deck und falsche Deckzahl abgelehnt");
        Check(shares.size() == 4 && inRange && worstSum <= 1e-15, "Mehrdeck: Anteile je Groesse zwischen 0 und 1, Summe 1");
    }

    // 6) Continuum: a screen with three decks gives exactly what three
    //    screens in series give (coarse of each deck out, fines onto the next).
    {
        SSimulationGrid grid;
        grid.numSizeClasses = 200;
        grid.sizeMin = 1e-6;
        grid.sizeMax = 1e-2;
        grid.compounds = { "Beton", "Ziegel" };
        const std::vector<SScreenSettings> decks = { Make(EScreenModel::Plitt, 2e-3, 4.0),
            Make(EScreenModel::MolerusHoffmann, 500e-6, 3.0), Make(EScreenModel::TeipelHennig, 100e-6, 2.0, 0.6, 0.05) };
        CScreenUnit multi(4);
        const bool taken = multi.SetDecks(decks);
        multi.Initialize(grid, 1);
        CStreamSlice input;
        input.Resize(grid.numSizeClasses, 2);
        double level = 0.0;
        for (size_t c = 0; c < 2; ++c)
            for (size_t s = 0; s < grid.numSizeClasses; ++s)
            {
                input.SetValue(c, s, (1.0 + c) * std::exp(-0.01 * static_cast<double>(s)));
                level = std::max(level, input.GetValue(c, s));
            }
        input.SetTemperature(320.0);
        input.SetPressure(1.5e5);
        std::vector<CStreamSlice> outputs(4);
        multi.Step({ 0, 0.0, 0.1 }, { &input }, outputs);

        std::vector<CStreamSlice> series(4);
        CStreamSlice feed = input;
        for (size_t k = 0; k < 3; ++k)
        {
            CScreenUnit single;
            single.SetSettings(decks[k]);
            single.Initialize(grid, 1);
            std::vector<CStreamSlice> pair(2);
            single.Step({ 0, 0.0, 0.1 }, { &feed }, pair);
            series[k] = pair[0];
            feed = pair[1];
        }
        series[3] = feed;
        double worstSeries = 0.0, worstSum = 0.0;
        bool state = true;
        for (size_t j = 0; j < 4; ++j)
        {
            state = state && outputs[j].GetTemperature() == 320.0 && outputs[j].GetPressure() == 1.5e5;
            for (size_t c = 0; c < 2; ++c)
                for (size_t s = 0; s < grid.numSizeClasses; ++s)
                    worstSeries = std::max(worstSeries, std::abs(outputs[j].GetValue(c, s) - series[j].GetValue(c, s)) / level);
        }
        for (size_t c = 0; c < 2; ++c)
            for (size_t s = 0; s < grid.numSizeClasses; ++s)
            {
                double sum = 0.0;
                for (size_t j = 0; j < 4; ++j)
                    sum += outputs[j].GetValue(c, s);
                worstSum = std::max(worstSum, std::abs(sum - input.GetValue(c, s)) / level);
            }
        std::printf("      Mehrdeck gegen Siebe in Reihe: hoechstens %.1e; Summe der Ausgaenge - Zulauf hoechstens %.1e\n",
            worstSeries, worstSum);
        Check(taken && worstSeries <= 1e-15, "Mehrdeck, Kontinuum: 3 Decks = 3 Siebe in Reihe (1e-15)");
        Check(worstSum <= 1e-15, "Mehrdeck, Kontinuum: Summe der 4 Ausgaenge = Zulauf (Masse bleibt)");
        Check(state, "Mehrdeck, Kontinuum: Temperatur und Druck in allen Ausgaengen wie im Zulauf");
    }

    // 7) Particle mode with two decks (x_cut 60 and 30 um, Plitt alpha = 20):
    //    > 60 um, 30 ... 60 um, < 30 um. Two log-normal compounds. Over 1000
    //    steps every output gets its share of the mass, also per compound and
    //    size range; no card is copied; the accounts stay below one card
    //    share. The shares of the model follow exactly from the distributions
    //    (integral of q3(d) t_j(d)); for comparison the ideal sharp cut, Q3.
    {
        const size_t steps = 1000;
        SSimulationGrid grid;
        grid.representation = ERepresentation::Particles;
        grid.numTimeSteps = steps;
        grid.totalTime = 100.0;
        grid.compounds = { "Sand", "Kalk" };
        const std::vector<SScreenSettings> decks = { Make(EScreenModel::Plitt, 60e-6, 20.0),
            Make(EScreenModel::Plitt, 30e-6, 20.0) };
        CScreenUnit screen(3);
        const bool taken = screen.SetDecks(decks);
        screen.Initialize(grid, 42);
        const SSizeDistribution sand{ EDistributionType::LogNormal, 45e-6, 0.5 };
        const SSizeDistribution lime{ EDistributionType::LogNormal, 70e-6, 0.4 };
        const double massSand = 3.0, massLime = 1.0;
        CStreamSlice input;
        input.Resize(300, 2, ERepresentation::Particles);
        AddCards(input, sand, 2000, 0, massSand);
        AddCards(input, lime, 2000, 1, massLime);

        // Shares of the model from the distributions: integral over ln d.
        double exact[3] = { 0.0, 0.0, 0.0 };
        std::vector<double> t;
        for (const auto& [distribution, mass] : { std::pair{ sand, massSand }, std::pair{ lime, massLime } })
        {
            const int n = 200000;
            const double lo = std::log(distribution.position) - 8.0 * distribution.width;
            const double step = 16.0 * distribution.width / n;
            for (int i = 0; i < n; ++i)
            {
                const double d = std::exp(lo + (i + 0.5) * step);
                ComputeScreenShares(decks, d, t);
                for (size_t j = 0; j < 3; ++j)
                    exact[j] += mass * distribution.Density(d) * d * step * t[j];
            }
        }
        const double total = massSand + massLime;
        const double sharp[3] = {
            (massSand * (1.0 - sand.Cumulative(60e-6)) + massLime * (1.0 - lime.Cumulative(60e-6))) / total,
            (massSand * (sand.Cumulative(60e-6) - sand.Cumulative(30e-6)) + massLime * (lime.Cumulative(60e-6) - lime.Cumulative(30e-6))) / total,
            (massSand * sand.Cumulative(30e-6) + massLime * lime.Cumulative(30e-6)) / total };

        std::vector<double> sizes, masses;
        for (size_t k = 0; k < input.GetNumCards(); ++k)
        {
            sizes.push_back(input.GetCardSize(k));
            masses.push_back(input.GetCardMass(k));
        }
        const std::vector<double> accountShares = ComputeAccountShares(sizes, masses, grid.cards, grid.GetCardCountMin());
        const double largestShare = *std::max_element(accountShares.begin(), accountShares.end());
        const auto compoundOf = [](const CStreamSlice& slice, size_t card)
        { return slice.GetCardMass(card, 1) > slice.GetCardMass(card, 0) ? 1 : 0; };
        std::map<std::pair<int, int>, double> rangeIn;
        std::map<std::pair<int, int>, double> rangeExpected[3], rangeOut[3];
        double cardsExpected[3] = { 0.0, 0.0, 0.0 };
        for (size_t k = 0; k < input.GetNumCards(); ++k)
        {
            const std::pair<int, int> key{ compoundOf(input, k), GetCardRangeIndex(sizes[k], grid.cards) };
            rangeIn[key] += masses[k];
            ComputeScreenShares(decks, sizes[k], t);
            for (size_t j = 0; j < 3; ++j)
            {
                rangeExpected[j][key] += t[j] * masses[k];
                cardsExpected[j] += t[j] * masses[k] / input.GetCardsMass();
            }
        }

        std::vector<CStreamSlice> outputs(3);
        bool countsOk = true, massOk = true;
        double outTotal[3] = { 0.0, 0.0, 0.0 };
        double worstAccount = 0.0;
        for (size_t k = 0; k < steps; ++k)
        {
            for (CStreamSlice& o : outputs)
                o.Resize(300, 2, ERepresentation::Particles);
            screen.Step({ k, 0.1 * k, 0.1 }, { &input }, outputs);
            countsOk = countsOk && outputs[0].GetNumCards() + outputs[1].GetNumCards() + outputs[2].GetNumCards() == input.GetNumCards();
            massOk = massOk && NearRel(outputs[0].GetCardsMass() + outputs[1].GetCardsMass() + outputs[2].GetCardsMass(),
                input.GetCardsMass(), 1e-12);
            worstAccount = std::max(worstAccount, screen.GetLargestAccount());
            for (size_t j = 0; j < 3; ++j)
            {
                outTotal[j] += outputs[j].GetCardsMass() / (input.GetCardsMass() * steps);
                for (size_t c = 0; c < outputs[j].GetNumCards(); ++c)
                    rangeOut[j][{ compoundOf(outputs[j], c), GetCardRangeIndex(outputs[j].GetCardSize(c), grid.cards) }]
                        += outputs[j].GetCardMass(c);
            }
        }
        double worstTotal = 0.0, worstModel = 0.0, worstRange = 0.0;
        for (size_t j = 0; j < 3; ++j)
        {
            worstTotal = std::max(worstTotal, std::abs(outTotal[j] - cardsExpected[j]));
            worstModel = std::max(worstModel, std::abs(outTotal[j] - exact[j] / total));
            for (const auto& e : rangeIn)
            {
                if (e.second < 0.01 * input.GetCardsMass())
                    continue;
                worstRange = std::max(worstRange,
                    std::abs(rangeOut[j][e.first] / (e.second * steps) - rangeExpected[j][e.first] / e.second));
            }
        }
        std::printf("      Mehrdeck 60/30 um: Ausgang 1 (> 60 um) %.4f, 2 (30-60 um) %.4f, 3 (< 30 um) %.4f;\n"
                    "      Modell exakt %.4f / %.4f / %.4f, scharfer Schnitt (Q3) %.4f / %.4f / %.4f;\n"
                    "      je Stoff und Groessenbereich hoechstens %.1e daneben; groesstes Konto %.2e, groesster Kartenanteil %.2e\n",
            outTotal[0], outTotal[1], outTotal[2], exact[0] / total, exact[1] / total, exact[2] / total,
            sharp[0], sharp[1], sharp[2], worstRange, worstAccount, largestShare);
        Check(taken, "Mehrdeck, Partikel: 2 Decks mit 60 und 30 um angenommen");
        Check(countsOk && massOk, "Mehrdeck, Partikel: Summe der Ausgaenge = Zulauf an Karten und Masse (nichts kopiert)");
        Check(worstTotal <= 1e-3, "Mehrdeck, Partikel: jeder Ausgang ueber 1000 Schritte mit seinem Anteil (1e-3)");
        Check(worstModel <= 2e-3, "Mehrdeck, Partikel: Anteile wie aus den Verteilungen exakt gerechnet (2e-3)");
        Check(worstRange < 5e-3, "Mehrdeck, Partikel: jeder Stoff und Groessenbereich mit seinem Anteil je Ausgang (5e-3)");
        Check(worstAccount <= largestShare + 1e-15, "Mehrdeck, Partikel: Konten bleiben unter einem Kartenanteil");
    }

    return Summary();
}
