#pragma once
#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include "ScreenUnit.h"
#include "Materials.h"

namespace stosis
{
    inline constexpr double kStandardGravity = 9.80665; // m/s^2

    // Drag coefficient of a sphere after Kaskas (Stiess, Mechanische
    // Verfahrenstechnik): c_w = 24/Re + 4/sqrt(Re) + 0.4, from the Stokes
    // range up to the Newton range, Re < 2e5.
    inline double KaskasDragCoefficient(double reynolds)
    {
        return 24.0 / reynolds + 4.0 / std::sqrt(reynolds) + 0.4;
    }
    inline constexpr double kKaskasReynoldsMax = 2e5;

    struct SSettlingVelocity
    {
        double velocity = 0.0; // w_s [m/s]
        double reynolds = 0.0; // Re = w_s d rho_f / eta
    };

    // Terminal settling velocity of a sphere of size d [m] and density
    // rho_p in a fluid of density rho_f [kg/m3] and viscosity eta [Pa s]:
    // weight minus buoyancy = drag, with c_w after Kaskas. With the
    // Archimedes number Ar = d^3 (rho_p - rho_f) rho_f g / eta^2 this is
    // c_w Re^2 = 24 Re + 4 Re^1.5 + 0.4 Re^2 = 4/3 Ar, for x = sqrt(Re) a
    // polynomial that rises and is convex for x > 0. Newton's method from
    // above (each term alone gives an upper bound) falls monotonically to
    // the only root. 0 if the particle does not sink (rho_p <= rho_f).
    inline SSettlingVelocity ComputeSettlingVelocity(double size, double particleDensity, double fluidDensity,
        double fluidViscosity, double gravity = kStandardGravity)
    {
        SSettlingVelocity result;
        if (!(size > 0.0) || !(fluidDensity > 0.0) || !(fluidViscosity > 0.0) || !(particleDensity > fluidDensity)
            || !std::isfinite(size) || !std::isfinite(particleDensity))
            return result;
        const double archimedes = size * size * size * (particleDensity - fluidDensity) * fluidDensity * gravity
            / (fluidViscosity * fluidViscosity);
        const double k = 4.0 / 3.0 * archimedes;
        double x = std::min({ std::sqrt(k / 24.0), std::cbrt(k / 4.0), std::sqrt(std::sqrt(k / 0.4)) });
        for (int i = 0; i < 200; ++i)
        {
            const double f = ((0.4 * x + 4.0) * x + 24.0) * x * x - k;
            const double df = ((1.6 * x + 12.0) * x + 48.0) * x;
            const double next = x - f / df;
            if (!(next < x) || !(next > 0.0))
                break; // no further descent: x is the root to rounding
            x = next;
        }
        result.reynolds = x * x;
        result.velocity = result.reynolds * fluidViscosity / (size * fluidDensity);
        return result;
    }

    // Fluid of a classifier at temperature T and pressure p from the
    // material file; ok == false with a sentence in problem otherwise.
    struct SClassifierFluid
    {
        bool ok = false;
        double density = 0.0;   // kg/m3
        double viscosity = 0.0; // Pa s
        std::string problem;
    };

    inline SClassifierFluid GetClassifierFluid(const CMaterialDatabase* materials, const std::string& name, double T, double p)
    {
        SClassifierFluid fluid;
        const SMaterial* material = materials ? materials->FindByName(name) : nullptr;
        if (!materials)
            fluid.problem = "Materialdatei fehlt, ohne Fluid geht alles ins Leichtgut.";
        else if (!material)
            fluid.problem = "Fluid " + name + " fehlt in der Materialdatei, alles geht ins Leichtgut.";
        else
        {
            const SPropertyValue density = material->GetProperty("density", T, p);
            const SPropertyValue viscosity = material->GetProperty("viscosity", T, p);
            fluid.density = density.value;
            fluid.viscosity = viscosity.value;
            fluid.ok = density.found && viscosity.found && fluid.density > 0.0 && fluid.viscosity > 0.0;
            if (!fluid.ok)
                fluid.problem = "Fluid " + name + " hat keine Dichte oder Viskositaet in der Materialdatei, alles geht ins Leichtgut.";
        }
        return fluid;
    }

    // Density of a compound at T and p [kg/m3]; NaN if unknown.
    inline double GetCompoundDensity(const CMaterialDatabase* materials, const std::string& name, double T, double p)
    {
        const SMaterial* material = materials ? materials->FindByName(name) : nullptr;
        const SPropertyValue density = material ? material->GetProperty("density", T, p) : SPropertyValue{};
        return density.found && density.value > 0.0 ? density.value : std::nan("");
    }

