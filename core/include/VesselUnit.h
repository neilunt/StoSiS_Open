#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "FlowsheetUnit.h"
#include "Random.h"
#include "StreamMixing.h"
#include "CardLimits.h"
#include "CardPlacement.h"
#include "SizeDistribution.h"
#include "SimulationLog.h"

namespace stosis
{
    // Mischverhalten im Behaelter (Methodenhandbuch 7.4). Alle vier haben die
    // mittlere Verweilzeit tau; verschieden ist, welche Karten gehen.
    enum class EVesselMixing
    {
        Mixed,    // ideal durchmischt: jede Karte geht je Schritt mit derselben Wahrscheinlichkeit
        Plug,     // Pfropfenstroemung: jede Karte bleibt genau tau
        Cascade,  // Kaskade aus n ideal durchmischten Kesseln mit je tau / n
        CoreFlow  // Kernfluss: ein Teil laeuft als Pfropfen durch den Kern, der Rest durchmischt durch die Randzone
    };

    inline const char* GetVesselMixingName(EVesselMixing mixing)
    {
        switch (mixing)
        {
        case EVesselMixing::Plug:     return "Pfropfenstroemung";
        case EVesselMixing::Cascade:  return "Kaskade";
        case EVesselMixing::CoreFlow: return "Kernfluss";
        case EVesselMixing::Mixed:
        default:                      return "ideal durchmischt";
        }
    }

    // Inhalt bei t = 0.
    enum class EVesselFill
    {
        Empty,       // leer, wie beim Anfahren
        SteadyState, // wie im Dauerbetrieb mit dem Zulauf bei t = 0
        Prescribed   // vorgegeben: Masse, Stoff, Groessenverteilung
    };

    inline const char* GetVesselFillName(EVesselFill fill)
    {
        switch (fill)
        {
        case EVesselFill::SteadyState: return "wie im Dauerbetrieb";
        case EVesselFill::Prescribed:  return "vorgegeben";
        case EVesselFill::Empty:
        default:                       return "leer";
        }
    }

    // Einstellungen eines Behaelters.
    struct SVesselSettings
    {
        EVesselMixing mixing = EVesselMixing::Mixed;
        double residenceTime = 10.0; // mittlere Verweilzeit tau [s]
        bool batch = false;          // Charge: kein Ablauf (tau = unendlich), Mischung ohne Bedeutung
        size_t cells = 3;            // Kaskade: Zahl der Kessel
        double coreShare = 0.6;      // Kernfluss: Anteil des Zulaufs, der durch den Kern laeuft
        double coreTime = 0.3;       // Kernfluss: Verweilzeit im Kern relativ zu tau
        EVesselFill fill = EVesselFill::SteadyState;
        // Nur fuer die vorgegebene Anfangsfuellung:
        double fillMass = 100.0;     // kg
        std::string fillMaterial = "Sand";
        SSizeDistribution fillDistribution;
        double fillTemperature = kRoomTemperature; // K
        double fillPressure = kRoomPressure;       // Pa
        // Vorrat im Partikelmodus: hoechstens stockFactor x N Karten.
        double stockFactor = 100.0;

        static constexpr size_t kMaxCells = 50;

        // Mittlere Verweilzeit der Randzone beim Kernfluss, damit das Mittel
        // ueber Kern und Rand tau bleibt: phi tau_K + (1 - phi) tau_R = tau.
        double GetWallTime() const
        {
            return (1.0 - coreShare * coreTime) * residenceTime / (1.0 - coreShare);
        }

        // Leer = gueltig, sonst ein Satz, was nicht passt.
        std::string Check() const
        {
            if (!batch && !(residenceTime > 0.0 && std::isfinite(residenceTime)))
                return "Die Verweilzeit tau muss groesser als 0 sein (ohne Ablauf: Charge waehlen).";
            if (!batch && mixing == EVesselMixing::Cascade && (cells < 1 || cells > kMaxCells))
                return "Die Kaskade braucht 1 bis " + std::to_string(kMaxCells) + " Kessel.";
            if (!batch && mixing == EVesselMixing::CoreFlow)
            {
                if (!(coreShare > 0.0 && coreShare < 1.0))
                    return "Der Kernanteil muss zwischen 0 und 100 % liegen.";
                if (!(coreTime > 0.0 && coreTime < 1.0))
                    return "Die Kernzeit muss zwischen 0 und 100 % von tau liegen - sonst ist der Kern nicht schneller als die Randzone.";
            }
            if (fill == EVesselFill::Prescribed)
            {
                if (!(fillMass >= 0.0 && std::isfinite(fillMass)))
                    return "Die Masse der Anfangsfuellung darf nicht negativ sein.";
                if (fillDistribution.type == EDistributionType::Table)
                {
                    const std::string table = fillDistribution.CheckTable();
                    if (!table.empty())
                        return "Verteilung der Anfangsfuellung: " + table;
                }
                else if (!(fillDistribution.position > 0.0) || !(fillDistribution.width > 0.0))
                    return "Die Verteilung der Anfangsfuellung braucht positive Parameter.";
                if (!(fillTemperature > 0.0))
                    return "Die Temperatur der Anfangsfuellung muss ueber 0 K liegen.";
            }
            if (!(stockFactor >= 1.0))
                return "Der Vorrat muss mindestens N Karten fassen.";
            return std::string();
        }
    };

    // Wahrscheinlichkeit, dass eine Karte im ideal durchmischten Behaelter
    // waehrend eines Schritts geht: a = 1 - exp(-dt / tau).
    inline double VesselExitFraction(double dt, double tau)
    {
        return (dt > 0.0 && tau > 0.0) ? -std::expm1(-dt / tau) : (dt > 0.0 ? 1.0 : 0.0);
    }

    // Summenkurve F(t) der Verweilzeitverteilung (geschlossen): Anteil, der
    // nach der Zeit t wieder gegangen ist. Bei konstantem Zulauf in einen
    // leeren Behaelter ist das zugleich der Ablauf relativ zum Zulauf.
    //   ideal durchmischt: 1 - exp(-t/tau)
    //   Pfropfenstroemung: Sprung bei tau
    //   Kaskade: Erlang-Verteilung, 1 - exp(-x) sum_{i<n} x^i / i!, x = n t / tau
    //   Kernfluss: phi * Sprung bei kappa tau + (1 - phi) (1 - exp(-t/tau_R))
    inline double VesselResidenceCdf(const SVesselSettings& settings, double t)
    {
        if (settings.batch || !(t > 0.0))
            return 0.0;
        const double tau = settings.residenceTime;
        switch (settings.mixing)
        {
        case EVesselMixing::Plug:
            return t >= tau ? 1.0 : 0.0;
        case EVesselMixing::Cascade:
        {
            const size_t n = std::max<size_t>(settings.cells, 1);
            const double x = static_cast<double>(n) * t / tau;
            double term = 1.0, sum = 1.0;
            for (size_t i = 1; i < n; ++i)
            {
                term *= x / static_cast<double>(i);
                sum += term;
            }
            return std::clamp(1.0 - std::exp(-x) * sum, 0.0, 1.0);
        }
        case EVesselMixing::CoreFlow:
            return settings.coreShare * (t >= settings.coreTime * tau ? 1.0 : 0.0)
                + (1.0 - settings.coreShare) * -std::expm1(-t / settings.GetWallTime());
        case EVesselMixing::Mixed:
        default:
            return -std::expm1(-t / tau);
        }
    }

    // Zeit, bis der Anteil fraction des Zulaufs wieder gegangen ist (Bisektion
    // auf VesselResidenceCdf); unendlich bei der Charge.
    inline double VesselTimeToFraction(const SVesselSettings& settings, double fraction)
    {
        if (settings.batch || !(settings.residenceTime > 0.0) || !(fraction > 0.0))
            return settings.batch ? HUGE_VAL : 0.0;
        double hi = settings.residenceTime;
        while (VesselResidenceCdf(settings, hi) < fraction && hi < 1e15)
            hi *= 2.0;
        double lo = 0.0;
        for (int i = 0; i < 200; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            if (VesselResidenceCdf(settings, mid) >= fraction)
                hi = mid;
            else
                lo = mid;
        }
        return hi;
    }

    // P(X > m) fuer X ~ Poisson(lambda), als Summe der Glieder ab m + 1 -
    // genau auch dort, wo 1 - P(X <= m) ausloeschen wuerde.
    inline double PoissonTail(size_t m, double lambda)
    {
        if (!(lambda > 0.0))
            return 0.0;
        double term = std::exp(-lambda + static_cast<double>(m + 1) * std::log(lambda) - std::lgamma(static_cast<double>(m) + 2.0));
        double sum = 0.0;
        for (size_t i = m + 1; i < m + 200000; ++i)
        {
            sum += term;
            term *= lambda / static_cast<double>(i + 1);
            if (term <= 1e-18 * sum && static_cast<double>(i) > lambda)
                break;
        }
        return std::min(sum, 1.0);
    }

