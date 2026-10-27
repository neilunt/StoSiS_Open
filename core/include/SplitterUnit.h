#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <cstdio>
#include <string>
#include "FlowsheetUnit.h"
#include "Random.h"

namespace stosis
{
    // Schwankung eines Splitter-Ausgangs (stochastischer Modus).
    struct SSplitterFluctuation
    {
        double sigma = 0.05; // Standardabweichung des Anteils (absolut, z.B. 0.05 = +-5 %-Punkte)
        double tau = 1.0;    // Korrelationszeit [s]; 0 = jeder Schritt unabhaengig neu gezogen
    };

    // Teilt einen Eingangsstrom auf N Ausgaenge (aktuell 2 oder 3) auf -
    // als reiner Mengenteiler: der Anteil ist fuer alle Partikelgroessen
    // gleich (groessenabhaengiges Trennen waere ein Sieb/Sichter).
    // Kein Hold-up: was in einem Schritt hereinkommt, geht im selben
    // Schritt wieder hinaus. Die Masse bleibt in jedem Schritt exakt
    // erhalten, auch im stochastischen Modus.
    // Im Partikelmodus setzt das Konto-Verfahren den Anteil um: ganze
    // Karten statt skalierter Mengen (DistributeCards).
    //
    // Mittlere Anteile mu_i: fuer die Ausgaenge 1 .. N-1 einstellbar, der
    // letzte Ausgang bekommt den Rest.
    //
    // Stochastischer Modus: Jeder Ausgang 1 .. N-1 schwankt um seinen
    // Mittelwert nach einem Ornstein-Uhlenbeck-Prozess mit eigenem sigma
    // und tau. Exakte Fortschreibung pro Makroschritt:
    //   g(t+dt) = mu + (g(t) - mu) * a + sigma * sqrt(1 - a^2) * xi,
    //   a = exp(-dt/tau), xi ~ N(0,1)
    // Dadurch haengen Mittelwert, Streuung und Zeitverhalten NICHT von dt
    // ab. Der Prozess startet bereits "eingeschwungen" (g(0) ~ N(mu, sigma)).
    // Liegt ein Anteil ausserhalb [0, 1] oder die Summe ueber 1, wird
    // abgeschnitten (bzw. anteilig gekuerzt) - bei grossem sigma
    // verschiebt das den Mittelwert leicht.
    class CSplitterUnit : public IFlowsheetUnit
    {
    public:
        explicit CSplitterUnit(size_t numOutputs)
            : m_fractions(numOutputs, numOutputs > 0 ? 1.0 / static_cast<double>(numOutputs) : 0.0)
            , m_fluctuations(numOutputs > 0 ? numOutputs - 1 : 0)
            , m_currentFractions(m_fractions)
        {
        }

        size_t GetNumOutputs() const { return m_fractions.size(); }

        // Mittlere Anteile mu_i aller Ausgaenge (der letzte = Rest).
        const std::vector<double>& GetFractions() const { return m_fractions; }

        // Uebernimmt die Anteile nur, wenn sie gueltig sind: richtige
        // Anzahl, keiner negativ, Summe = 1. Sonst false, alte Werte bleiben.
        bool SetFractions(const std::vector<double>& fractions)
        {
            if (fractions.size() != m_fractions.size())
                return false;
            double sum = 0.0;
            for (double f : fractions)
            {
                if (f < 0.0)
                    return false;
                sum += f;
            }
            if (std::abs(sum - 1.0) > 1e-9)
                return false;
            m_fractions = fractions;
            return true;
        }

        bool IsStochastic() const override { return m_stochastic; }
        void SetStochastic(bool stochastic) { m_stochastic = stochastic; }

        // Schwankung der Ausgaenge 1 .. N-1 (der Rest-Ausgang hat keine
        // eigene - er schwankt automatisch mit).
        const std::vector<SSplitterFluctuation>& GetFluctuations() const { return m_fluctuations; }

        bool SetFluctuations(const std::vector<SSplitterFluctuation>& fluctuations)
        {
            if (fluctuations.size() != m_fluctuations.size())
                return false;
            for (const SSplitterFluctuation& f : fluctuations)
                if (f.sigma < 0.0 || f.tau < 0.0)
                    return false;
            m_fluctuations = fluctuations;
            return true;
        }

