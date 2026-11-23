#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <string>
#include "TwoWaySeparator.h"

namespace stosis
{
    // Grade efficiency models of the screen (as in Dyssol, CScreen). The
    // classifier uses the same curves over the settling velocity.
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

    // Grade efficiency T(x) of a model for the ratio x = d / x_cut (screen)
    // or x = w_s / w_cut (classifier), between 0 and 1. x <= 0 gives 0, with
    // Teipel-Hennig the offset.
    inline double EvaluateGradeEfficiency(EScreenModel model, double x, double alpha, double beta, double offset)
    {
        if (!(x > 0.0))
            return model == EScreenModel::TeipelHennig ? std::clamp(offset, 0.0, 1.0) : 0.0;
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

    // Checks alpha, beta and offset of a grade efficiency model; empty if
    // valid, otherwise one sentence per problem (each ending with a blank).
    inline std::string CheckGradeEfficiencyParameters(EScreenModel model, double alpha, double beta, double offset)
    {
        std::string text;
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
        return text;
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
            text += CheckGradeEfficiencyParameters(model, alpha, beta, offset);
            if (!text.empty())
                text.pop_back();
            return text;
        }

        // Grade efficiency: mass fraction of particles of size d that leaves
        // through the coarse output, between 0 and 1.
        double GradeEfficiency(double size) const
        {
            if (!(size > 0.0) || !(cutSize > 0.0))
                return EvaluateGradeEfficiency(model, 0.0, alpha, beta, offset);
            return EvaluateGradeEfficiency(model, size / cutSize, alpha, beta, offset);
        }
    };

    // Checks the decks of a screen with numDecks decks: every deck valid and
    // the cut sizes falling from deck 1 (top) downwards. Empty if valid,
    // otherwise one sentence per problem.
    inline std::string CheckScreenDecks(const std::vector<SScreenSettings>& decks, size_t numDecks)
    {
        if (decks.size() != numDecks)
            return "Das Sieb braucht " + std::to_string(numDecks) + (numDecks == 1 ? " Deck." : " Decks.");
        std::string text;
        for (size_t k = 0; k < decks.size(); ++k)
        {
            const std::string problems = decks[k].Check();
            if (!problems.empty())
                text += (numDecks > 1 ? "Deck " + std::to_string(k + 1) + ": " : std::string()) + problems + " ";
            if (k > 0 && !(decks[k].cutSize < decks[k - 1].cutSize))
                text += "Trenngroesse von Deck " + std::to_string(k + 1) + " muss kleiner sein als die von Deck "
                    + std::to_string(k) + ". ";
        }
        if (!text.empty())
            text.pop_back();
        return text;
    }

    // Shares of the outputs of a screen for particles of size d. The feed
    // meets deck 1 (top, largest cut size) first; what stays on deck k is
    // output k, what passes falls onto deck k + 1, and what passes the lowest
    // deck is the last output (fines):
    //   t_k = T_k(d) * (1 - T_1(d)) ... (1 - T_(k-1)(d)),  t_n = (1 - T_1(d)) ... (1 - T_(n-1)(d)).
    // With one deck: t_1 = T(d) (coarse), t_2 = 1 - T(d) (fines).
    inline void ComputeScreenShares(const std::vector<SScreenSettings>& decks, double size, std::vector<double>& shares)
    {
        shares.resize(decks.size() + 1);
        double passing = 1.0;
        for (size_t k = 0; k < decks.size(); ++k)
        {
            const double t = decks[k].GradeEfficiency(size);
            shares[k] = passing * t;
            passing *= 1.0 - t;
        }
        shares[decks.size()] = passing;
    }