    // Behaelter mit Hold-up (Methodenhandbuch 7.4): ein Eingang, ein Ausgang,
    // Ablauf erster Ordnung mit fester mittlerer Verweilzeit tau oder Charge
    // ohne Ablauf. Das Mischverhalten bestimmt, welches Material geht:
    //  - ideal durchmischt: Vom Inhalt geht je Schritt der Anteil
    //    a = 1 - exp(-dt/tau), vom Zulauf desselben Schritts b = 1 - (tau/dt) a.
    //    Exakt fuer einen im Schritt konstanten Zulauf; die mittlere
    //    Verweilzeit ist genau tau.
    //  - Pfropfenstroemung: Alles bleibt genau tau. Ist tau/dt = q + r nicht
    //    ganzzahlig, geht der Anteil 1 - r eines Zulaufschritts nach q
    //    Schritten, der Rest r nach q + 1 (der Zulauf verteilt sich ueber den
    //    Schritt) - wie die lineare Interpolation der Verzoegerung.
    //  - Kaskade aus n Kesseln mit je tau/n: exakte Schrittformel fuer
    //    stueckweise konstanten Zulauf. Aus einem Kessel kommt Material in
    //    einem Schritt m Kessel weiter mit der Poisson-Wahrscheinlichkeit
    //    P_m (lambda = n dt / tau); was ueber den letzten Kessel hinauskaeme,
    //    geht. Fuer den Zulauf gilt Q_m = P(X > m) / lambda.
    //  - Kernfluss: Der Anteil phi des Zulaufs laeuft als Pfropfen in
    //    kappa tau durch den Kern, der Rest durch eine ideal durchmischte
    //    Randzone mit tau_R = (1 - phi kappa) tau / (1 - phi).
    // Jeder Schritt bringt die Masse des Zulaufs mal dt; der Ausgang zeigt,
    // was im Schritt austritt, geteilt durch dt. So bleibt die Masse exakt
    // erhalten: Summe Zulauf = Summe Ablauf + Zunahme des Inhalts.
    //
    // In particle mode the account method moves whole cards: every card pays
    // the share of each target into that target's account and goes where
    // most is standing; that account is charged one card. Unlike in the
    // splitter every card counts 1, not its share of the card density, so
    // that every card leaves with the same probability whatever its weight.
    // The cards of a region lie in compartments per compound and size class
    // (SCardStore); the account walk over the stock is done in one jump
    // (StoreDraw): the positions where the account "goes" fires follow
    // directly from the account, they give how many cards leave each
    // compartment, and which cards of a compartment leave is drawn at random.
    // A step therefore only touches the cards that move, and leaving does
    // not depend on age. The stock holds (tau/dt + 2) slices of cards
    // (StockCap), at most 10 million cards; beyond that newly arriving cards
    // are merged in pairs (LimitIncoming). Temperature and pressure of each
    // region follow from the energy balance as in the merger (MixConditions).
    class CVesselUnit : public IFlowsheetUnit
    {
    public:
        const SVesselSettings& GetSettings() const { return m_settings; }

        // Uebernimmt die Einstellungen nur, wenn sie gueltig sind (Check()).
        bool SetSettings(const SVesselSettings& settings)
        {
            if (!settings.Check().empty())
                return false;
            m_settings = settings;
            return true;
        }

        // Stoff der vorgegebenen Anfangsfuellung (sonst leer): zaehlt zu den
        // aktiven Stoffen des Flowsheets.
        std::string GetFillMaterial() const
        {
            return m_settings.fill == EVesselFill::Prescribed ? m_settings.fillMaterial : std::string();
        }

        bool SupportsParticles() const override { return true; }

        void Initialize(const SSimulationGrid& grid, std::uint64_t unitSeed) override
        {
            m_random.Seed(MakeUnitSeed(unitSeed, "Behaelter"));
            m_lotRandom.Seed(MakeUnitSeed(unitSeed, "Behaelter-Losnummer"));
            m_dt = grid.GetDt();
            m_numS = grid.numSizeClasses;
            m_numC = grid.GetNumCompounds();
            m_numT = grid.numTimeSteps;
            m_sizes = grid.GetSizeGrid();
            m_sizeWeights = m_sizes.GetWeights();
            m_compounds = grid.compounds;
            m_materials = grid.materials;
            m_particles = grid.representation == ERepresentation::Particles;
            m_cardSettings = grid.cards;
            m_countMin = grid.GetCardCountMin();
            m_cap = StockCap(grid);
            SetupClasses();
            m_missingHeatCapacity.clear();
            m_mergedCards = 0;
            m_maxMergeRatio = 1.0;
            m_zeroSteadyInflow = false;
            m_fillCompoundMissing = false;
            m_initialMass = 0.0;
            m_placements.clear();
            m_merged.clear();

            SetupFractions();
            SetupRegions();

            // "wie im Dauerbetrieb" braucht den ersten Zulauf (Step 0).
            m_filled = m_settings.batch || m_settings.fill != EVesselFill::SteadyState;
            if (m_settings.fill == EVesselFill::Prescribed)
                FillPrescribed(grid);
            m_initialMass = GetContentMass();
            m_inside = GetContentCards();
        }

        void Step(const SStepContext& context, const std::vector<const CStreamSlice*>& inputs,
            std::vector<CStreamSlice>& outputs) override
        {
            if (outputs.empty())
                return;
            const CStreamSlice* input = inputs.empty() ? nullptr : inputs[0];
            CStreamSlice& out = outputs[0];
            m_outParts.clear();
            if (m_particles)
            {
                if (out.HasCards())
                    StepCards(context.stepIndex, input, out);
            }
            else
                StepContinuum(context.stepIndex, input, out);

            // Temperatur und Druck am Ausgang: Mischung dessen, was geht.
            // Geht nichts, gelten die Werte des Eingangs.
            const bool anything = std::any_of(m_outParts.begin(), m_outParts.end(), [](const SMixPart& part)
                { return std::any_of(part.compoundWeights.begin(), part.compoundWeights.end(), [](double w) { return w > 0.0; }); });
            if (!anything)
            {
                out.SetTemperature(input ? input->GetTemperature() : kRoomTemperature);
                out.SetPressure(input ? input->GetPressure() : kRoomPressure);
                return;
            }
            const SMixResult mix = MixConditions(m_outParts, m_compounds, m_materials);
            out.SetTemperature(mix.temperature);
            out.SetPressure(mix.pressure);
            m_missingHeatCapacity.insert(mix.missingHeatCapacity.begin(), mix.missingHeatCapacity.end());
        }

        std::vector<std::string> GetWarnings() const override
        {
            std::vector<std::string> warnings;
            for (const std::string& name : m_missingHeatCapacity)
                warnings.push_back("Keine Waermekapazitaet fuer " + name +
                    " - Temperatur im Behaelter nur nach Masse gewichtet statt aus der Energiebilanz.");
            if (m_settings.batch && m_settings.fill == EVesselFill::SteadyState)
                warnings.push_back("Charge ohne Ablauf: Eine Fuellung \"wie im Dauerbetrieb\" gibt es nicht, der Behaelter startet leer.");
            if (m_zeroSteadyInflow)
                warnings.push_back("Anfangsfuellung \"wie im Dauerbetrieb\": Bei t = 0 kommt kein Zulauf an, der Behaelter startet leer.");
            if (m_fillCompoundMissing)
                warnings.push_back("Der Stoff der Anfangsfuellung (" + m_settings.fillMaterial + ") ist kein aktiver Stoff - die Fuellung fehlt.");
            if (m_mergedCards > 0)
                warnings.push_back("Vorrat voll (hoechstens " + std::to_string(m_cap) + " Karten): Karten im Zulauf bis "
                    + FormatDecimal(m_maxMergeRatio, 1) + " : 1 zusammengelegt, der Ablauf traegt dadurch weniger Karten.");
            return warnings;
        }

        std::string DescribeForLog() const override
        {
            std::string text;
            const double tau = m_settings.residenceTime;
            if (m_settings.batch)
                text = "Charge ohne Ablauf";
            else
            {
                switch (m_mode)
                {
                case EVesselMixing::Plug:
                    text = "Pfropfenstroemung, tau = " + FormatNumber(tau, 6) + " s = " + FormatNumber(tau / std::max(m_dt, 1e-300), 6) + " Zeitschritte";
                    break;
                case EVesselMixing::Cascade:
                    text = "Kaskade aus " + std::to_string(m_settings.cells) + " Kesseln mit je " + FormatNumber(tau / static_cast<double>(m_settings.cells), 4) + " s (tau = "
                        + FormatNumber(tau, 6) + " s)";
                    break;
                case EVesselMixing::CoreFlow:
                    text = "Kernfluss: " + FormatNumber(100.0 * m_settings.coreShare, 4) + " % im Kern in " + FormatNumber(m_settings.coreTime * tau, 4) + " s, "
                        + FormatNumber(100.0 * (1.0 - m_settings.coreShare), 4) + " % in der Randzone mit im Mittel " + FormatNumber(m_settings.GetWallTime(), 4) + " s";
                    break;
                case EVesselMixing::Mixed:
                default:
                    text = "ideal durchmischt, tau = " + FormatNumber(tau, 6) + " s, je Zeitschritt gehen "
                        + FormatNumber(100.0 * VesselExitFraction(m_dt, tau), 4) + " % des Inhalts";
                    break;
                }
            }
            text += ", Anfangsfuellung " + std::string(GetVesselFillName(m_settings.fill));
            if (m_settings.fill == EVesselFill::Prescribed)
                text += " (" + FormatNumber(m_settings.fillMass, 6) + " kg " + m_settings.fillMaterial + ")";
            if (m_particles)
            {
                text += ", Vorrat bis " + std::to_string(m_cap) + " Karten";
                text += m_capWanted > static_cast<double>(m_cap) + 0.5
                    ? " (begrenzt; tau/dt + 2 Zulaufschritte braeuchten " + FormatNumber(m_capWanted, 4) + ")"
                    : " (tau/dt + 2 Zulaufschritte)";
            }
            return text;
        }

        // Prueft die Verteilung der vorgegebenen Anfangsfuellung gegen das Raster (wie am Inlet).
        std::vector<std::string> CheckSizeGrid(const SSizeGrid& sizes) const override
        {
            std::vector<std::string> notes;
            if (m_settings.fill != EVesselFill::Prescribed)
                return notes;
            const auto note = [&](double fraction, const char* where, double size)
            {
                if (fraction <= 1e-3)
                    return;
                const double percent = fraction * 100.0;
                char text[200];
                std::snprintf(text, sizeof(text), percent >= 10.0 ? "Anfangsfuellung: %.0f %% der Groessenverteilung liegen %s (%g %s)."
                    : "Anfangsfuellung: %.2g %% der Groessenverteilung liegen %s (%g %s).",
                    percent, where, size * kSizeDisplayFactor, kSizeDisplayUnit);
                notes.push_back(text);
            };
            note(m_settings.fillDistribution.FractionBelow(sizes.sizeMin), "unter d_min", sizes.sizeMin);
            note(m_settings.fillDistribution.FractionAbove(sizes.sizeMax), "ueber d_max", sizes.sizeMax);
            return notes;
        }