        // Faustregel fuer einen Hinweis im Editor: liegt mu +- 3 sigma
        // ausserhalb [0, 1] oder kann die Summe der schwankenden Anteile
        // spuerbar ueber 1 gehen, wird oft abgeschnitten.
        static bool IsClippingLikely(const std::vector<double>& means,
            const std::vector<SSplitterFluctuation>& fluctuations)
        {
            double sumMeans = 0.0, sumVariance = 0.0;
            for (size_t i = 0; i < fluctuations.size() && i < means.size(); ++i)
            {
                const double mu = means[i];
                const double sigma = fluctuations[i].sigma;
                if (mu - 3.0 * sigma < 0.0 || mu + 3.0 * sigma > 1.0)
                    return true;
                sumMeans += mu;
                sumVariance += sigma * sigma;
            }
            return sumMeans + 3.0 * std::sqrt(sumVariance) > 1.0;
        }

        void Initialize(const SSimulationGrid& grid, std::uint64_t unitSeed) override
        {
            m_random.Seed(unitSeed);
            const double dt = grid.GetDt();
            const size_t numVarying = m_fluctuations.size();
            m_decay.assign(numVarying, 0.0);
            m_noise.assign(numVarying, 0.0);
            m_state.assign(numVarying, 0.0);
            for (size_t i = 0; i < numVarying; ++i)
            {
                const SSplitterFluctuation& f = m_fluctuations[i];
                if (f.tau > 0.0)
                {
                    m_decay[i] = std::exp(-dt / f.tau);
                    // sqrt(1 - a^2), ueber expm1 genauer fuer kleine dt/tau
                    m_noise[i] = f.sigma * std::sqrt(-std::expm1(-2.0 * dt / f.tau));
                }
                else
                {
                    m_decay[i] = 0.0;
                    m_noise[i] = f.sigma;
                }
                // Eingeschwungener Start statt "alle beginnen beim Mittelwert".
                m_state[i] = m_stochastic ? m_fractions[i] + f.sigma * m_random.Normal() : m_fractions[i];
            }
            UpdateCurrentFractions();

            // Partikelmodus: eigener Zufallsstrom fuer die Startposition, damit
            // der OU-Verlauf bei gleichem Seed derselbe ist wie im Kontinuum.
            m_cardRandom.Seed(MakeUnitSeed(unitSeed, "Konto-Verfahren"));
            m_accounts.assign(m_fractions.size(), 0.0);
            m_particles = grid.representation == ERepresentation::Particles;
        }

        void Step(const SStepContext& context, const std::vector<const CStreamSlice*>& inputs,
            std::vector<CStreamSlice>& outputs) override
        {
            // Zuerst den Zufallsprozess weiterschreiben - unabhaengig davon,
            // ob gerade etwas ankommt. So haengt die Zufallsfolge nur vom
            // Seed ab, nicht davon, wie der Splitter verdrahtet ist.
            if (m_stochastic && context.stepIndex > 0)
            {
                for (size_t i = 0; i < m_state.size(); ++i)
                {
                    const double mu = m_fractions[i];
                    m_state[i] = mu + (m_state[i] - mu) * m_decay[i] + m_noise[i] * m_random.Normal();
                }
                UpdateCurrentFractions();
            }

            const CStreamSlice* input = inputs.empty() ? nullptr : inputs[0];
            if (!input)
                return; // Ausgaenge bleiben 0
            if (input->HasCards())
            {
                DistributeCards(*input, outputs);
                return;
            }
            for (size_t i = 0; i < outputs.size() && i < m_currentFractions.size(); ++i)
                outputs[i].AssignScaled(*input, m_currentFractions[i]);
        }

        bool SupportsParticles() const override { return true; }

        std::string DescribeForLog() const override
        {
            std::string text = "Anteile";
            for (size_t i = 0; i < m_fractions.size(); ++i)
            {
                char percent[32];
                std::snprintf(percent, sizeof(percent), "%g", std::round(m_fractions[i] * 1000.0) / 10.0);
                for (char* c = percent; *c; ++c)
                    if (*c == '.')
                        *c = ',';
                text += (i == 0 ? " " : " / ") + std::string(percent);
            }
            text += m_stochastic ? " %, schwankend (OU-Prozess)" : " %, fest";
            if (m_particles)
                text += ", Karten nach dem Konto-Verfahren";
            return text;
        }

        // Konten des Konto-Verfahrens (Massenstrom, den jeder Ausgang noch
        // bekommen soll; Summe 0). Fuer Tests und Anzeige.
        const std::vector<double>& GetCardAccounts() const { return m_accounts; }

