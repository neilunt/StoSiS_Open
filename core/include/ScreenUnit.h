#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstdio>
#include <string>
#include <map>
#include <utility>
#include "FlowsheetUnit.h"
#include "Random.h"
#include "CardDensity.h"

namespace stosis
{
    // Grade efficiency models of the screen (as in Dyssol, CScreen).
    enum class EScreenModel
    {
        Plitt,           // T = 1 - exp(-0.693 (d/x_cut)^alpha)
        MolerusHoffmann, // T = 1 / (1 + (x_cut/d)^2 exp(alpha (1 - (d/x_cut)^2)))
        TeipelHennig     // T = (1 - (1 + 3 (d/x_cut)^((d/x_cut + alpha) beta))^(-1/2)) (1 - offset) + offset
    };

    inline const char* GetScreenModelLabel(EScreenModel model)
    {
        switch (model)
        {
        case EScreenModel::Plitt:           return "Plitt";
        case EScreenModel::MolerusHoffmann: return "Molerus-Hoffmann";
        case EScreenModel::TeipelHennig:    return "Teipel-Hennig";
        }
        return "";
    }

    struct SScreenSettings
    {
        EScreenModel model = EScreenModel::Plitt;
        double cutSize = 100e-6; // x_cut [m]
        double alpha = 8.0;      // sharpness of separation [-]
        double beta = 0.5;       // Teipel-Hennig only [-]
        double offset = 0.0;     // Teipel-Hennig only: share of fines in the coarse product [-]

        // Empty if valid, otherwise one sentence per problem.
        std::string Check() const
        {
            std::string text;
            if (!(cutSize > 0.0 && std::isfinite(cutSize)))
                text += "Trenngroesse x_cut muss groesser als 0 sein. ";
            if (!(alpha >= 0.0 && alpha <= 100.0) || (model == EScreenModel::MolerusHoffmann && !(alpha > 0.0)))
                text += model == EScreenModel::MolerusHoffmann ? "Trennschaerfe alpha muss in (0, 100] liegen. "
                                                               : "Trennschaerfe alpha muss in [0, 100] liegen. ";
            if (model == EScreenModel::TeipelHennig)
            {
                if (!(beta > 0.0 && beta <= 100.0))
                    text += "beta muss in (0, 100] liegen. ";
                if (!(offset >= 0.0 && offset <= 1.0))
                    text += "Offset muss zwischen 0 und 1 liegen. ";
            }
            if (!text.empty())
                text.pop_back();
            return text;
        }

        // Grade efficiency: mass fraction of particles of size d that leaves
        // through the coarse output, between 0 and 1.
        double GradeEfficiency(double size) const
        {
            if (!(size > 0.0) || !(cutSize > 0.0))
                return model == EScreenModel::TeipelHennig ? std::clamp(offset, 0.0, 1.0) : 0.0;
            const double x = size / cutSize;
            double t = 0.0;
            switch (model)
            {
            case EScreenModel::Plitt:
                t = 1.0 - std::exp(-0.693 * std::pow(x, alpha));
                break;
            case EScreenModel::MolerusHoffmann:
                t = 1.0 / (1.0 + std::exp(alpha * (1.0 - x * x)) / (x * x));
                break;
            case EScreenModel::TeipelHennig:
                t = (1.0 - std::pow(1.0 + 3.0 * std::pow(x, (x + alpha) * beta), -0.5)) * (1.0 - offset) + offset;
                break;
            }
            return std::isfinite(t) ? std::clamp(t, 0.0, 1.0) : (x > 1.0 ? 1.0 : 0.0);
        }
    };

    // Screen: classifies the feed by size into coarse (output 1) and fines
    // (output 2) with a grade efficiency T(d) (Plitt, Molerus-Hoffmann,
    // Teipel-Hennig, as in Dyssol). No hold-up; mass is conserved in every
    // step. Several decks are several screens in series.
    //
    // Continuum: coarse = T(d) * feed, fines = (1 - T(d)) * feed, at every
    // node of the size grid and for every compound.
    //
    // Particle mode: whole cards with the account method of the splitter
    // (Methodenhandbuch 9.4), but every card brings its own share T(d). Cards
    // with T = 0 or T = 1 go straight to fines or coarse. The others are
    // ordered by compound and size and walked once from a random start; each
    // pays T(d) * s to the coarse and (1 - T(d)) * s to the fines account of
    // its compound and size range (factor 2, as the ranges of the card view)
    // and goes completely to the output with the higher balance, which is
    // then charged s. s is the card's share of the card density of the slice
    // (ComputeCardShares). Within a range T changes little, so there every
    // (1/T)-th card goes to the coarse product; a balance never moves a card
    // into another range or compound. The accounts carry on over the steps;
    // cards are not copied.
    class CScreenUnit : public IFlowsheetUnit
    {
    public:
        const SScreenSettings& GetSettings() const { return m_settings; }

        // Takes the settings only if they are valid.
        bool SetSettings(const SScreenSettings& settings)
        {
            if (!settings.Check().empty())
                return false;
            m_settings = settings;
            return true;
        }

        void Initialize(const SSimulationGrid& grid, std::uint64_t unitSeed) override
        {
            m_random.Seed(MakeUnitSeed(unitSeed, "Konto-Verfahren"));
            m_accounts.clear();
            m_particles = grid.representation == ERepresentation::Particles;
            m_cardSettings = grid.cards;
            m_countMin = grid.GetCardCountMin();
            const SSizeGrid sizes = grid.GetSizeGrid();
            m_nodeShare.resize(grid.numSizeClasses);
            for (size_t s = 0; s < grid.numSizeClasses; ++s)
                m_nodeShare[s] = m_settings.GradeEfficiency(sizes.GetSizeAt(s));
        }