        // --- Fuer Tests und Anzeige ------------------------------------------

        // Inhalt [kg] je Stoff bzw. insgesamt.
        double GetContentMass(size_t compound) const
        {
            double sum = 0.0;
            for (const SRegion& region : m_regions)
            {
                const std::vector<double> weights = RegionWeights(region);
                if (compound < weights.size())
                    sum += weights[compound];
            }
            return sum * m_dt;
        }

        double GetContentMass() const
        {
            double sum = 0.0;
            for (size_t c = 0; c < m_numC; ++c)
                sum += GetContentMass(c);
            return sum;
        }

        // Inhalt nach der Anfangsfuellung [kg] ("wie im Dauerbetrieb": nach Schritt 0 gesetzt).
        double GetInitialContentMass() const { return m_initialMass; }

        // Karten im Vorrat (Partikelmodus).
        size_t GetContentCards() const
        {
            size_t cards = 0;
            for (const SRegion& region : m_regions)
                cards += region.store.Count();
            return cards;
        }

        size_t GetCardCap() const { return m_cap; }
        size_t GetMergedCards() const { return m_mergedCards; }

        // Upper limit of the stock in cards (default kDefaultStockLimit);
        // applies from the next Initialize. Lower values e.g. in tests.
        void SetStockLimit(size_t cards) { m_stockLimit = std::max<size_t>(cards, 1); }

        static constexpr size_t kDefaultStockLimit = 10000000;
        static constexpr double kStockClassesPerDecade = 100.0;

    private:
        // Karten eines Bereichs, geordnet nach Stoff und darin nach Groesse;
        // gleich grosse Karten werden vor jedem Durchgang neu ausgelost
        // (DrawLots, Methodenhandbuch 7.4).
        struct SCardBin
        {
            std::vector<double> sizes;
            std::vector<double> flows;            // [Karte * Stoffe + Stoff], Massenstrom wie im Strom
            std::vector<std::uint32_t> compounds; // vorherrschender Stoff (Ordnung)
            size_t Count() const { return sizes.size(); }
            void Clear()
            {
                sizes.clear();
                flows.clear();
                compounds.clear();
            }
        };

        // Cards of a region in compartments, one per compound and size class
        // (SetupClasses), unordered inside a compartment. Adding a card and
        // taking one out cost the same whatever the size of the stock, so a
        // step only touches the cards that move. weights follows every card
        // that comes or goes (for temperature and pressure); StoreWeights
        // sums exactly.
        struct SCardStore
        {
            std::vector<SCardBin> bins;  // [compound * classes + class]
            std::vector<double> weights; // mass flow per compound
            size_t count = 0;
            size_t Count() const { return count; }
        };

        // Ein Bereich des Behaelters: Bestand, Kessel, Randzone oder die
        // Karten, die in einem bestimmten Schritt austreten. Inhalt in der
        // Einheit eines Zulaufschritts (Massenstromdichte; mal dt = Masse).
        struct SRegion
        {
            std::vector<double> values; // Kontinuum: [Stoff * numS + Klasse]
            SCardStore store;           // particle mode
            double temperature = kRoomTemperature;
            double pressure = kRoomPressure;
        };

        static constexpr size_t kMaxMerge = 50; // hoechstens so viele Zulaufkarten zu einer

        // Room of the stock in particle mode: (tau/dt + 2) slices of at most
        // upperFactor x N cards each (2 N without slice limits), so that in
        // steady operation no card has to be merged; at least stockFactor x N,
        // at most the stock limit. For a batch the whole simulation counts
        // instead of tau.
        size_t StockCap(const SSimulationGrid& grid)
        {
            const double N = static_cast<double>(std::max<size_t>(grid.cards.cardsPerSlice, 1));
            const double perSlice = (grid.cards.sliceLimits ? std::max(grid.cards.upperFactor, 1.0) : 2.0) * N;
            const double dt = grid.GetDt();
            const double steps = m_settings.batch ? static_cast<double>(grid.numTimeSteps)
                : (dt > 0.0 ? m_settings.residenceTime / dt : 0.0);
            m_capWanted = (steps + 2.0) * perSlice;
            const double cap = std::min(static_cast<double>(m_stockLimit),
                std::max(std::max(m_settings.stockFactor, 1.0) * N, m_capWanted));
            return static_cast<size_t>(std::max(std::floor(cap + 1e-9), 1.0));
        }

        // Size classes of the compartments: kStockClassesPerDecade per decade
        // over the size grid, plus one below and one above it.
        void SetupClasses()
        {
            const double dMax = m_sizes.sizeMax > 0.0 ? m_sizes.sizeMax : 1.0;
            const double dMin = (m_sizes.sizeMin > 0.0 && m_sizes.sizeMin < dMax) ? m_sizes.sizeMin : dMax * 1e-6;
            m_logSizeMin = std::log10(dMin);
            m_numClasses = static_cast<size_t>(std::ceil(std::log10(dMax / dMin) * kStockClassesPerDecade)) + 2;
        }

        size_t ClassOf(double size) const
        {
            if (!(size > 0.0))
                return 0;
            const double x = (std::log10(size) - m_logSizeMin) * kStockClassesPerDecade;
            if (!(x >= 0.0))
                return 0;
            return std::min(static_cast<size_t>(x) + 1, m_numClasses - 1);
        }

        // ---------------- Anteile je Schritt ----------------

        static void SplitDelay(double steps, size_t& whole, double& rest)
        {
            const double x = std::max(steps, 0.0);
            whole = static_cast<size_t>(std::floor(x + 1e-9));
            rest = x - static_cast<double>(whole);
            if (rest < 1e-9)
                rest = 0.0;
        }

        void SetupFractions()
        {
            const double tau = m_settings.residenceTime;
            m_mode = m_settings.batch ? EVesselMixing::Mixed : m_settings.mixing;
            m_stockFractions.clear();
            m_inflowFractions.clear();
            m_cellFractions.clear();
            m_delaySteps = 0;
            m_delayRest = 0.0;
            if (m_settings.batch)
            {
                m_stockFractions = { 1.0, 0.0 };
                m_inflowFractions = { 1.0, 0.0 };
                return;
            }
            // Ideal durchmischt mit tau_m: bleibt / geht fuer Bestand und Zulauf.
            const auto mixed = [this](double tauMixed, double& stay, double& exit, double& inflowStay)
            {
                const double x = (m_dt > 0.0 && tauMixed > 0.0) ? m_dt / tauMixed : 0.0;
                exit = VesselExitFraction(m_dt, tauMixed);
                stay = std::exp(-x);
                inflowStay = x > 0.0 ? exit / x : 1.0;
            };
            switch (m_mode)
            {
            case EVesselMixing::Plug:
                SplitDelay(m_dt > 0.0 ? tau / m_dt : 0.0, m_delaySteps, m_delayRest);
                m_inflowFractions = { 1.0 - m_delayRest, m_delayRest };
                break;
            case EVesselMixing::Cascade:
            {
                const size_t n = std::max<size_t>(m_settings.cells, 1);
                const double lambda = m_dt > 0.0 ? static_cast<double>(n) * m_dt / tau : 0.0;
                std::vector<double> pmf(n, 0.0);
                for (size_t m = 0; m < n; ++m)
                    pmf[m] = lambda > 0.0 ? std::exp(-lambda + static_cast<double>(m) * std::log(lambda) - std::lgamma(static_cast<double>(m) + 1.0))
                                          : (m == 0 ? 1.0 : 0.0);
                m_cellFractions.assign(n, {});
                m_cellDraws.assign(n, {});
                for (size_t j = 0; j < n; ++j)
                {
                    // Ziele: m = 0 (bleibt), 1, ..., n - 1 - j Kessel weiter, dann Austritt.
                    std::vector<double>& f = m_cellFractions[j];
                    for (size_t m = 0; m + j < n; ++m)
                        f.push_back(pmf[m]);
                    f.push_back(PoissonTail(n - 1 - j, lambda));
                    // Particle mode: one draw "goes / stays" per target, first
                    // the exit, then 1, 2, ... cells on, each from what is
                    // still in the cell (share relative to the rest), so each
                    // target is right to one card.
                    double rest = 1.0;
                    const auto draw = [&](size_t t)
                    {
                        const double share = rest > 0.0 ? std::min(f[t] / rest, 1.0) : 0.0;
                        m_cellDraws[j].push_back({ t, share });
                        rest -= f[t];
                    };
                    draw(f.size() - 1);
                    for (size_t t = 1; t + 1 < f.size(); ++t)
                        draw(t);
                }
                double sum = 0.0;
                for (size_t m = 0; m < n; ++m)
                {
                    const double q = lambda > 0.0 ? PoissonTail(m, lambda) / lambda : (m == 0 ? 1.0 : 0.0);
                    m_inflowFractions.push_back(q);
                    sum += q;
                }
                m_inflowFractions.push_back(std::max(0.0, 1.0 - sum));
                break;
            }
            case EVesselMixing::CoreFlow:
            {
                const double phi = m_settings.coreShare;
                SplitDelay(m_dt > 0.0 ? m_settings.coreTime * tau / m_dt : 0.0, m_delaySteps, m_delayRest);
                double stay = 1.0, exit = 0.0, inflowStay = 1.0;
                mixed(m_settings.GetWallTime(), stay, exit, inflowStay);
                m_stockFractions = { stay, exit };
                // Zulauf: Kern frueh, Kern spaet, Randzone sofort wieder hinaus, Randzone bleibt.
                m_inflowFractions = { phi * (1.0 - m_delayRest), phi * m_delayRest,
                    (1.0 - phi) * (1.0 - inflowStay), (1.0 - phi) * inflowStay };
                break;
            }
            case EVesselMixing::Mixed:
            default:
            {
                double stay = 1.0, exit = 0.0, inflowStay = 1.0;
                mixed(tau, stay, exit, inflowStay);
                m_stockFractions = { stay, exit };
                m_inflowFractions = { inflowStay, 1.0 - inflowStay };
                break;
            }
            }
        }