    struct SClassifierSettings
    {
        EScreenModel model = EScreenModel::Plitt;
        double cutVelocity = 0.5; // w_cut [m/s]: settling velocity with T = 0.5 (Plitt about)
        double alpha = 4.0;       // sharpness over w_s / w_cut [-]
        double beta = 0.5;        // Teipel-Hennig only [-]
        double offset = 0.0;      // Teipel-Hennig only: share of the light product in the heavy one [-]
        std::string fluid = "Air"; // name in the material file: Air (air classifier), H2O (settling tank)

        // Empty if valid, otherwise one sentence per problem.
        std::string Check() const
        {
            std::string text;
            if (!(cutVelocity > 0.0 && std::isfinite(cutVelocity)))
                text += "Trenngeschwindigkeit w_cut muss groesser als 0 sein. ";
            text += CheckGradeEfficiencyParameters(model, alpha, beta, offset);
            if (fluid.empty())
                text += "Fluid fehlt. ";
            if (!text.empty())
                text.pop_back();
            return text;
        }

        // Share that goes to the heavy product (output 1) at settling velocity w_s.
        double GradeEfficiency(double settlingVelocity) const
        {
            return EvaluateGradeEfficiency(model, cutVelocity > 0.0 ? settlingVelocity / cutVelocity : 0.0,
                alpha, beta, offset);
        }
    };

    // Classifier: separates by terminal settling velocity w_s(d, rho) in a
    // fluid, so by size and density together - an air classifier (fluid
    // air, w_cut = upward air velocity) or a settling tank (fluid water,
    // w_cut = overflow rate Q/A). Output 1 heavy product (w_s above w_cut,
    // settles), output 2 light product (carried away; also everything that
    // floats). The grade efficiency T(w_s / w_cut) uses the curves of the
    // screen. Fluid density and viscosity and the density of every compound
    // come from the material file at temperature and pressure of the feed.
    // A card with several compounds (only after coagulation) has the
    // density m / sum(m_c / rho_c). Continuum and particle mode (account
    // method per compound and size range): CTwoWaySeparatorUnit.
    class CClassifierUnit : public CTwoWaySeparatorUnit
    {
    public:
        const SClassifierSettings& GetSettings() const { return m_settings; }

        // Takes the settings only if they are valid.
        bool SetSettings(const SClassifierSettings& settings)
        {
            if (!settings.Check().empty())
                return false;
            m_settings = settings;
            return true;
        }

        std::string DescribeForLog() const override
        {
            char text[256];
            std::snprintf(text, sizeof(text), "%s, w_cut = %g m/s, alpha = %g", GetScreenModelLabel(m_settings.model),
                m_settings.cutVelocity, m_settings.alpha);
            std::string result = text;
            if (m_settings.model == EScreenModel::TeipelHennig)
            {
                std::snprintf(text, sizeof(text), ", beta = %g, Offset = %g", m_settings.beta, m_settings.offset);
                result += text;
            }
            for (char& c : result)
                if (c == '.')
                    c = ',';
            result += ", Fluid " + m_settings.fluid + ", Sinkgeschwindigkeit nach Kaskas";
            if (IsParticleMode())
                result += "; Karten nach dem Konto-Verfahren, Anteil je Karte T(w_s)";
            return result;
        }

        std::vector<std::string> GetWarnings() const override
        {
            std::vector<std::string> warnings = m_warnings;
            if (m_largestReynolds > kKaskasReynoldsMax)
            {
                char text[256];
                std::snprintf(text, sizeof(text),
                    "Kaskas ist nur bis Re = 2e5 belegt, hier bis Re = %.2g (%s, d = %g %s).", m_largestReynolds,
                    m_largestReynoldsCompound.c_str(), m_largestReynoldsSize * kSizeDisplayFactor, kSizeDisplayUnit);
                std::string sentence = text;
                for (char& c : sentence)
                    if (c == '.' && &c != &sentence.back())
                        c = ',';
                warnings.push_back(sentence);
            }
            return warnings;
        }

        // Settling velocity of compound c at size d under the conditions of
        // the last step (tests); 0 if a value is missing.
        double GetSettlingVelocity(size_t compound, double size) const
        {
            if (!m_fluid.ok || compound >= m_density.size())
                return 0.0;
            return ComputeSettlingVelocity(size, m_density[compound], m_fluid.density, m_fluid.viscosity).velocity;
        }

    protected:
        void InitializeShares(const SSimulationGrid& grid) override
        {
            m_materials = grid.materials;
            m_compounds = grid.compounds;
            m_numCompounds = grid.GetNumCompounds();
            const SSizeGrid sizes = grid.GetSizeGrid();
            m_nodeSizes.resize(grid.numSizeClasses);
            for (size_t s = 0; s < grid.numSizeClasses; ++s)
                m_nodeSizes[s] = sizes.GetSizeAt(s);
            m_haveConditions = false;
            m_warnings.clear();
            m_largestReynolds = 0.0;
            m_largestReynoldsSize = 0.0;
            m_largestReynoldsCompound.clear();
        }