        void Step(const SStepContext& /*context*/, const std::vector<const CStreamSlice*>& inputs,
            std::vector<CStreamSlice>& outputs) override
        {
            const CStreamSlice* input = inputs.empty() ? nullptr : inputs[0];
            if (!input || outputs.size() < 2)
                return; // outputs stay 0
            if (input->HasCards())
            {
                DistributeCards(*input, outputs);
                return;
            }
            CStreamSlice& coarse = outputs[0];
            CStreamSlice& fines = outputs[1];
            coarse.AssignScaled(*input, 0.0);
            fines.AssignScaled(*input, 0.0);
            const size_t numS = std::min(input->GetNumSizeClasses(), m_nodeShare.size());
            for (size_t c = 0; c < input->GetNumCompounds(); ++c)
                for (size_t s = 0; s < numS; ++s)
                {
                    const double value = input->GetValue(c, s);
                    const double toCoarse = m_nodeShare[s] * value;
                    coarse.SetValue(c, s, toCoarse);
                    fines.SetValue(c, s, value - toCoarse);
                }
        }

        bool SupportsParticles() const override { return true; }

        std::string DescribeForLog() const override
        {
            char text[256];
            std::snprintf(text, sizeof(text), "%s, x_cut = %g %s, alpha = %g", GetScreenModelLabel(m_settings.model),
                m_settings.cutSize * kSizeDisplayFactor, kSizeDisplayUnit, m_settings.alpha);
            std::string result = text;
            if (m_settings.model == EScreenModel::TeipelHennig)
            {
                std::snprintf(text, sizeof(text), ", beta = %g, Offset = %g", m_settings.beta, m_settings.offset);
                result += text;
            }
            for (char& c : result)
                if (c == '.')
                    c = ',';
            if (m_particles)
                result += "; Karten nach dem Konto-Verfahren, Anteil je Karte T(d)";
            return result;
        }

        // Largest balance of a coarse account (card shares) over all compounds
        // and size ranges. For tests: stays below about one card share.
        double GetLargestAccount() const
        {
            double largest = 0.0;
            for (const auto& entry : m_accounts)
                largest = std::max(largest, std::abs(entry.second));
            return largest;
        }

    private:
        void DistributeCards(const CStreamSlice& input, std::vector<CStreamSlice>& outputs)
        {
            for (size_t i = 0; i < 2; ++i)
            {
                outputs[i].SetTemperature(input.GetTemperature());
                outputs[i].SetPressure(input.GetPressure());
            }
            const size_t numCards = input.GetNumCards();
            if (numCards == 0)
                return;

            // Order by dominant compound, then by size (as the splitter).
            const auto dominantCompound = [&input](size_t card)
            {
                size_t best = 0;
                for (size_t c = 1; c < input.GetNumCompounds(); ++c)
                    if (input.GetCardMass(card, c) > input.GetCardMass(card, best))
                        best = c;
                return best;
            };
            m_order.resize(numCards);
            std::iota(m_order.begin(), m_order.end(), size_t(0));
            std::stable_sort(m_order.begin(), m_order.end(), [&](size_t a, size_t b)
                {
                    const size_t ca = dominantCompound(a);
                    const size_t cb = dominantCompound(b);
                    return ca != cb ? ca < cb : input.GetCardSize(a) < input.GetCardSize(b);
                });

            m_sizes.resize(numCards);
            m_masses.resize(numCards);
            for (size_t k = 0; k < numCards; ++k)
            {
                m_sizes[k] = input.GetCardSize(k);
                m_masses[k] = input.GetCardMass(k);
            }
            std::vector<double> shares = ComputeCardShares(m_sizes, m_masses, m_cardSettings, m_countMin);
            if (!(std::accumulate(shares.begin(), shares.end(), 0.0) > 0.0))
                shares.assign(numCards, 1.0 / static_cast<double>(numCards));

            const size_t start = std::min(static_cast<size_t>(m_random.Uniform() * static_cast<double>(numCards)), numCards - 1);
            for (size_t j = 0; j < numCards; ++j)
            {
                const size_t card = m_order[(start + j) % numCards];
                const double t = m_settings.GradeEfficiency(m_sizes[card]);
                if (t <= 0.0 || t >= 1.0)
                {
                    outputs[t >= 1.0 ? 0 : 1].AppendCard(input, card);
                    continue;
                }
                // Coarse balance a of this compound and range; the fines balance is -a.
                const double share = shares[card];
                double& a = m_accounts[{ dominantCompound(card), GetCardRangeIndex(m_sizes[card], m_cardSettings) }];
                a += t * share;
                const bool toCoarse = a >= -a + share; // coarse a + t s against fines -a + (1 - t) s
                if (toCoarse)
                    a -= share;
                outputs[toCoarse ? 0 : 1].AppendCard(input, card);
            }
        }

        SScreenSettings m_settings;
        std::vector<double> m_nodeShare; // T(d) at the nodes of the size grid

        // Particle mode (account method)
        CRandom m_random;                // own stream: start position per step
        std::map<std::pair<size_t, int>, double> m_accounts; // coarse balance per (compound, size range)
        std::vector<size_t> m_order;
        std::vector<double> m_sizes, m_masses;
        SCardSettings m_cardSettings;
        double m_countMin = 0.0;
        bool m_particles = false;
    };
}