        // Bereiche und Konten je Typ anlegen.
        void SetupRegions()
        {
            size_t regions = 1, sources = 2;
            m_ring = 0;
            switch (m_mode)
            {
            case EVesselMixing::Plug:
                m_ring = std::min(m_delaySteps + 2, m_numT + 2);
                regions = m_ring + 1; // Ring + "nach dem Ende"
                sources = 1;
                break;
            case EVesselMixing::Cascade:
                regions = m_cellFractions.size();
                sources = regions + 1;
                break;
            case EVesselMixing::CoreFlow:
                m_ring = std::min(m_delaySteps + 2, m_numT + 2);
                regions = m_ring + 2; // Ring + "nach dem Ende" + Randzone
                sources = 2;
                break;
            case EVesselMixing::Mixed:
            default:
                break;
            }
            m_regions.assign(regions, SRegion());
            for (SRegion& region : m_regions)
            {
                if (m_particles)
                    SetupStore(region.store);
                else
                    region.values.assign(m_numC * m_numS, 0.0);
            }
            // Puffer fuer neue Karten einmal in voller Zahl, damit Verweise
            // darauf innerhalb eines Schritts gueltig bleiben.
            m_incoming.assign(std::max<size_t>(3, 2 * m_cellFractions.size()), SCardBin());
            m_inside = 0;
            m_accounts.assign(sources, {});
            m_drawAccounts.clear();
            if (m_mode == EVesselMixing::Cascade)
            {
                for (size_t j = 0; j < m_cellFractions.size(); ++j)
                    m_accounts[j].assign(m_cellFractions[j].size(), 0.0);
                m_accounts.back().assign(m_inflowFractions.size(), 0.0);
                m_drawAccounts.resize(m_cellDraws.size());
                for (size_t j = 0; j < m_cellDraws.size(); ++j)
                    m_drawAccounts[j].assign(m_cellDraws[j].size(), std::vector<double>(2, 0.0));
            }
            else if (m_mode == EVesselMixing::Plug)
                m_accounts[0].assign(m_inflowFractions.size(), 0.0);
            else
            {
                m_accounts[0].assign(m_stockFractions.size(), 0.0);
                m_accounts[1].assign(m_inflowFractions.size(), 0.0);
            }
        }

        // Bereich, aus dem Material im Schritt e austritt (Ring); e ab dem Ende der Simulation: "nach dem Ende".
        size_t RingRegion(size_t e) const { return e < m_numT ? e % m_ring : m_ring; }
        size_t WallRegion() const { return m_ring + 1; }

        // Inhalt jedes Bereichs im Dauerbetrieb, in Zulaufschritten (Summe tau / dt).
        std::vector<std::pair<size_t, double>> SteadyWeights() const
        {
            std::vector<std::pair<size_t, double>> weights;
            const double tau = m_settings.residenceTime;
            const double steps = m_dt > 0.0 ? tau / m_dt : 0.0;
            const auto plugRing = [&](double scale)
            {
                double beyond = 0.0;
                for (size_t e = 0; e < m_delaySteps && e < m_numT; ++e)
                    weights.push_back({ RingRegion(e), scale });
                if (m_delaySteps > m_numT)
                    beyond += scale * static_cast<double>(m_delaySteps - m_numT);
                if (m_delayRest > 0.0)
                {
                    if (m_delaySteps < m_numT)
                        weights.push_back({ RingRegion(m_delaySteps), scale * m_delayRest });
                    else
                        beyond += scale * m_delayRest;
                }
                if (beyond > 0.0)
                    weights.push_back({ m_ring, beyond });
            };
            if (m_settings.batch)
                return { { 0, 1.0 } };
            switch (m_mode)
            {
            case EVesselMixing::Plug:
                plugRing(1.0);
                break;
            case EVesselMixing::Cascade:
                for (size_t j = 0; j < m_cellFractions.size(); ++j)
                    weights.push_back({ j, steps / static_cast<double>(m_cellFractions.size()) });
                break;
            case EVesselMixing::CoreFlow:
                plugRing(m_settings.coreShare);
                weights.push_back({ WallRegion(), (1.0 - m_settings.coreShare) * (m_dt > 0.0 ? m_settings.GetWallTime() / m_dt : 0.0) });
                break;
            case EVesselMixing::Mixed:
            default:
                weights.push_back({ 0, steps });
                break;
            }
            return weights;
        }

        // ---------------- Hilfen fuer Mengen und Temperatur ----------------

        // Stoffmengen eines Bereichs (Massenstrom-Aequivalent; mal dt = kg).
        std::vector<double> RegionWeights(const SRegion& region) const
        {
            std::vector<double> weights(m_numC, 0.0);
            if (!region.values.empty())
            {
                for (size_t c = 0; c < m_numC; ++c)
                    for (size_t s = 0; s < m_numS && s < m_sizeWeights.size(); ++s)
                        weights[c] += m_sizeWeights[s] * region.values[c * m_numS + s];
            }
            for (const SCardBin& bin : region.store.bins)
                AddBinWeights(bin, weights);
            return weights;
        }

        void AddBinWeights(const SCardBin& bin, std::vector<double>& weights) const
        {
            const size_t n = bin.Count();
            for (size_t k = 0; k < n; ++k)
                for (size_t c = 0; c < m_numC; ++c)
                    weights[c] += bin.flows[k * m_numC + c];
        }

        std::vector<double> BinWeights(const SCardBin& bin) const
        {
            std::vector<double> weights(m_numC, 0.0);
            AddBinWeights(bin, weights);
            return weights;
        }

        std::vector<double> SliceWeights(const CStreamSlice* slice) const
        {
            std::vector<double> weights(m_numC, 0.0);
            if (!slice)
                return weights;
            for (size_t c = 0; c < m_numC && c < slice->GetNumCompounds(); ++c)
                weights[c] = slice->GetCompoundWeight(c, m_sizeWeights);
            return weights;
        }

        static std::vector<double> Scaled(const std::vector<double>& weights, double factor)
        {
            std::vector<double> result(weights);
            for (double& w : result)
                w *= factor;
            return result;
        }

        static SMixPart MakePart(std::vector<double> weights, double temperature, double pressure)
        {
            SMixPart part;
            part.compoundWeights = std::move(weights);
            part.temperature = temperature;
            part.pressure = pressure;
            return part;
        }

        void AddOutPart(std::vector<double> weights, double temperature, double pressure)
        {
            m_outParts.push_back(MakePart(std::move(weights), temperature, pressure));
        }

        // Temperatur und Druck eines Bereichs nach dem Zumischen: was bleibt
        // (remaining, bei seiner bisherigen Temperatur) plus die neuen Teile.
        void MixRegion(SRegion& region, std::vector<double> remaining, const std::vector<SMixPart>& added)
        {
            std::vector<SMixPart> parts;
            parts.reserve(added.size() + 1);
            parts.push_back(MakePart(std::move(remaining), region.temperature, region.pressure));
            parts.insert(parts.end(), added.begin(), added.end());
            const bool anything = std::any_of(parts.begin(), parts.end(), [](const SMixPart& part)
                { return std::any_of(part.compoundWeights.begin(), part.compoundWeights.end(), [](double w) { return w > 0.0; }); });
            if (!anything)
                return; // kein Material: Temperatur und Druck bleiben Anzeigewerte
            const SMixResult mix = MixConditions(parts, m_compounds, m_materials);
            region.temperature = mix.temperature;
            region.pressure = mix.pressure;
            m_missingHeatCapacity.insert(mix.missingHeatCapacity.begin(), mix.missingHeatCapacity.end());
        }

        // ---------------- Anfangsfuellung ----------------

        // Vorgegebene Fuellung: Masse fillMass mit der eingestellten
        // Verteilung, verteilt auf die Bereiche wie im Dauerbetrieb (bei der
        // Charge alles in einem Bereich).
        void FillPrescribed(const SSimulationGrid& grid)
        {
            const auto it = std::find(m_compounds.begin(), m_compounds.end(), m_settings.fillMaterial);
            if (it == m_compounds.end())
            {
                m_fillCompoundMissing = true;
                return;
            }
            const size_t compound = static_cast<size_t>(it - m_compounds.begin());
            const std::vector<std::pair<size_t, double>> weights = SteadyWeights();
            double total = 0.0;
            for (const auto& entry : weights)
                total += entry.second;
            if (!(total > 0.0) || !(m_dt > 0.0) || !(m_settings.fillMass > 0.0))
                return;
            // Massenstrom eines "Zulaufschritts" der Fuellung: Summe ueber die Bereiche = Masse / dt.
            const double unitFlow = m_settings.fillMass / (m_dt * total);
            const double T = m_settings.fillTemperature, p = m_settings.fillPressure;
            if (!m_particles)
            {
                const std::vector<double> masses = m_settings.fillDistribution.MassesAtNodes(grid.GetSizeGrid());
                for (const auto& entry : weights)
                {
                    SRegion& region = m_regions[entry.first];
                    for (size_t s = 0; s < m_numS && s < masses.size(); ++s)
                        if (m_sizes.GetWeight(s) > 0.0)
                            region.values[compound * m_numS + s] += entry.second * unitFlow * masses[s] / m_sizes.GetWeight(s);
                    region.temperature = T;
                    region.pressure = p;
                }
                return;
            }
            const size_t N = std::max<size_t>(m_cardSettings.cardsPerSlice, 1);
            const double budget = std::min(total * static_cast<double>(N), static_cast<double>(m_cap));
            for (const auto& entry : weights)
            {
                const size_t want = std::max<size_t>(1, static_cast<size_t>(std::llround(budget * entry.second / total)));
                const SCardPlacement& placement = PlacementFor(want);
                SCardBin part;
                for (size_t k = 0; k < placement.sizes.size(); ++k)
                {
                    part.sizes.push_back(placement.sizes[k]);
                    part.compounds.push_back(static_cast<std::uint32_t>(compound));
                    for (size_t c = 0; c < m_numC; ++c)
                        part.flows.push_back(c == compound ? entry.second * unitFlow * placement.massFractions[k] : 0.0);
                }
                SRegion& region = m_regions[entry.first];
                StoreAddAll(region.store, part);
                region.temperature = T;
                region.pressure = p;
            }
        }