        // Fluid and compound properties follow temperature and pressure of
        // the feed; the table T(compound, node) is computed anew only when
        // they change.
        void PrepareStep(const CStreamSlice& input) override
        {
            const double T = input.GetTemperature();
            const double p = input.GetPressure();
            if (m_haveConditions && T == m_temperature && p == m_pressure)
                return;
            m_haveConditions = true;
            m_temperature = T;
            m_pressure = p;

            m_fluid = GetClassifierFluid(m_materials, m_settings.fluid, T, p);
            if (!m_fluid.ok)
                AddWarning(m_fluid.problem);
            m_density.assign(m_numCompounds, std::nan(""));
            for (size_t c = 0; c < m_compounds.size() && c < m_numCompounds; ++c)
            {
                m_density[c] = GetCompoundDensity(m_materials, m_compounds[c], T, p);
                if (m_materials && !(m_density[c] > 0.0))
                    AddWarning("Stoff " + m_compounds[c] + " hat keine Dichte in der Materialdatei und geht ganz ins Leichtgut.");
            }

            const size_t numS = m_nodeSizes.size();
            m_nodeShare.assign(m_numCompounds * numS, 0.0);
            m_nodeReynolds.assign(m_numCompounds * numS, 0.0);
            if (!m_fluid.ok)
                return;
            for (size_t c = 0; c < m_numCompounds; ++c)
                for (size_t s = 0; s < numS; ++s)
                {
                    if (!(m_density[c] > 0.0))
                        continue;
                    const SSettlingVelocity w = ComputeSettlingVelocity(m_nodeSizes[s], m_density[c], m_fluid.density, m_fluid.viscosity);
                    m_nodeShare[c * numS + s] = m_settings.GradeEfficiency(w.velocity);
                    m_nodeReynolds[c * numS + s] = w.reynolds;
                }
        }

        double NodeShare(size_t compound, size_t sizeIdx) const override
        {
            const size_t numS = m_nodeSizes.size();
            if (compound >= m_numCompounds || sizeIdx >= numS)
                return 0.0;
            NoteReynolds(m_nodeReynolds[compound * numS + sizeIdx], compound, m_nodeSizes[sizeIdx]);
            return m_nodeShare[compound * numS + sizeIdx];
        }

        double CardShare(const CStreamSlice& input, size_t card) const override
        {
            if (!m_fluid.ok)
                return 0.0;
            const int single = SingleCompound(input, card);
            double density = 0.0;
            size_t compound = 0;
            if (single >= 0)
            {
                compound = static_cast<size_t>(single);
                density = compound < m_density.size() ? m_density[compound] : std::nan("");
            }
            else
            {
                double mass = 0.0, volume = 0.0, largest = -1.0;
                for (size_t c = 0; c < input.GetNumCompounds(); ++c)
                {
                    const double m = input.GetCardMass(card, c);
                    if (m == 0.0)
                        continue;
                    const double rho = c < m_density.size() ? m_density[c] : std::nan("");
                    mass += m;
                    volume += m / rho; // NaN if a density is missing
                    if (m > largest)
                    {
                        largest = m;
                        compound = c;
                    }
                }
                density = mass / volume;
            }
            if (!(density > 0.0))
                return 0.0;
            const double size = input.GetCardSize(card);
            const SSettlingVelocity w = ComputeSettlingVelocity(size, density, m_fluid.density, m_fluid.viscosity);
            NoteReynolds(w.reynolds, compound, size);
            return m_settings.GradeEfficiency(w.velocity);
        }

    private:
        void AddWarning(const std::string& text)
        {
            if (std::find(m_warnings.begin(), m_warnings.end(), text) == m_warnings.end())
                m_warnings.push_back(text);
        }

        // Diagnostics only: largest Reynolds number that carried material.
        void NoteReynolds(double reynolds, size_t compound, double size) const
        {
            if (reynolds <= m_largestReynolds)
                return;
            m_largestReynolds = reynolds;
            m_largestReynoldsSize = size;
            m_largestReynoldsCompound = compound < m_compounds.size() ? m_compounds[compound] : std::string("Stoff");
        }

        SClassifierSettings m_settings;
        const CMaterialDatabase* m_materials = nullptr;
        std::vector<std::string> m_compounds;
        size_t m_numCompounds = 1;
        std::vector<double> m_nodeSizes;

        // Conditions of the last step and what follows from them.
        bool m_haveConditions = false;
        double m_temperature = 0.0;
        double m_pressure = 0.0;
        SClassifierFluid m_fluid;
        std::vector<double> m_density;      // per compound [kg/m3], NaN if unknown
        std::vector<double> m_nodeShare;    // [compound * sizes + node]
        std::vector<double> m_nodeReynolds; // [compound * sizes + node]

        std::vector<std::string> m_warnings;
        mutable double m_largestReynolds = 0.0;
        mutable double m_largestReynoldsSize = 0.0;
        mutable std::string m_largestReynoldsCompound;
    };
}