        // Tatsaechliche Anteile im zuletzt gerechneten Schritt (Summe = 1).
        const std::vector<double>& GetCurrentFractions() const { return m_currentFractions; }

    private:
        // Konto-Verfahren (Methoden-Dokument 13.18, 13.22): Die Karten eines
        // Schritts werden nach Stoff und darin nach Groesse geordnet und ab
        // einer zufaelligen Startposition einmal ringsum durchgegangen. Jede
        // Karte zahlt f_i * m auf das Konto jedes Ausgangs i ein und geht ganz
        // in den Ausgang mit dem hoechsten Kontostand, dessen Konto um m
        // belastet wird. Die Konten laufen ueber die Schritte weiter, ihre
        // Summe bleibt 0. So bekommt jeder Ausgang seinen Anteil bis auf
        // weniger als eine Karte, Karten werden nicht kopiert (die Kartenzahl
        // bleibt), und jeder Stoff und jede Groesse verteilt sich gleichmaessig.
        // Zuerst nach Stoff: Haetten zwei Inlets dieselben Kartengroessen,
        // wechselten sich bei reiner Groessenordnung die Stoffe ab, und bei
        // f = 50 % ginge in einem Schritt ein Stoff ganz nach A, der andere
        // ganz nach B.
        void DistributeCards(const CStreamSlice& input, std::vector<CStreamSlice>& outputs)
        {
            const size_t numOutputs = std::min(outputs.size(), m_currentFractions.size());
            for (size_t i = 0; i < numOutputs; ++i)
            {
                outputs[i].SetTemperature(input.GetTemperature());
                outputs[i].SetPressure(input.GetPressure());
            }
            const size_t numCards = input.GetNumCards();
            if (numCards == 0 || numOutputs == 0)
                return;

            const auto dominantCompound = [&input](size_t card)
            {
                size_t best = 0;
                for (size_t c = 1; c < input.GetNumCompounds(); ++c)
                    if (input.GetCardMassFlow(card, c) > input.GetCardMassFlow(card, best))
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

            const size_t start = std::min(static_cast<size_t>(m_cardRandom.Uniform() * static_cast<double>(numCards)), numCards - 1);
            for (size_t j = 0; j < numCards; ++j)
            {
                const size_t card = m_order[(start + j) % numCards];
                const double mass = input.GetCardMassFlow(card);
                size_t best = 0;
                for (size_t i = 0; i < numOutputs; ++i)
                {
                    m_accounts[i] += m_currentFractions[i] * mass;
                    if (m_accounts[i] > m_accounts[best])
                        best = i;
                }
                m_accounts[best] -= mass;
                outputs[best].AppendCard(input, card);
            }
        }

        void UpdateCurrentFractions()
        {
            m_currentFractions = m_fractions;
            if (!m_stochastic || m_fractions.empty())
                return;

            // Schwankende Ausgaenge auf [0, 1] begrenzen ...
            const size_t last = m_fractions.size() - 1;
            double sum = 0.0;
            for (size_t i = 0; i < last; ++i)
            {
                m_currentFractions[i] = std::clamp(m_state[i], 0.0, 1.0);
                sum += m_currentFractions[i];
            }
            // ... und anteilig kuerzen, falls fuer den Rest nichts bliebe.
            if (sum > 1.0)
            {
                for (size_t i = 0; i < last; ++i)
                    m_currentFractions[i] /= sum;
                sum = 1.0;
            }
            m_currentFractions[last] = std::max(0.0, 1.0 - sum);
        }

        std::vector<double> m_fractions;                   // Mittelwerte mu_i
        bool m_stochastic = false;
        std::vector<SSplitterFluctuation> m_fluctuations;  // Ausgaenge 1 .. N-1

        CRandom m_random;
        std::vector<double> m_decay;  // a_i = exp(-dt/tau_i)
        std::vector<double> m_noise;  // sigma_i * sqrt(1 - a_i^2)
        std::vector<double> m_state;  // ungekappter OU-Zustand g_i
        std::vector<double> m_currentFractions;

        // Partikelmodus (Konto-Verfahren)
        CRandom m_cardRandom;           // eigener Strom: Startposition je Schritt
        std::vector<double> m_accounts; // Konto je Ausgang, Summe 0
        std::vector<size_t> m_order;    // Kartenreihenfolge (wiederverwendet)
        bool m_particles = false;       // Partikelmodus in der letzten Initialisierung
    };
}