        // Karten der vorgegebenen Verteilung an den Quantilen (wie am Inlet), je Kartenzahl einmal.
        const SCardPlacement& PlacementFor(size_t numCards)
        {
            for (const auto& entry : m_placements)
                if (entry.first == numCards)
                    return entry.second;
            SCardSettings settings = m_cardSettings;
            settings.cardsPerSlice = numCards;
            m_placements.emplace_back(numCards, PlaceCardsAtQuantiles(m_settings.fillDistribution, settings, m_countMin));
            return m_placements.back().second;
        }

        // "Wie im Dauerbetrieb" aus dem ersten Zulauf: jeder Bereich erhaelt
        // seinen Inhalt im Dauerbetrieb (SteadyWeights) als Vielfaches eines
        // Zulaufschritts.
        void FillSteadyContinuum(const CStreamSlice& input)
        {
            for (const auto& entry : SteadyWeights())
            {
                SRegion& region = m_regions[entry.first];
                for (size_t c = 0; c < m_numC && c < input.GetNumCompounds(); ++c)
                    for (size_t s = 0; s < m_numS && s < input.GetNumSizeClasses(); ++s)
                        region.values[c * m_numS + s] += entry.second * input.GetValue(c, s);
                region.temperature = input.GetTemperature();
                region.pressure = input.GetPressure();
            }
        }

        // Im Partikelmodus: Kopien der Zulaufkarten (wie Teilen in gleiche
        // Stuecke) oder, wenn ein Bereich weniger Karten bekommt als ein
        // Zulaufschritt hat, die zusammengelegte Vorlage. Insgesamt hoechstens
        // so viele Karten, wie der Vorrat fasst.
        void FillSteadyCards(const SCardBin& inflow, double temperature, double pressure)
        {
            const std::vector<std::pair<size_t, double>> weights = SteadyWeights();
            double total = 0.0;
            for (const auto& entry : weights)
                total += entry.second;
            const size_t nt = inflow.Count();
            if (!(total > 0.0) || nt == 0)
                return;
            const double budget = std::min(total * static_cast<double>(nt), static_cast<double>(m_cap));
            for (const auto& entry : weights)
            {
                const double cards = budget * entry.second / total;
                SCardBin part;
                if (cards >= static_cast<double>(nt))
                {
                    const size_t copies = std::max<size_t>(1, static_cast<size_t>(std::llround(cards / static_cast<double>(nt))));
                    const double scale = entry.second / static_cast<double>(copies);
                    for (size_t copy = 0; copy < copies; ++copy)
                        for (size_t k = 0; k < nt; ++k)
                            PushCard(part, inflow, k, scale);
                }
                else
                {
                    const SCardBin& merged = MergedTemplate(inflow, std::max<size_t>(1, static_cast<size_t>(std::llround(cards))));
                    for (size_t k = 0; k < merged.Count(); ++k)
                        PushCard(part, merged, k, entry.second);
                }
                SRegion& region = m_regions[entry.first];
                StoreAddAll(region.store, part);
                region.temperature = temperature;
                region.pressure = pressure;
            }
        }

        // Vorlage auf numCards Karten zusammengelegt, je Kartenzahl einmal.
        const SCardBin& MergedTemplate(const SCardBin& inflow, size_t numCards)
        {
            for (const auto& entry : m_merged)
                if (entry.first == numCards)
                    return entry.second;
            SCardBin merged = inflow;
            MergeBin(merged, numCards);
            m_merged.emplace_back(numCards, std::move(merged));
            return m_merged.back().second;
        }

        // ---------------- Karten ----------------

        static bool CardBefore(std::uint32_t compoundA, double sizeA, std::uint32_t compoundB, double sizeB)
        {
            return compoundA != compoundB ? compoundA < compoundB : sizeA < sizeB;
        }

        // Losnummer (Methodenhandbuch 7.4): Vor jedem Durchgang des Kontos
        // werden Karten gleichen Stoffs und gleicher Groesse neu gemischt
        // (Fisher-Yates, eigener Zufallsstrom; die Startstellen bleiben davon
        // unberuehrt). Ohne das stuenden sie in der Reihenfolge ihres
        // Eintritts, das Abzaehlen traefe in allen Groessenstellen fast
        // dieselbe Altersstufe, und bei verschieden schweren Karten gleicher
        // Groesse (z.B. aus zwei Inlets) gingen bei 50 % alle schweren in
        // dasselbe Ziel. Eine einmal gezogene, feste Losnummer je Karte reicht
        // nicht: Alle Groessenstellen teilen dieselbe Folge der Startstellen,
        // und Rangstellen, die eine Weile selten gezogen wurden, sammeln dann
        // in allen Groessen alte Karten.
        void DrawLots(SCardBin& bin)
        {
            const size_t n = bin.Count();
            for (size_t first = 0; first < n;)
            {
                size_t last = first + 1;
                while (last < n && bin.compounds[last] == bin.compounds[first] && bin.sizes[last] == bin.sizes[first])
                    ++last;
                for (size_t i = last - 1; i > first; --i)
                {
                    const size_t span = i - first + 1;
                    const size_t j = first + std::min(static_cast<size_t>(m_lotRandom.Uniform() * static_cast<double>(span)), span - 1);
                    if (j != i)
                        std::swap_ranges(bin.flows.begin() + static_cast<std::ptrdiff_t>(i * m_numC),
                            bin.flows.begin() + static_cast<std::ptrdiff_t>((i + 1) * m_numC),
                            bin.flows.begin() + static_cast<std::ptrdiff_t>(j * m_numC));
                }
                first = last;
            }
        }

        void PushCard(SCardBin& to, const SCardBin& from, size_t k, double scale = 1.0) const
        {
            to.sizes.push_back(from.sizes[k]);
            to.compounds.push_back(from.compounds[k]);
            for (size_t c = 0; c < m_numC; ++c)
                to.flows.push_back(from.flows[k * m_numC + c] * scale);
        }

        std::uint32_t DominantCompound(const double* flows) const
        {
            size_t best = 0;
            for (size_t c = 1; c < m_numC; ++c)
                if (flows[c] > flows[best])
                    best = c;
            return static_cast<std::uint32_t>(best);
        }

        void CardsFromSlice(const CStreamSlice& slice, SCardBin& bin) const
        {
            bin.Clear();
            const size_t n = slice.GetNumCards();
            bin.sizes.reserve(n);
            bin.compounds.reserve(n);
            bin.flows.reserve(n * m_numC);
            for (size_t k = 0; k < n; ++k)
            {
                bin.sizes.push_back(slice.GetCardSize(k));
                for (size_t c = 0; c < m_numC; ++c)
                    bin.flows.push_back(c < slice.GetNumCompounds() ? slice.GetCardMassFlow(k, c) : 0.0);
                bin.compounds.push_back(DominantCompound(&bin.flows[k * m_numC]));
            }
        }

        // Nach Stoff und Groesse ordnen (stabil).
        void SortBin(SCardBin& bin)
        {
            const size_t n = bin.Count();
            bool sorted = true;
            for (size_t k = 1; k < n && sorted; ++k)
                if (CardBefore(bin.compounds[k], bin.sizes[k], bin.compounds[k - 1], bin.sizes[k - 1]))
                    sorted = false;
            if (sorted)
                return;
            m_order.resize(n);
            std::iota(m_order.begin(), m_order.end(), size_t(0));
            std::stable_sort(m_order.begin(), m_order.end(), [&bin](size_t a, size_t b)
                { return CardBefore(bin.compounds[a], bin.sizes[a], bin.compounds[b], bin.sizes[b]); });
            SCardBin result;
            result.sizes.reserve(n);
            result.compounds.reserve(n);
            result.flows.reserve(n * m_numC);
            for (size_t k : m_order)
                PushCard(result, bin, k);
            bin = std::move(result);
        }

        // ---------------- Stock in compartments ----------------

        void SetupStore(SCardStore& store) const
        {
            store.bins.assign(m_numC * m_numClasses, SCardBin());
            store.weights.assign(m_numC, 0.0);
            store.count = 0;
        }

        void StoreAdd(SCardStore& store, const SCardBin& from, size_t k, double scale = 1.0) const
        {
            const size_t b = static_cast<size_t>(from.compounds[k]) * m_numClasses + ClassOf(from.sizes[k]);
            PushCard(store.bins[b], from, k, scale);
            for (size_t c = 0; c < m_numC; ++c)
                store.weights[c] += from.flows[k * m_numC + c] * scale;
            ++store.count;
        }

        void StoreAddAll(SCardStore& store, const SCardBin& from) const
        {
            for (size_t k = 0; k < from.Count(); ++k)
                StoreAdd(store, from, k);
        }

        // All cards to "to" (in the order of the compartments); empty afterwards.
        void StoreTakeAll(SCardStore& store, SCardBin& to) const
        {
            for (SCardBin& bin : store.bins)
            {
                for (size_t i = 0; i < bin.Count(); ++i)
                    PushCard(to, bin, i);
                bin.Clear();
            }
            std::fill(store.weights.begin(), store.weights.end(), 0.0);
            store.count = 0;
        }

