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

    // Screen: classifies the feed by size into coarse (output 1) and fines
    // (output 2) with a grade efficiency T(d) (Plitt, Molerus-Hoffmann,
    // Teipel-Hennig, as in Dyssol). Several decks are several screens in
    // series. Continuum and particle mode (account method per compound and
    // size range): CTwoWaySeparatorUnit.
    class CScreenUnit : public CTwoWaySeparatorUnit
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
            if (IsParticleMode())
                result += "; Karten nach dem Konto-Verfahren, Anteil je Karte T(d)";
            return result;
        }

    protected:
        void InitializeShares(const SSimulationGrid& grid) override
        {
            const SSizeGrid sizes = grid.GetSizeGrid();
            m_nodeShare.resize(grid.numSizeClasses);
            for (size_t s = 0; s < grid.numSizeClasses; ++s)
                m_nodeShare[s] = m_settings.GradeEfficiency(sizes.GetSizeAt(s));
        }

        double NodeShare(size_t /*compound*/, size_t sizeIdx) const override
        {
            return sizeIdx < m_nodeShare.size() ? m_nodeShare[sizeIdx] : 0.0;
        }

        double CardShare(const CStreamSlice& input, size_t card) const override
        {
            return m_settings.GradeEfficiency(input.GetCardSize(card));
        }

    private:
        SScreenSettings m_settings;
        std::vector<double> m_nodeShare; // T(d) at the nodes of the size grid
    };
}