    // Screen with one or more decks (Methodenhandbuch 7.5): n outputs, n - 1
    // decks, each with its own grade efficiency T_k(d) (Plitt,
    // Molerus-Hoffmann, Teipel-Hennig, as in Dyssol). Output 1 is the coarse
    // product of the top deck, output k the coarse product of deck k, the last
    // output the fines of the lowest deck (ComputeScreenShares). With two
    // outputs: coarse (output 1) and fines (output 2). Continuum and particle
    // mode as in CSeparatorUnit; the account share s of a card is taken
    // without flattening (ComputeAccountShares), because a screen often sits
    // in a recycle (mill circuit), where the flattening would couple the
    // shares with earlier decisions of the account (see CardDensity.h).
    class CScreenUnit : public CSeparatorUnit
    {
    public:
        explicit CScreenUnit(size_t numOutputs = 2)
            : CSeparatorUnit(numOutputs, false)
        {
            // Default cut sizes 100, 50, 25 um ... from the top deck downwards.
            m_decks.resize(GetNumOutputs() - 1);
            for (size_t k = 0; k < m_decks.size(); ++k)
                m_decks[k].cutSize = 100e-6 / std::pow(2.0, static_cast<double>(k));
        }

        size_t GetNumDecks() const { return m_decks.size(); }
        const std::vector<SScreenSettings>& GetDecks() const { return m_decks; }

        // Takes the decks only if they are valid (CheckScreenDecks).
        bool SetDecks(const std::vector<SScreenSettings>& decks)
        {
            if (!CheckScreenDecks(decks, m_decks.size()).empty())
                return false;
            m_decks = decks;
            return true;
        }

        // Deck 1 (top deck); with two outputs the only deck.
        const SScreenSettings& GetSettings() const { return m_decks.front(); }

        // Sets deck 1; only if valid together with the other decks.
        bool SetSettings(const SScreenSettings& settings)
        {
            std::vector<SScreenSettings> decks = m_decks;
            decks.front() = settings;
            return SetDecks(decks);
        }

        std::string DescribeForLog() const override
        {
            std::string result;
            if (m_decks.size() > 1)
                result = std::to_string(m_decks.size()) + " Decks: ";
            for (size_t k = 0; k < m_decks.size(); ++k)
            {
                const SScreenSettings& deck = m_decks[k];
                char text[256];
                std::snprintf(text, sizeof(text), "%s%s, x_cut = %g %s, alpha = %g", k > 0 ? "; " : "",
                    GetScreenModelLabel(deck.model), deck.cutSize * kSizeDisplayFactor, kSizeDisplayUnit, deck.alpha);
                result += text;
                if (deck.model == EScreenModel::TeipelHennig)
                {
                    std::snprintf(text, sizeof(text), ", beta = %g, Offset = %g", deck.beta, deck.offset);
                    result += text;
                }
            }
            for (char& c : result)
                if (c == '.')
                    c = ',';
            if (IsParticleMode())
                result += m_decks.size() > 1 ? "; Karten nach dem Konto-Verfahren, Anteile je Karte aus T_k(d)"
                                             : "; Karten nach dem Konto-Verfahren, Anteil je Karte T(d)";
            return result;
        }

    protected:
        void InitializeShares(const SSimulationGrid& grid) override
        {
            const SSizeGrid sizes = grid.GetSizeGrid();
            m_nodeShares.resize(grid.numSizeClasses);
            for (size_t s = 0; s < grid.numSizeClasses; ++s)
                ComputeScreenShares(m_decks, sizes.GetSizeAt(s), m_nodeShares[s]);
        }

        void NodeShares(size_t /*compound*/, size_t sizeIdx, std::vector<double>& shares) const override
        {
            if (sizeIdx < m_nodeShares.size())
                std::copy(m_nodeShares[sizeIdx].begin(), m_nodeShares[sizeIdx].end(), shares.begin());
            else
            {
                std::fill(shares.begin(), shares.end(), 0.0);
                shares.back() = 1.0;
            }
        }

        void CardShares(const CStreamSlice& input, size_t card, std::vector<double>& shares) const override
        {
            ComputeScreenShares(m_decks, input.GetCardSize(card), shares);
        }

    private:
        std::vector<SScreenSettings> m_decks;            // deck 1 (top) ... deck n - 1
        std::vector<std::vector<double>> m_nodeShares;   // shares of the outputs at the nodes of the size grid
    };
}