        // Card i of compartment b to "to" (its mass flows added to
        // toWeights); the last card of the compartment fills the gap.
        void StoreTake(SCardStore& store, size_t b, size_t i, SCardBin& to, std::vector<double>& toWeights) const
        {
            SCardBin& bin = store.bins[b];
            PushCard(to, bin, i);
            for (size_t c = 0; c < m_numC; ++c)
            {
                const double w = bin.flows[i * m_numC + c];
                toWeights[c] += w;
                store.weights[c] -= w;
            }
            const size_t last = bin.Count() - 1;
            if (i != last)
            {
                bin.sizes[i] = bin.sizes[last];
                bin.compounds[i] = bin.compounds[last];
                std::copy(bin.flows.begin() + static_cast<std::ptrdiff_t>(last * m_numC),
                    bin.flows.begin() + static_cast<std::ptrdiff_t>((last + 1) * m_numC),
                    bin.flows.begin() + static_cast<std::ptrdiff_t>(i * m_numC));
            }
            bin.sizes.pop_back();
            bin.compounds.pop_back();
            bin.flows.resize(last * m_numC);
            if (--store.count == 0)
                std::fill(store.weights.begin(), store.weights.end(), 0.0); // no rounding remainder
        }

        // Running weights without small negative remainders of rounding.
        std::vector<double> StoreKeptWeights(const SCardStore& store) const
        {
            std::vector<double> weights(store.weights);
            for (double& w : weights)
                w = std::max(w, 0.0);
            return weights;
        }

        // Account method with the accounts "stays" and "goes" (fraction f of
        // the stock leaves), done in one jump instead of card by card. Walking
        // the n cards once from a random start, a card goes when, after its
        // payment, "goes" exceeds "stays"; with g = goes - 1/2 (both accounts
        // add up to 0) the k-th card goes at walk position
        //   j_k = floor((k - 1 - g) / f) + 1,   k = 1, 2, ... while j_k <= n,
        // and afterwards g = g + n f - (number gone), as after the walk card by
        // card. The walk runs over the compartments in their order (compound,
        // size), so the positions give how many cards leave each compartment;
        // which cards of a compartment leave is drawn at random (lot), so that
        // leaving does not depend on age or weight.
        void StoreDraw(SCardStore& store, double f, std::vector<double>& accounts, SCardBin& to, std::vector<double>& toWeights)
        {
            const size_t n = store.Count();
            if (n == 0 || !(f > 0.0) || accounts.size() < 2)
                return;
            const double g0 = accounts[1] - 0.5;
            const size_t start = std::min(static_cast<size_t>(m_random.Uniform() * static_cast<double>(n)), n - 1);
            const size_t numBins = store.bins.size();
            m_binStart.resize(numBins + 1);
            m_binStart[0] = 0;
            for (size_t b = 0; b < numBins; ++b)
                m_binStart[b + 1] = m_binStart[b] + store.bins[b].Count();
            m_leaveCount.assign(numBins, 0);
            size_t gone = 0;
            for (size_t k = 1;; ++k)
            {
                const double x = (static_cast<double>(k) - 1.0 - g0) / f;
                if (!(x < static_cast<double>(n)))
                    break;
                const size_t j = static_cast<size_t>(std::floor(x)) + 1; // walk position 1 .. n
                const size_t position = (start + j - 1) % n;
                const size_t b = static_cast<size_t>(std::upper_bound(m_binStart.begin(), m_binStart.end(), position) - m_binStart.begin()) - 1;
                ++m_leaveCount[b];
                ++gone;
            }
            const double g = g0 + static_cast<double>(n) * f - static_cast<double>(gone);
            accounts[1] = g + 0.5;
            accounts[0] = -accounts[1];
            for (size_t b = 0; b < numBins; ++b)
                for (size_t r = 0; r < m_leaveCount[b]; ++r)
                {
                    const size_t size = store.bins[b].Count();
                    const size_t i = std::min(static_cast<size_t>(m_lotRandom.Uniform() * static_cast<double>(size)), size - 1);
                    StoreTake(store, b, i, to, toWeights);
                }
        }

        // Konto-Verfahren mit ganzen Karten: Jede Karte zahlt den Anteil
        // fractions[i] auf das Konto jedes Ziels i ein und geht zum hoechsten
        // Kontostand (bei Gleichstand das erste Ziel); dieses Konto wird um 1
        // belastet. Die Karten werden in ihrer Ordnung (Stoff, Groesse, gleich
        // grosse neu ausgelost) ab einer zufaelligen Startposition einmal
        // ringsum durchgegangen; die Konten laufen ueber die Schritte weiter,
        // ihre Summe bleibt 0.
        void Walk(SCardBin& bin, const std::vector<double>& fractions, std::vector<double>& accounts,
            std::vector<std::uint16_t>& target)
        {
            DrawLots(bin);
            const size_t numCards = bin.Count();
            target.assign(numCards, 0);
            if (numCards == 0 || fractions.empty())
                return;
            const size_t numTargets = std::min(fractions.size(), accounts.size());
            const size_t start = std::min(static_cast<size_t>(m_random.Uniform() * static_cast<double>(numCards)), numCards - 1);
            for (size_t j = 0; j < numCards; ++j)
            {
                const size_t card = start + j < numCards ? start + j : start + j - numCards;
                size_t best = 0;
                for (size_t i = 0; i < numTargets; ++i)
                {
                    accounts[i] += fractions[i];
                    if (accounts[i] > accounts[best])
                        best = i;
                }
                accounts[best] -= 1.0;
                target[card] = static_cast<std::uint16_t>(best);
            }
        }

        // Karten eines Bereichs auf hoechstens numCards zusammenlegen wie bei
        // den Grenzen je Stromscheibe (benachbarte Karten desselben Stoffs,
        // die leichtesten Paare zuerst; Masse und Partikelzahl bleiben).
        // Liefert die Zahl der weggefallenen Karten.
        size_t MergeBin(SCardBin& bin, size_t numCards)
        {
            if (bin.Count() <= numCards || bin.Count() < 2)
                return 0;
            CStreamSlice slice;
            slice.Resize(0, m_numC, ERepresentation::Particles);
            slice.SetCards(bin.sizes, bin.flows);
            SCardSettings settings = m_cardSettings;
            settings.cardsPerSlice = std::max<size_t>(numCards, 1);
            settings.upperFactor = 1.0;
            settings.splitFactor = 1e12; // Paare ohne Obergrenze, kein Teilen
            const SLimitCounts counts = ApplySliceLimits(slice, settings, m_countMin);
            bin.Clear();
            for (size_t k = 0; k < slice.GetNumCards(); ++k)
            {
                bin.sizes.push_back(slice.GetCardSize(k));
                for (size_t c = 0; c < m_numC; ++c)
                    bin.flows.push_back(slice.GetCardMassFlow(k, c));
                bin.compounds.push_back(DominantCompound(&bin.flows[k * m_numC]));
            }
            SortBin(bin);
            return counts.removed;
        }

        // Haelt den Vorrat bei hoechstens m_cap Karten: Passen die neu
        // hinzukommenden Karten nicht mehr hinein, wird jede Gruppe im
        // selben Verhaeltnis zusammengelegt (hoechstens kMaxMerge : 1).
        void LimitIncoming(const std::vector<SCardBin*>& groups, size_t stockCards)
        {
            size_t incoming = 0;
            for (const SCardBin* group : groups)
                incoming += group->Count();
            if (incoming == 0 || stockCards + incoming <= m_cap)
                return;
            const size_t room = m_cap > stockCards ? m_cap - stockCards : 0;
            const size_t allowed = std::max(room, (incoming + kMaxMerge - 1) / kMaxMerge);
            if (allowed >= incoming)
                return;
            const double ratio = static_cast<double>(allowed) / static_cast<double>(incoming);
            for (SCardBin* group : groups)
            {
                const size_t before = group->Count();
                if (before < 2)
                    continue;
                const size_t target = std::max<size_t>(1, static_cast<size_t>(std::ceil(static_cast<double>(before) * ratio - 1e-9)));
                if (target >= before)
                    continue;
                m_mergedCards += MergeBin(*group, target);
                if (group->Count() > 0)
                    m_maxMergeRatio = std::max(m_maxMergeRatio, static_cast<double>(before) / static_cast<double>(group->Count()));
            }
        }

        // ---------------- Schritt im Partikelmodus ----------------

        void StepCards(size_t k, const CStreamSlice* input, CStreamSlice& out)
        {
            SCardBin& inflow = m_inflow;
            inflow.Clear();
            double inT = kRoomTemperature, inP = kRoomPressure;
            if (input && input->HasCards())
            {
                CardsFromSlice(*input, inflow);
                SortBin(inflow);
                inT = input->GetTemperature();
                inP = input->GetPressure();
            }
            if (!m_filled)
            {
                m_filled = true;
                if (inflow.Count() == 0 || !(input && input->GetCardsMassFlow() > 0.0))
                    m_zeroSteadyInflow = true;
                else
                    FillSteadyCards(inflow, inT, inP);
                m_initialMass = GetContentMass();
                m_inside = GetContentCards();
            }
            m_outCards.Clear();

            switch (m_mode)
            {
            case EVesselMixing::Plug:
            {
                Walk(inflow, m_inflowFractions, m_accounts[0], m_targetIn);
                SCardBin& early = Incoming(0);
                SCardBin& late = Incoming(1);
                for (size_t i = 0; i < inflow.Count(); ++i)
                    PushCard(m_targetIn[i] == 0 ? early : late, inflow, i);
                LimitIncoming({ &early, &late }, m_inside - std::min(m_inside, DueCards(k)));
                AddToRing(k + m_delaySteps, early, inT, inP);
                AddToRing(k + m_delaySteps + 1, late, inT, inP);
                m_inside += early.Count() + late.Count();
                m_inside -= std::min(m_inside, DueCards(k));
                ReleaseRing(k);
                break;
            }
            case EVesselMixing::Cascade:
                StepCascadeCards(inflow, inT, inP);
                break;
            case EVesselMixing::CoreFlow:
            {
                SRegion& wall = m_regions[WallRegion()];
                std::vector<double> outW(m_numC, 0.0);
                const size_t wallBefore = wall.store.Count();
                StoreDraw(wall.store, m_stockFractions[1], m_accounts[0], m_outCards, outW);
                const size_t wallLeft = wallBefore - wall.store.Count(); // gehen in diesem Schritt
                const std::vector<double> keptW = StoreKeptWeights(wall.store);
                AddOutPart(outW, wall.temperature, wall.pressure);

                Walk(inflow, m_inflowFractions, m_accounts[1], m_targetIn);
                SCardBin& early = Incoming(0);
                SCardBin& late = Incoming(1);
                SCardBin& stay = Incoming(2);
                std::vector<double> inOutW(m_numC, 0.0);
                for (size_t i = 0; i < inflow.Count(); ++i)
                {
                    switch (m_targetIn[i])
                    {
                    case 0: PushCard(early, inflow, i); break;
                    case 1: PushCard(late, inflow, i); break;
                    case 2:
                        PushCard(m_outCards, inflow, i);
                        for (size_t c = 0; c < m_numC; ++c)
                            inOutW[c] += inflow.flows[i * m_numC + c];
                        break;
                    default: PushCard(stay, inflow, i); break;
                    }
                }
                AddOutPart(inOutW, inT, inP);
                const size_t remaining = m_inside - std::min(m_inside, wallLeft + DueCards(k));
                LimitIncoming({ &early, &late, &stay }, remaining);
                AddToRing(k + m_delaySteps, early, inT, inP);
                AddToRing(k + m_delaySteps + 1, late, inT, inP);
                MixRegion(wall, keptW, { MakePart(BinWeights(stay), inT, inP) });
                StoreAddAll(wall.store, stay);
                m_inside = m_inside - std::min(m_inside, wallLeft) + early.Count() + late.Count() + stay.Count();
                m_inside -= std::min(m_inside, DueCards(k));
                ReleaseRing(k);
                break;
            }
            case EVesselMixing::Mixed:
            default:
            {
                SRegion& stock = m_regions[0];
                std::vector<double> outW(m_numC, 0.0);
                StoreDraw(stock.store, m_stockFractions[1], m_accounts[0], m_outCards, outW);
                const std::vector<double> keptW = StoreKeptWeights(stock.store);
                AddOutPart(outW, stock.temperature, stock.pressure);

                Walk(inflow, m_inflowFractions, m_accounts[1], m_targetIn);
                SCardBin& stay = Incoming(0);
                std::vector<double> inOutW(m_numC, 0.0);
                for (size_t i = 0; i < inflow.Count(); ++i)
                {
                    if (m_targetIn[i] == 1)
                    {
                        PushCard(m_outCards, inflow, i);
                        for (size_t c = 0; c < m_numC; ++c)
                            inOutW[c] += inflow.flows[i * m_numC + c];
                    }
                    else
                        PushCard(stay, inflow, i);
                }
                AddOutPart(inOutW, inT, inP);
                const size_t kept = stock.store.Count();
                LimitIncoming({ &stay }, kept);
                MixRegion(stock, keptW, { MakePart(BinWeights(stay), inT, inP) });
                StoreAddAll(stock.store, stay);
                m_inside = stock.store.Count();
                break;
            }
            }

            out.SetCards(std::move(m_outCards.sizes), std::move(m_outCards.flows));
            m_outCards.Clear();
        }

        // Puffer fuer neu hinzukommende Karten (geleert). Die Zahl der Puffer
        // steht seit SetupRegions fest, Verweise bleiben gueltig.
        SCardBin& Incoming(size_t index)
        {
            SCardBin& bin = m_incoming[std::min(index, m_incoming.size() - 1)];
            bin.Clear();
            return bin;
        }

        // Karten, die im Schritt k aus dem Ring gehen.
        size_t DueCards(size_t k) const
        {
            return (k < m_numT && m_ring > 0) ? m_regions[RingRegion(k)].store.Count() : 0;
        }

        // Karten, die im Schritt e austreten, in den Ring legen.
        void AddToRing(size_t e, const SCardBin& cards, double temperature, double pressure)
        {
            if (cards.Count() == 0)
                return;
            SRegion& region = m_regions[RingRegion(e)];
            MixRegion(region, StoreKeptWeights(region.store), { MakePart(BinWeights(cards), temperature, pressure) });
            StoreAddAll(region.store, cards);
        }

        // Was im Schritt k faellig ist, geht ganz in den Ablauf.
        void ReleaseRing(size_t k)
        {
            if (k >= m_numT || m_ring == 0)
                return;
            SRegion& due = m_regions[RingRegion(k)];
            if (due.store.Count() == 0)
                return;
            AddOutPart(StoreKeptWeights(due.store), due.temperature, due.pressure);
            StoreTakeAll(due.store, m_outCards);
            due.temperature = kRoomTemperature;
            due.pressure = kRoomPressure;
        }

        void StepCascadeCards(SCardBin& inflow, double inT, double inP)
        {
            const size_t n = m_cellFractions.size();
            // Ziele je Kessel: 0 = bleibt, t = t Kessel weiter, n - j = Austritt.
            std::vector<std::vector<double>> moveW(n);
            for (size_t j = 0; j < n; ++j)
                moveW[j].assign((n - j + 1) * m_numC, 0.0);
            Walk(inflow, m_inflowFractions, m_accounts[n], m_targetIn);

            // Bewegte Karten je Zielkessel sammeln: Index 0 .. n-1 aus den
            // Kesseln, n .. 2n-1 frisch aus dem Zulauf (nur diese zusammenlegen).
            // Each cell draws one target after the other (m_cellDraws: exit
            // first, then 1, 2, ... cells on) straight from its stock. Moved
            // cards are collected first and only enter their cell at the end,
            // so every cell draws from its content of the previous step.
            for (size_t j = 0; j < 2 * n; ++j)
                Incoming(j);
            size_t stock = 0;
            for (size_t j = 0; j < n; ++j)
            {
                SCardStore& cell = m_regions[j].store;
                const size_t exitIndex = n - j;
                for (size_t d = 0; d < m_cellDraws[j].size(); ++d)
                {
                    const size_t t = m_cellDraws[j][d].first;
                    SCardBin& to = t >= exitIndex ? m_outCards : m_incoming[j + t];
                    const size_t before = to.Count();
                    std::vector<double> drawnW(m_numC, 0.0);
                    StoreDraw(cell, m_cellDraws[j][d].second, m_drawAccounts[j][d], to, drawnW);
                    for (size_t c = 0; c < m_numC; ++c)
                        moveW[j][t * m_numC + c] += drawnW[c];
                    if (t < exitIndex)
                        stock += to.Count() - before;
                }
                stock += cell.Count(); // bleibt im Behaelter
                const std::vector<double> stayW = StoreKeptWeights(cell);
                std::copy(stayW.begin(), stayW.end(), moveW[j].begin());
                const size_t exitOffset = exitIndex * m_numC;
                AddOutPart(std::vector<double>(moveW[j].begin() + static_cast<std::ptrdiff_t>(exitOffset),
                    moveW[j].begin() + static_cast<std::ptrdiff_t>(exitOffset + m_numC)), m_regions[j].temperature, m_regions[j].pressure);
            }
            std::vector<double> inOutW(m_numC, 0.0);
            for (size_t i = 0; i < inflow.Count(); ++i)
            {
                const size_t t = m_targetIn[i];
                if (t >= n)
                {
                    PushCard(m_outCards, inflow, i);
                    for (size_t c = 0; c < m_numC; ++c)
                        inOutW[c] += inflow.flows[i * m_numC + c];
                }
                else
                    PushCard(m_incoming[n + t], inflow, i);
            }
            AddOutPart(inOutW, inT, inP);

            std::vector<SCardBin*> fresh;
            for (size_t j = 0; j < n; ++j)
                fresh.push_back(&m_incoming[n + j]);
            LimitIncoming(fresh, stock);
            m_inside = stock;
            for (const SCardBin* group : fresh)
                m_inside += group->Count();

            // Neue Temperaturen aus den alten Kesseln, dann der neue Inhalt
            // (vom letzten Kessel zum ersten, damit die Quellen noch alt sind).
            std::vector<double> newT(n), newP(n);
            for (size_t target = 0; target < n; ++target)
            {
                std::vector<SMixPart> parts;
                for (size_t j = 0; j <= target; ++j)
                {
                    const size_t t = target - j;
                    parts.push_back(MakePart(std::vector<double>(moveW[j].begin() + static_cast<std::ptrdiff_t>(t * m_numC),
                        moveW[j].begin() + static_cast<std::ptrdiff_t>((t + 1) * m_numC)), m_regions[j].temperature, m_regions[j].pressure));
                }
                parts.push_back(MakePart(BinWeights(m_incoming[n + target]), inT, inP));
                SRegion probe;
                probe.temperature = m_regions[target].temperature;
                probe.pressure = m_regions[target].pressure;
                MixRegion(probe, std::vector<double>(m_numC, 0.0), parts);
                newT[target] = probe.temperature;
                newP[target] = probe.pressure;
            }
            for (size_t target = 0; target < n; ++target)
            {
                StoreAddAll(m_regions[target].store, m_incoming[target]);
                StoreAddAll(m_regions[target].store, m_incoming[n + target]);
                m_regions[target].temperature = newT[target];
                m_regions[target].pressure = newP[target];
            }
        }

        // ---------------- Schritt im Kontinuum ----------------

        void StepContinuum(size_t k, const CStreamSlice* input, CStreamSlice& out)
        {
            const size_t size = m_numC * m_numS;
            m_u.assign(size, 0.0);
            double inT = kRoomTemperature, inP = kRoomPressure;
            if (input && !input->HasCards())
            {
                for (size_t c = 0; c < m_numC && c < input->GetNumCompounds(); ++c)
                    for (size_t s = 0; s < m_numS && s < input->GetNumSizeClasses(); ++s)
                        m_u[c * m_numS + s] = input->GetValue(c, s);
                inT = input->GetTemperature();
                inP = input->GetPressure();
            }
            const std::vector<double> inW = SliceWeights(input && !input->HasCards() ? input : nullptr);
            if (!m_filled)
            {
                m_filled = true;
                if (!input || !std::any_of(inW.begin(), inW.end(), [](double w) { return w > 0.0; }))
                    m_zeroSteadyInflow = true;
                else
                    FillSteadyContinuum(*input);
                m_initialMass = GetContentMass();
            }
            m_outValues.assign(size, 0.0);

            switch (m_mode)
            {
            case EVesselMixing::Plug:
                AddToRingValues(k + m_delaySteps, m_inflowFractions[0], inW, inT, inP);
                AddToRingValues(k + m_delaySteps + 1, m_inflowFractions[1], inW, inT, inP);
                ReleaseRingValues(k);
                break;
            case EVesselMixing::Cascade:
                StepCascadeContinuum(inW, inT, inP);
                break;
            case EVesselMixing::CoreFlow:
            {
                SRegion& wall = m_regions[WallRegion()];
                const double stay = m_stockFractions[0], exit = m_stockFractions[1];
                const double now = m_inflowFractions[2], remain = m_inflowFractions[3];
                const std::vector<double> wallW = RegionWeights(wall);
                AddOutPart(Scaled(wallW, exit), wall.temperature, wall.pressure);
                AddOutPart(Scaled(inW, now), inT, inP);
                MixRegion(wall, Scaled(wallW, stay), { MakePart(Scaled(inW, remain), inT, inP) });
                for (size_t i = 0; i < size; ++i)
                {
                    m_outValues[i] += exit * wall.values[i] + now * m_u[i];
                    wall.values[i] = stay * wall.values[i] + remain * m_u[i];
                }
                AddToRingValues(k + m_delaySteps, m_inflowFractions[0], inW, inT, inP);
                AddToRingValues(k + m_delaySteps + 1, m_inflowFractions[1], inW, inT, inP);
                ReleaseRingValues(k);
                break;
            }
            case EVesselMixing::Mixed:
            default:
            {
                SRegion& stock = m_regions[0];
                const double stay = m_stockFractions[0], exit = m_stockFractions[1];
                const double remain = m_inflowFractions[0], now = m_inflowFractions[1];
                const std::vector<double> stockW = RegionWeights(stock);
                AddOutPart(Scaled(stockW, exit), stock.temperature, stock.pressure);
                AddOutPart(Scaled(inW, now), inT, inP);
                MixRegion(stock, Scaled(stockW, stay), { MakePart(Scaled(inW, remain), inT, inP) });
                for (size_t i = 0; i < size; ++i)
                {
                    m_outValues[i] = exit * stock.values[i] + now * m_u[i];
                    stock.values[i] = stay * stock.values[i] + remain * m_u[i];
                }
                break;
            }
            }

            for (size_t c = 0; c < m_numC && c < out.GetNumCompounds(); ++c)
                for (size_t s = 0; s < m_numS && s < out.GetNumSizeClasses(); ++s)
                    out.SetValue(c, s, m_outValues[c * m_numS + s]);
        }

        // Anteil fraction des Zulaufs tritt im Schritt e aus.
        void AddToRingValues(size_t e, double fraction, const std::vector<double>& inW, double temperature, double pressure)
        {
            if (!(fraction > 0.0))
                return;
            SRegion& region = m_regions[RingRegion(e)];
            MixRegion(region, RegionWeights(region), { MakePart(Scaled(inW, fraction), temperature, pressure) });
            for (size_t i = 0; i < region.values.size(); ++i)
                region.values[i] += fraction * m_u[i];
        }

        void ReleaseRingValues(size_t k)
        {
            if (k >= m_numT || m_ring == 0)
                return;
            SRegion& due = m_regions[RingRegion(k)];
            AddOutPart(RegionWeights(due), due.temperature, due.pressure);
            for (size_t i = 0; i < due.values.size(); ++i)
            {
                m_outValues[i] += due.values[i];
                due.values[i] = 0.0;
            }
            due.temperature = kRoomTemperature;
            due.pressure = kRoomPressure;
        }

        void StepCascadeContinuum(const std::vector<double>& inW, double inT, double inP)
        {
            const size_t n = m_cellFractions.size();
            const size_t size = m_numC * m_numS;
            std::vector<std::vector<double>> cellW(n);
            for (size_t j = 0; j < n; ++j)
                cellW[j] = RegionWeights(m_regions[j]);
            // Ablauf: aus jedem Kessel der Austrittsanteil, dazu vom Zulauf.
            for (size_t j = 0; j < n; ++j)
            {
                const double exit = m_cellFractions[j].back();
                AddOutPart(Scaled(cellW[j], exit), m_regions[j].temperature, m_regions[j].pressure);
                for (size_t i = 0; i < size; ++i)
                    m_outValues[i] += exit * m_regions[j].values[i];
            }
            const double inflowExit = m_inflowFractions.back();
            AddOutPart(Scaled(inW, inflowExit), inT, inP);
            for (size_t i = 0; i < size; ++i)
                m_outValues[i] += inflowExit * m_u[i];

            // Neuer Inhalt, vom letzten Kessel zum ersten: Kessel t bekommt aus
            // jedem Kessel j <= t den Anteil P_(t-j), dazu Q_t vom Zulauf.
            for (size_t t = n; t-- > 0;)
            {
                std::vector<SMixPart> parts;
                for (size_t j = 0; j <= t; ++j)
                    parts.push_back(MakePart(Scaled(cellW[j], m_cellFractions[j][t - j]), m_regions[j].temperature, m_regions[j].pressure));
                parts.push_back(MakePart(Scaled(inW, m_inflowFractions[t]), inT, inP));
                SRegion probe;
                probe.temperature = m_regions[t].temperature;
                probe.pressure = m_regions[t].pressure;
                MixRegion(probe, std::vector<double>(m_numC, 0.0), parts);
                for (size_t i = 0; i < size; ++i)
                {
                    double value = m_inflowFractions[t] * m_u[i];
                    for (size_t j = 0; j <= t; ++j)
                        value += m_cellFractions[j][t - j] * m_regions[j].values[i];
                    m_regions[t].values[i] = value;
                }
                m_regions[t].temperature = probe.temperature;
                m_regions[t].pressure = probe.pressure;
            }
        }

        // ---------------- Daten ----------------

        SVesselSettings m_settings;

        // aus Initialize
        double m_dt = 0.0;
        size_t m_numS = 0;
        size_t m_numC = 1;
        size_t m_numT = 0;
        SSizeGrid m_sizes;
        std::vector<double> m_sizeWeights;
        std::vector<std::string> m_compounds;
        const CMaterialDatabase* m_materials = nullptr;
        bool m_particles = false;
        SCardSettings m_cardSettings;
        double m_countMin = 0.0;
        size_t m_cap = 1;
        size_t m_stockLimit = kDefaultStockLimit;
        double m_capWanted = 0.0;   // (tau/dt + 2) slices of cards
        double m_logSizeMin = 0.0;  // compartments: lower bound of class 1 (log10 m)
        size_t m_numClasses = 2;    // size classes per compound
        CRandom m_random;
        CRandom m_lotRandom; // Losnummer: gleich grosse Karten neu mischen (DrawLots)

        // Anteile je Schritt
        EVesselMixing m_mode = EVesselMixing::Mixed;
        std::vector<double> m_stockFractions;              // ideal durchmischt / Randzone: bleibt, geht
        std::vector<double> m_inflowFractions;             // Zulauf, Ziele je nach Typ
        std::vector<std::vector<double>> m_cellFractions;  // Kaskade: je Kessel bleibt, +1, ..., Austritt
        std::vector<std::vector<std::pair<size_t, double>>> m_cellDraws; // cascade, particle mode: target and share per draw
        size_t m_delaySteps = 0;                           // Pfropfen / Kern: ganze Schritte q
        double m_delayRest = 0.0;                          // und Rest r
        size_t m_ring = 0;                                 // Plaetze im Ring der Austrittsschritte

        // Zustand
        std::vector<SRegion> m_regions;
        std::vector<std::vector<double>> m_accounts; // Konten je Quelle
        std::vector<std::vector<std::vector<double>>> m_drawAccounts; // cascade: "stays" / "goes" per draw
        size_t m_inside = 0;                         // Karten im Behaelter (laufend mitgezaehlt)
        bool m_filled = false;
        double m_initialMass = 0.0;

        // wiederverwendete Puffer
        SCardBin m_inflow, m_outCards, m_scratch;
        std::vector<SCardBin> m_incoming;
        std::vector<std::uint16_t> m_targetIn;
        std::vector<size_t> m_order;
        std::vector<size_t> m_binStart, m_leaveCount; // StoreDraw
        std::vector<double> m_u, m_outValues;
        std::vector<SMixPart> m_outParts;
        std::vector<std::pair<size_t, SCardPlacement>> m_placements;
        std::vector<std::pair<size_t, SCardBin>> m_merged;

        // Hinweise
        std::set<std::string> m_missingHeatCapacity;
        size_t m_mergedCards = 0;
        double m_maxMergeRatio = 1.0;
        bool m_zeroSteadyInflow = false;
        bool m_fillCompoundMissing = false;
    };
}
