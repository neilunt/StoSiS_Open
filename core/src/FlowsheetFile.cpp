#include "FlowsheetFile.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>

#ifndef STOSIS_VERSION
#define STOSIS_VERSION "?"
#endif

// Flowsheet als Datei (Methodenhandbuch 3.4). Die JSON-Bibliothek
// (nlohmann/json) wird nur hier und in MaterialDatabase.cpp gebraucht.

namespace stosis
{
    namespace
    {
        // ordered_json behaelt die Reihenfolge der Felder.
        using json = nlohmann::ordered_json;

        // ---- Schluessel der Aufzaehlungen in der Datei -------------------

        template <typename E>
        struct SEnumKey
        {
            E value;
            const char* key;
        };

        constexpr SEnumKey<ENodeType> kNodeTypes[] = {
            { ENodeType::Inlet, "Inlet" }, { ENodeType::Outlet, "Outlet" }, { ENodeType::Process, "Process" },
            { ENodeType::Splitter, "Splitter" }, { ENodeType::Merger, "Merger" }, { ENodeType::Vessel, "Vessel" },
            { ENodeType::Screen, "Screen" } };
        constexpr SEnumKey<ESizeGridType> kSizeGridTypes[] = {
            { ESizeGridType::Logarithmic, "logarithmic" }, { ESizeGridType::Linear, "linear" } };
        constexpr SEnumKey<ERepresentation> kRepresentations[] = {
            { ERepresentation::Continuum, "continuum" }, { ERepresentation::Particles, "particles" } };
        constexpr SEnumKey<ECardDensityRule> kCardRules[] = {
            { ECardDensityRule::Sum, "sum" }, { ECardDensityRule::Product, "product" },
            { ECardDensityRule::Quadratic, "quadratic" } };
        constexpr SEnumKey<EDistributionType> kDistributionTypes[] = {
            { EDistributionType::LogNormal, "lognormal" }, { EDistributionType::Normal, "normal" },
            { EDistributionType::RRSB, "rrsb" }, { EDistributionType::GGS, "ggs" }, { EDistributionType::Table, "table" } };
        constexpr SEnumKey<EVesselMixing> kVesselMixings[] = {
            { EVesselMixing::Mixed, "mixed" }, { EVesselMixing::Plug, "plug" },
            { EVesselMixing::Cascade, "cascade" }, { EVesselMixing::CoreFlow, "coreFlow" } };
        constexpr SEnumKey<EVesselFill> kVesselFills[] = {
            { EVesselFill::Empty, "empty" }, { EVesselFill::SteadyState, "steadyState" },
            { EVesselFill::Prescribed, "prescribed" } };
        constexpr SEnumKey<EScreenModel> kScreenModels[] = {
            { EScreenModel::Plitt, "plitt" }, { EScreenModel::MolerusHoffmann, "molerusHoffmann" },
            { EScreenModel::TeipelHennig, "teipelHennig" } };

        // "recycle" einer Verbindung: von Hand markiert, vom Graphen
        // aufgeschnitten oder nicht aufgeschnitten (Unknown nur beim Lesen).
        enum class ECut { Unknown, Manual, Auto, No };
        constexpr SEnumKey<ECut> kCuts[] = {
            { ECut::Manual, "manual" }, { ECut::Auto, "auto" }, { ECut::No, "no" } };

        template <typename E, size_t N>
        const char* KeyOf(const SEnumKey<E> (&table)[N], E value)
        {
            for (const SEnumKey<E>& entry : table)
                if (entry.value == value)
                    return entry.key;
            return table[0].key;
        }

        // ---- Schreiben ---------------------------------------------------

        json DistributionToJson(const SSizeDistribution& distribution)
        {
            json j = json::object();
            j["type"] = KeyOf(kDistributionTypes, distribution.type);
            j["position"] = distribution.position;
            j["width"] = distribution.width;
            json points = json::array();
            for (const SDistributionPoint& point : distribution.points)
                points.push_back(json::array({ point.size, point.fraction }));
            j["points"] = std::move(points);
            return j;
        }

        json SimulationToJson(const CFlowsheetGraph& graph, const SFlowsheetFileSettings& settings)
        {
            const SSimulationGrid& grid = graph.GetGrid();
            json simulation = json::object();
            simulation["duration"] = grid.totalTime;
            simulation["dt"] = grid.GetDt();
            simulation["seed"] = grid.randomSeed;
            simulation["runs"] = settings.runs;
            simulation["representation"] = KeyOf(kRepresentations, grid.representation);
            json sizes = json::object();
            sizes["type"] = KeyOf(kSizeGridTypes, grid.sizeGridType);
            sizes["classes"] = grid.numSizeClasses;
            sizes["dMin"] = grid.sizeMin;
            sizes["dMax"] = grid.sizeMax;
            simulation["sizeGrid"] = std::move(sizes);
            // Ohne die Klassen und die Speichergrenze der Kartenansicht.
            const SCardSettings& c = grid.cards;
            json cards = json::object();
            cards["perSlice"] = c.cardsPerSlice;
            cards["rule"] = KeyOf(kCardRules, c.rule);
            cards["numberShare"] = c.numberShare;
            cards["exponent"] = c.exponent;
            cards["sliceLimits"] = c.sliceLimits;
            cards["upperFactor"] = c.upperFactor;
            cards["splitFactor"] = c.splitFactor;
            simulation["cards"] = std::move(cards);
            return simulation;
        }

        json UnitToJson(const CFlowsheetGraph& graph, const SFlowsheetNode& node)
        {
            json j = json::object();
            j["id"] = node.id;
            j["type"] = KeyOf(kNodeTypes, node.type);
            j["name"] = node.name;
            j["position"] = json::array({ node.posX, node.posY });
            json settings = json::object();
            switch (node.type)
            {
            case ENodeType::Inlet:
                if (const CInletUnit* inlet = graph.GetInlet(node.id))
                {
                    settings["material"] = inlet->GetMaterial();
                    settings["temperature"] = inlet->GetTemperature();
                    settings["pressure"] = inlet->GetPressure();
                    json profile = json::array();
                    for (const SInletTimePoint& point : inlet->GetTimeProfile())
                        profile.push_back(json::array({ point.time, point.massFlow }));
                    settings["massFlow"] = std::move(profile);
                    settings["sizeDistribution"] = DistributionToJson(inlet->GetSizeDistribution());
                    j["settings"] = std::move(settings);
                }
                break;
            case ENodeType::Splitter:
                j["outputs"] = node.numOutputs;
                if (const CSplitterUnit* splitter = graph.GetSplitter(node.id))
                {
                    settings["fractions"] = splitter->GetFractions();
                    settings["stochastic"] = splitter->IsStochastic();
                    json fluctuations = json::array();
                    for (const SSplitterFluctuation& f : splitter->GetFluctuations())
                    {
                        json item = json::object();
                        item["sigma"] = f.sigma;
                        item["tau"] = f.tau;
                        fluctuations.push_back(std::move(item));
                    }
                    settings["fluctuations"] = std::move(fluctuations);
                    j["settings"] = std::move(settings);
                }
                break;
            case ENodeType::Merger:
                j["inputs"] = node.numInputs;
                break;
            case ENodeType::Process:
                if (const IStreamProcessor* processor = graph.GetProcessor(node.id))
                {
                    settings["model"] = processor->GetTypeName();
                    json parameters = json::object();
                    for (const SParameterDescriptor& descriptor : processor->GetParameterDescriptors())
                        parameters[descriptor.id] = processor->GetParameterValue(descriptor.id);
                    settings["parameters"] = std::move(parameters);
                    j["settings"] = std::move(settings);
                }
                break;
            case ENodeType::Vessel:
                if (const CVesselUnit* vessel = graph.GetVessel(node.id))
                {
                    const SVesselSettings& v = vessel->GetSettings();
                    settings["mixing"] = KeyOf(kVesselMixings, v.mixing);
                    settings["residenceTime"] = v.residenceTime;
                    settings["batch"] = v.batch;
                    settings["cells"] = v.cells;
                    settings["coreShare"] = v.coreShare;
                    settings["coreTime"] = v.coreTime;
                    settings["fill"] = KeyOf(kVesselFills, v.fill);
                    settings["fillMass"] = v.fillMass;
                    settings["fillMaterial"] = v.fillMaterial;
                    settings["fillDistribution"] = DistributionToJson(v.fillDistribution);
                    settings["fillTemperature"] = v.fillTemperature;
                    settings["fillPressure"] = v.fillPressure;
                    settings["stockFactor"] = v.stockFactor;
                    j["settings"] = std::move(settings);
                }
                break;
            case ENodeType::Screen:
                if (const CScreenUnit* screen = graph.GetScreen(node.id))
                {
                    const SScreenSettings& s = screen->GetSettings();
                    settings["model"] = KeyOf(kScreenModels, s.model);
                    settings["cutSize"] = s.cutSize;
                    settings["alpha"] = s.alpha;
                    settings["beta"] = s.beta;
                    settings["offset"] = s.offset;
                    j["settings"] = std::move(settings);
                }
                break;
            case ENodeType::Outlet:
                break;
            }
            return j;
        }

        // ---- Gut lesbare Ausgabe -----------------------------------------
        // Listen aus Zahlen und kleine Objekte aus Zahlen und Texten stehen
        // auf einer Zeile, alles andere eingerueckt untereinander.

        constexpr size_t kLineWidth = 120;

        // Zahl, Text, Wahrheitswert oder leere Liste: steht immer auf einer Zeile.
        bool IsPrimitive(const json& value) { return (!value.is_object() && !value.is_array()) || value.empty(); }

        bool IsFlat(const json& value)
        {
            if (IsPrimitive(value))
                return true;
            for (const json& item : value)
                if (!IsPrimitive(item))
                    return false;
            return true;
        }

        std::string OneLine(const json& value)
        {
            if (IsPrimitive(value))
                return value.dump();
            std::string text;
            if (value.is_array())
            {
                for (const json& item : value)
                    text += (text.empty() ? "" : ", ") + OneLine(item);
                return "[" + text + "]";
            }
            if (value.empty())
                return "{}";
            for (auto it = value.begin(); it != value.end(); ++it)
                text += (text.empty() ? "" : ", ") + json(it.key()).dump() + ": " + OneLine(it.value());
            return "{ " + text + " }";
        }

        // column = Spalte, in der der Wert beginnt (fuer die Zeilenlaenge).
        void Format(const json& value, size_t indent, size_t column, std::string& out)
        {
            if (IsPrimitive(value) || value.empty())
            {
                out += OneLine(value);
                return;
            }
            const std::string line = OneLine(value);
            bool flatItems = true;
            bool primitiveItems = true;
            for (const json& item : value)
            {
                flatItems = flatItems && IsFlat(item);
                primitiveItems = primitiveItems && IsPrimitive(item);
            }
            const bool fits = column + line.size() <= kLineWidth;
            if ((value.is_array() && (primitiveItems || (flatItems && fits))) || (value.is_object() && primitiveItems && fits))
            {
                out += line;
                return;
            }
            const std::string inner(indent + 4, ' ');
            out += value.is_array() ? "[\n" : "{\n";
            size_t index = 0;
            for (auto it = value.begin(); it != value.end(); ++it, ++index)
            {
                std::string prefix = inner;
                if (value.is_object())
                    prefix += json(it.key()).dump() + ": ";
                out += prefix;
                Format(it.value(), indent + 4, prefix.size(), out);
                out += index + 1 < value.size() ? ",\n" : "\n";
            }
            out += std::string(indent, ' ') + (value.is_array() ? "]" : "}");
        }

        // ---- Lesen ---------------------------------------------------------

        // Inhaltlicher Fehler mit Ortsangabe ("Unit 3 (Splitter1), fractions: ...").
        struct SReadError : std::runtime_error
        {
            using std::runtime_error::runtime_error;
        };

        class CReader
        {
        public:
            explicit CReader(std::vector<std::string>& warnings) : m_warnings(warnings) {}

            [[noreturn]] void Fail(const std::string& where, const std::string& message) const
            {
                throw SReadError(where + ": " + message);
            }

            void Warn(const std::string& where, const std::string& message)
            {
                m_warnings.push_back(where + ": " + message);
            }

            // Prueft, dass value ein Objekt ist, und meldet unbekannte Felder.
            void Object(const json& value, const std::string& where, const std::vector<std::string>& known)
            {
                if (!value.is_object())
                    Fail(where, "Objekt { ... } erwartet");
                for (auto it = value.begin(); it != value.end(); ++it)
                    if (std::find(known.begin(), known.end(), it.key()) == known.end())
                        Warn(where, "unbekanntes Feld \"" + it.key() + "\" uebergangen");
            }

            const json& Required(const json& object, const char* key, const std::string& where) const
            {
                const auto it = object.find(key);
                if (it == object.end())
                    Fail(where, std::string("Feld \"") + key + "\" fehlt");
                return *it;
            }

            // Feld oder nullptr; dann bleibt der Standardwert (mit Hinweis).
            const json* Optional(const json& object, const char* key, const std::string& where)
            {
                const auto it = object.find(key);
                if (it == object.end())
                {
                    Warn(where, std::string("Feld \"") + key + "\" fehlt, Standardwert genommen");
                    return nullptr;
                }
                return &*it;
            }

            double Number(const json& value, const std::string& where) const
            {
                if (!value.is_number())
                    Fail(where, "Zahl erwartet");
                return value.get<double>();
            }

            // Ganze Zahl >= 0 (auch als 3.0 geschrieben).
            std::uint64_t Count(const json& value, const std::string& where) const
            {
                if (value.is_number_unsigned())
                    return value.get<std::uint64_t>();
                if (value.is_number_float())
                {
                    const double x = value.get<double>();
                    if (x >= 0.0 && x < 1.8e19 && std::floor(x) == x)
                        return static_cast<std::uint64_t>(x);
                }
                Fail(where, "ganze Zahl >= 0 erwartet");
            }

            std::string Text(const json& value, const std::string& where) const
            {
                if (!value.is_string())
                    Fail(where, "Text in \"...\" erwartet");
                return value.get<std::string>();
            }

            template <typename E, size_t N>
            E Enum(const json& value, const std::string& where, const SEnumKey<E> (&table)[N]) const
            {
                const std::string key = Text(value, where);
                std::string allowed;
                for (const SEnumKey<E>& entry : table)
                {
                    if (key == entry.key)
                        return entry.value;
                    allowed += (allowed.empty() ? "" : ", ") + std::string(entry.key);
                }
                Fail(where, "\"" + key + "\" unbekannt (moeglich: " + allowed + ")");
            }

            // Optionale Felder direkt in ein Ziel lesen; fehlt das Feld,
            // bleibt der bisherige Wert.
            void ReadNumber(const json& object, const char* key, const std::string& where, double& target)
            {
                if (const json* value = Optional(object, key, where))
                    target = Number(*value, where + ", " + key);
            }

            void ReadCount(const json& object, const char* key, const std::string& where, size_t& target)
            {
                if (const json* value = Optional(object, key, where))
                    target = static_cast<size_t>(Count(*value, where + ", " + key));
            }

            void ReadBool(const json& object, const char* key, const std::string& where, bool& target)
            {
                if (const json* value = Optional(object, key, where))
                {
                    if (!value->is_boolean())
                        Fail(where + ", " + key, "true oder false erwartet");
                    target = value->get<bool>();
                }
            }

            void ReadText(const json& object, const char* key, const std::string& where, std::string& target)
            {
                if (const json* value = Optional(object, key, where))
                    target = Text(*value, where + ", " + key);
            }

            template <typename E, size_t N>
            void ReadEnum(const json& object, const char* key, const std::string& where,
                const SEnumKey<E> (&table)[N], E& target)
            {
                if (const json* value = Optional(object, key, where))
                    target = Enum(*value, where + ", " + key, table);
            }

            // Liste aus Paaren [a, b] (Zeitprofil, Tabelle).
            std::vector<std::pair<double, double>> Pairs(const json& value, const std::string& where) const
            {
                if (!value.is_array())
                    Fail(where, "Liste [[a, b], ...] erwartet");
                std::vector<std::pair<double, double>> pairs;
                for (const json& item : value)
                {
                    if (!item.is_array() || item.size() != 2)
                        Fail(where, "je Eintrag zwei Zahlen [a, b] erwartet");
                    pairs.emplace_back(Number(item[0], where), Number(item[1], where));
                }
                return pairs;
            }

        private:
            std::vector<std::string>& m_warnings;
        };

        void ReadDistribution(CReader& reader, const json& j, const std::string& where,
            SSizeDistribution& distribution, bool checkTable)
        {
            reader.Object(j, where, { "type", "position", "width", "points" });
            reader.ReadEnum(j, "type", where, kDistributionTypes, distribution.type);
            reader.ReadNumber(j, "position", where, distribution.position);
            reader.ReadNumber(j, "width", where, distribution.width);
            if (const json* points = reader.Optional(j, "points", where))
            {
                distribution.points.clear();
                for (const auto& pair : reader.Pairs(*points, where + ", points"))
                    distribution.points.push_back({ pair.first, pair.second });
            }
            if (checkTable && distribution.type == EDistributionType::Table)
            {
                const std::string problem = distribution.CheckTable();
                if (!problem.empty())
                    reader.Fail(where, "Tabelle: " + problem);
            }
        }

        void ReadSimulation(CReader& reader, const json& simulation, CFlowsheetGraph& graph,
            SFlowsheetFileSettings& settings)
        {
            const std::string where = "simulation";
            reader.Object(simulation, where, { "duration", "dt", "seed", "runs", "representation", "sizeGrid", "cards" });
            SSimulationGrid grid = graph.GetGrid();
            double duration = grid.totalTime;
            double dt = grid.GetDt();
            reader.ReadNumber(simulation, "duration", where, duration);
            reader.ReadNumber(simulation, "dt", where, dt);
            if (!(duration > 0.0))
                reader.Fail(where + ", duration", "muss groesser als 0 sein");
            if (!(dt > 0.0))
                reader.Fail(where + ", dt", "muss groesser als 0 sein");
            if (const json* seed = reader.Optional(simulation, "seed", where))
                grid.randomSeed = reader.Count(*seed, where + ", seed");
            reader.ReadCount(simulation, "runs", where, settings.runs);
            if (settings.runs < 1)
                reader.Fail(where + ", runs", "mindestens 1");
            reader.ReadEnum(simulation, "representation", where, kRepresentations, grid.representation);

            if (const json* sizes = reader.Optional(simulation, "sizeGrid", where))
            {
                const std::string at = where + ", sizeGrid";
                reader.Object(*sizes, at, { "type", "classes", "dMin", "dMax" });
                reader.ReadEnum(*sizes, "type", at, kSizeGridTypes, grid.sizeGridType);
                reader.ReadCount(*sizes, "classes", at, grid.numSizeClasses);
                reader.ReadNumber(*sizes, "dMin", at, grid.sizeMin);
                reader.ReadNumber(*sizes, "dMax", at, grid.sizeMax);
                if (!grid.GetSizeGrid().IsValid())
                    reader.Fail(at, "ungueltig (mindestens 2 Klassen, 0 <= dMin < dMax, logarithmisch dMin > 0)");
            }
            if (const json* cards = reader.Optional(simulation, "cards", where))
            {
                const std::string at = where + ", cards";
                SCardSettings& c = grid.cards;
                reader.Object(*cards, at, { "perSlice", "rule", "numberShare", "exponent", "sliceLimits", "upperFactor", "splitFactor" });
                reader.ReadCount(*cards, "perSlice", at, c.cardsPerSlice);
                reader.ReadEnum(*cards, "rule", at, kCardRules, c.rule);
                reader.ReadNumber(*cards, "numberShare", at, c.numberShare);
                reader.ReadNumber(*cards, "exponent", at, c.exponent);
                reader.ReadBool(*cards, "sliceLimits", at, c.sliceLimits);
                reader.ReadNumber(*cards, "upperFactor", at, c.upperFactor);
                reader.ReadNumber(*cards, "splitFactor", at, c.splitFactor);
                if (c.cardsPerSlice < 1 || !(c.numberShare >= 0.0 && c.numberShare <= 1.0) || !(c.exponent > 0.0)
                    || !(c.upperFactor > 0.0) || !(c.splitFactor > 0.0))
                    reader.Fail(at, "ungueltig (perSlice >= 1, numberShare 0 ... 1, exponent, upperFactor und splitFactor > 0)");
            }

            graph.ConfigureGrid(grid.numSizeClasses, grid.sizeMin, grid.sizeMax, grid.numTimeSteps, duration, grid.sizeGridType);
            graph.ConfigureTime(duration, dt);
            graph.SetRandomSeed(grid.randomSeed);
            graph.SetRepresentation(grid.representation);
            graph.SetCardSettings(grid.cards);
        }

        void ReadInlet(CReader& reader, const json& s, const std::string& where, CInletUnit& inlet)
        {
            reader.Object(s, where, { "material", "temperature", "pressure", "massFlow", "sizeDistribution" });
            std::string material = inlet.GetMaterial();
            reader.ReadText(s, "material", where, material);
            inlet.SetMaterial(material);
            double temperature = inlet.GetTemperature();
            reader.ReadNumber(s, "temperature", where, temperature);
            inlet.SetTemperature(temperature);
            double pressure = inlet.GetPressure();
            reader.ReadNumber(s, "pressure", where, pressure);
            inlet.SetPressure(pressure);
            if (const json* profile = reader.Optional(s, "massFlow", where))
            {
                std::vector<SInletTimePoint> points;
                for (const auto& pair : reader.Pairs(*profile, where + ", massFlow"))
                {
                    if (!points.empty() && pair.first < points.back().time)
                        reader.Fail(where + ", massFlow", "Zeiten muessen aufsteigen");
                    points.push_back({ pair.first, pair.second });
                }
                inlet.SetTimeProfile(points);
            }
            if (const json* distribution = reader.Optional(s, "sizeDistribution", where))
            {
                SSizeDistribution d = inlet.GetSizeDistribution();
                ReadDistribution(reader, *distribution, where + ", sizeDistribution", d, true);
                inlet.SetSizeDistribution(d);
            }
        }

        void ReadSplitter(CReader& reader, const json& s, const std::string& where, CSplitterUnit& splitter)
        {
            reader.Object(s, where, { "fractions", "stochastic", "fluctuations" });
            if (const json* fractions = reader.Optional(s, "fractions", where))
            {
                if (!fractions->is_array())
                    reader.Fail(where + ", fractions", "Liste [ ... ] erwartet");
                std::vector<double> values;
                for (const json& item : *fractions)
                    values.push_back(reader.Number(item, where + ", fractions"));
                if (!splitter.SetFractions(values))
                    reader.Fail(where + ", fractions", "je Ausgang ein Anteil >= 0, zusammen 1");
            }
            bool stochastic = splitter.IsStochastic();
            reader.ReadBool(s, "stochastic", where, stochastic);
            splitter.SetStochastic(stochastic);
            if (const json* fluctuations = reader.Optional(s, "fluctuations", where))
            {
                const std::string at = where + ", fluctuations";
                if (!fluctuations->is_array())
                    reader.Fail(at, "Liste [ ... ] erwartet");
                std::vector<SSplitterFluctuation> list;
                for (const json& item : *fluctuations)
                {
                    SSplitterFluctuation f;
                    reader.Object(item, at, { "sigma", "tau" });
                    reader.ReadNumber(item, "sigma", at, f.sigma);
                    reader.ReadNumber(item, "tau", at, f.tau);
                    list.push_back(f);
                }
                if (!splitter.SetFluctuations(list))
                    reader.Fail(at, "je Ausgang ausser dem letzten ein Eintrag mit sigma >= 0 und tau >= 0");
            }
        }

        void ReadProcess(CReader& reader, const json& s, const std::string& where, const std::string& nodeId,
            CFlowsheetGraph& graph)
        {
            reader.Object(s, where, { "model", "parameters" });
            const IStreamProcessor* current = graph.GetProcessor(nodeId);
            std::string model = current ? current->GetTypeName() : std::string();
            reader.ReadText(s, "model", where, model);
            if (!current || model != current->GetTypeName())
                if (!graph.SetProcessorType(nodeId, model))
                {
                    std::string known;
                    for (const std::string& name : graph.GetProcessorRegistry().GetAvailableTypeNames())
                        known += (known.empty() ? "" : ", ") + name;
                    reader.Fail(where + ", model", "Modell \"" + model + "\" unbekannt (moeglich: " + known + ")");
                }
            IStreamProcessor* processor = graph.GetProcessor(nodeId);
            const json* parameters = processor ? reader.Optional(s, "parameters", where) : nullptr;
            if (!parameters)
                return;
            const std::string at = where + ", parameters";
            std::vector<std::string> ids;
            for (const SParameterDescriptor& descriptor : processor->GetParameterDescriptors())
                ids.push_back(descriptor.id);
            reader.Object(*parameters, at, ids);
            for (const SParameterDescriptor& descriptor : processor->GetParameterDescriptors())
            {
                double value = processor->GetParameterValue(descriptor.id);
                reader.ReadNumber(*parameters, descriptor.id.c_str(), at, value);
                if (value < descriptor.minValue || value > descriptor.maxValue)
                    reader.Warn(at + ", " + descriptor.id, "ausserhalb des Bereichs der Oberflaeche");
                processor->SetParameter(descriptor.id, value);
            }
        }

        void ReadVessel(CReader& reader, const json& s, const std::string& where, CVesselUnit& vessel)
        {
            reader.Object(s, where, { "mixing", "residenceTime", "batch", "cells", "coreShare", "coreTime", "fill",
                "fillMass", "fillMaterial", "fillDistribution", "fillTemperature", "fillPressure", "stockFactor" });
            SVesselSettings v = vessel.GetSettings();
            reader.ReadEnum(s, "mixing", where, kVesselMixings, v.mixing);
            reader.ReadNumber(s, "residenceTime", where, v.residenceTime);
            reader.ReadBool(s, "batch", where, v.batch);
            reader.ReadCount(s, "cells", where, v.cells);
            reader.ReadNumber(s, "coreShare", where, v.coreShare);
            reader.ReadNumber(s, "coreTime", where, v.coreTime);
            reader.ReadEnum(s, "fill", where, kVesselFills, v.fill);
            reader.ReadNumber(s, "fillMass", where, v.fillMass);
            reader.ReadText(s, "fillMaterial", where, v.fillMaterial);
            if (const json* distribution = reader.Optional(s, "fillDistribution", where))
                ReadDistribution(reader, *distribution, where + ", fillDistribution", v.fillDistribution, false);
            reader.ReadNumber(s, "fillTemperature", where, v.fillTemperature);
            reader.ReadNumber(s, "fillPressure", where, v.fillPressure);
            reader.ReadNumber(s, "stockFactor", where, v.stockFactor);
            if (!vessel.SetSettings(v))
                reader.Fail(where, v.Check());
        }

        void ReadScreen(CReader& reader, const json& s, const std::string& where, CScreenUnit& screen)
        {
            reader.Object(s, where, { "model", "cutSize", "alpha", "beta", "offset" });
            SScreenSettings v = screen.GetSettings();
            reader.ReadEnum(s, "model", where, kScreenModels, v.model);
            reader.ReadNumber(s, "cutSize", where, v.cutSize);
            reader.ReadNumber(s, "alpha", where, v.alpha);
            reader.ReadNumber(s, "beta", where, v.beta);
            reader.ReadNumber(s, "offset", where, v.offset);
            if (!screen.SetSettings(v))
                reader.Fail(where, v.Check());
        }

        void ReadUnit(CReader& reader, const json& unit, size_t index, CFlowsheetGraph& graph)
        {
            const std::string at = "Unit " + std::to_string(index + 1);
            if (!unit.is_object())
                reader.Fail(at, "Objekt { ... } erwartet");
            const std::string id = reader.Text(reader.Required(unit, "id", at), at + ", id");
            const ENodeType type = reader.Enum(reader.Required(unit, "type", at), at + ", type", kNodeTypes);
            std::string name;
            if (const auto it = unit.find("name"); it != unit.end())
                name = reader.Text(*it, at + ", name");
            const std::string where = at + " (" + (name.empty() ? id : name) + ")";

            std::vector<std::string> known = { "id", "type", "name", "position" };
            if (type == ENodeType::Splitter)
                known.push_back("outputs");
            if (type == ENodeType::Merger)
                known.push_back("inputs");
            const bool hasSettings = type == ENodeType::Inlet || type == ENodeType::Splitter
                || type == ENodeType::Process || type == ENodeType::Vessel || type == ENodeType::Screen;
            if (hasSettings)
                known.push_back("settings");
            reader.Object(unit, where, known);

            if (unit.find("name") == unit.end())
            {
                name = graph.MakeDefaultNodeName(type);
                reader.Warn(where, "Feld \"name\" fehlt, Name " + name + " vergeben");
            }
            double posX = 0.0;
            double posY = 0.0;
            if (const json* position = reader.Optional(unit, "position", where))
            {
                if (!position->is_array() || position->size() != 2)
                    reader.Fail(where + ", position", "[x, y] erwartet");
                posX = reader.Number((*position)[0], where + ", position");
                posY = reader.Number((*position)[1], where + ", position");
            }
            size_t ports = 2;
            if (type == ENodeType::Splitter)
                reader.ReadCount(unit, "outputs", where, ports);
            if (type == ENodeType::Merger)
                reader.ReadCount(unit, "inputs", where, ports);
            if (graph.FindNode(id))
                reader.Fail(where, "ID \"" + id + "\" kommt doppelt vor");
            if (graph.AddNodeWithId(id, type, name, ports, posX, posY).empty())
                reader.Fail(where, "laesst sich nicht anlegen (leere ID oder keine Ports)");

            const json* settings = hasSettings ? reader.Optional(unit, "settings", where) : nullptr;
            if (!settings)
                return;
            const std::string in = where + ", settings";
            switch (type)
            {
            case ENodeType::Inlet:
                ReadInlet(reader, *settings, in, *graph.GetInlet(id));
                break;
            case ENodeType::Splitter:
                ReadSplitter(reader, *settings, in, *graph.GetSplitter(id));
                break;
            case ENodeType::Process:
                ReadProcess(reader, *settings, in, id, graph);
                break;
            case ENodeType::Vessel:
                ReadVessel(reader, *settings, in, *graph.GetVessel(id));
                break;
            case ENodeType::Screen:
                ReadScreen(reader, *settings, in, *graph.GetScreen(id));
                break;
            default:
                break;
            }
        }

        // Liest eine Verbindung; liefert ihre ID und was in der Datei unter
        // "recycle" steht (Unknown bei true/false aus Formatversion 1 oder
        // wenn das Feld fehlt).
        std::pair<std::string, ECut> ReadConnection(CReader& reader, const json& connection, size_t index,
            CFlowsheetGraph& graph)
        {
            const std::string where = "Verbindung " + std::to_string(index + 1);
            reader.Object(connection, where, { "from", "output", "to", "input", "recycle" });
            const std::string from = reader.Text(reader.Required(connection, "from", where), where + ", from");
            const std::uint64_t output = reader.Count(reader.Required(connection, "output", where), where + ", output");
            const std::string to = reader.Text(reader.Required(connection, "to", where), where + ", to");
            const std::uint64_t input = reader.Count(reader.Required(connection, "input", where), where + ", input");
            ECut cut = ECut::Unknown;
            bool manual = false;
            if (const json* recycle = reader.Optional(connection, "recycle", where))
            {
                if (recycle->is_boolean())
                    manual = recycle->get<bool>(); // Formatversion 1: true = von Hand markiert
                else
                {
                    cut = reader.Enum(*recycle, where + ", recycle", kCuts);
                    manual = cut == ECut::Manual;
                }
            }
            const std::string id = (output >= 1 && input >= 1)
                ? graph.AddConnection(from, static_cast<size_t>(output - 1), to, static_cast<size_t>(input - 1))
                : std::string();
            if (id.empty())
                reader.Fail(where, from + " Ausgang " + std::to_string(output) + " -> " + to + " Eingang "
                    + std::to_string(input) + " geht nicht (Unit unbekannt, Port nicht vorhanden oder schon belegt)");
            if (manual)
                graph.SetConnectionRecycle(id, true);
            return { id, cut };
        }
    }

    std::string WriteFlowsheetJson(const CFlowsheetGraph& graph, const SFlowsheetFileSettings& settings)
    {
        json root = json::object();
        root["format"] = kFlowsheetFormatName;
        root["version"] = kFlowsheetFormatVersion;
        root["program"] = std::string("StoSiS ") + STOSIS_VERSION;
        root["info"] = "SI-Einheiten (Groessen in m, Zeiten in s, Massenstrom in kg/s, Masse in kg, Temperatur in K, "
                       "Druck in Pa). Ausgaenge und Eingaenge ab 1 gezaehlt. Stoffe ueber ihren Namen in Materialien.json. "
                       "recycle: manual = von Hand als Rueckfuehrung markiert, auto = vom Graphen aufgeschnitten (beim "
                       "Laden neu bestimmt), no = nicht aufgeschnitten.";
        root["simulation"] = SimulationToJson(graph, settings);
        json execution = json::object();
        execution["detailedLog"] = graph.IsDetailedLog();
        execution["cardHistory"] = graph.IsRecordCardHistory();
        root["execution"] = std::move(execution);
        json units = json::array();
        for (const SFlowsheetNode& node : graph.GetNodes())
            units.push_back(UnitToJson(graph, node));
        root["units"] = std::move(units);
        json connections = json::array();
        const std::set<std::string> lagged = graph.GetLaggedConnectionIds();
        for (const SFlowsheetConnection& c : graph.GetConnections())
        {
            json item = json::object();
            item["from"] = c.sourceNodeId;
            item["output"] = c.sourcePort + 1;
            item["to"] = c.targetNodeId;
            item["input"] = c.targetPort + 1;
            item["recycle"] = KeyOf(kCuts, c.isRecycle ? ECut::Manual : lagged.count(c.id) ? ECut::Auto : ECut::No);
            connections.push_back(std::move(item));
        }
        root["connections"] = std::move(connections);

        std::string text;
        Format(root, 0, 0, text);
        return text + "\n";
    }

    SFlowsheetReadResult ReadFlowsheetJson(const std::string& text, CFlowsheetGraph& graph)
    {
        SFlowsheetReadResult result;
        CReader reader(result.warnings);
        try
        {
            if (!graph.IsEmpty())
                reader.Fail("Flowsheet", "zum Laden muss der Graph leer sein");
            const json root = json::parse(text);
            const std::string where = "Datei";
            if (!root.is_object())
                reader.Fail(where, "Objekt { ... } erwartet");
            const json& format = reader.Required(root, "format", where);
            if (!format.is_string() || format.get<std::string>() != kFlowsheetFormatName)
                reader.Fail(where, std::string("keine StoSiS-Flowsheet-Datei (\"format\": \"") + kFlowsheetFormatName + "\" fehlt)");
            const std::uint64_t version = reader.Count(reader.Required(root, "version", where), where + ", version");
            if (version < 1 || version > static_cast<std::uint64_t>(kFlowsheetFormatVersion))
                reader.Fail(where, "Formatversion " + std::to_string(version) + " kann dieses Programm nicht lesen (bis "
                    + std::to_string(kFlowsheetFormatVersion) + ")");
            reader.Object(root, where, { "format", "version", "program", "info", "simulation", "execution", "units", "connections" });

            if (const json* simulation = reader.Optional(root, "simulation", where))
                ReadSimulation(reader, *simulation, graph, result.settings);
            if (const json* execution = reader.Optional(root, "execution", where))
            {
                reader.Object(*execution, "execution", { "detailedLog", "cardHistory" });
                bool detailed = graph.IsDetailedLog();
                reader.ReadBool(*execution, "detailedLog", "execution", detailed);
                graph.SetDetailedLog(detailed);
                bool cardHistory = graph.IsRecordCardHistory();
                reader.ReadBool(*execution, "cardHistory", "execution", cardHistory);
                graph.SetRecordCardHistory(cardHistory);
            }
            const json& units = reader.Required(root, "units", where);
            if (!units.is_array())
                reader.Fail(where + ", units", "Liste [ ... ] erwartet");
            for (size_t i = 0; i < units.size(); ++i)
                ReadUnit(reader, units[i], i, graph);
            const json& connections = reader.Required(root, "connections", where);
            if (!connections.is_array())
                reader.Fail(where + ", connections", "Liste [ ... ] erwartet");
            std::vector<std::pair<std::string, ECut>> cuts;
            for (size_t i = 0; i < connections.size(); ++i)
                cuts.push_back(ReadConnection(reader, connections[i], i, graph));
            // "auto" und "no" sind nur Information: Der Graph schneidet neu;
            // passt das nicht mehr zur Datei (von Hand geaendert?), ein Hinweis.
            const std::set<std::string> lagged = graph.GetLaggedConnectionIds();
            for (size_t i = 0; i < cuts.size(); ++i)
            {
                const bool cut = lagged.count(cuts[i].first) > 0;
                if (cuts[i].second == ECut::Auto && !cut)
                    reader.Warn("Verbindung " + std::to_string(i + 1),
                        "in der Datei \"auto\", der Graph schneidet hier aber nicht mehr auf (neu bestimmt)");
                else if (cuts[i].second == ECut::No && cut)
                    reader.Warn("Verbindung " + std::to_string(i + 1),
                        "in der Datei \"no\", der Graph schneidet hier jetzt auf (neu bestimmt)");
            }
            result.ok = true;
        }
        catch (const SReadError& e)
        {
            result.error = e.what();
        }
        catch (const json::exception& e)
        {
            result.error = std::string("Kein gueltiges JSON: ") + e.what();
        }
        return result;
    }

    bool SaveFlowsheetFile(const std::string& path, const CFlowsheetGraph& graph,
        const SFlowsheetFileSettings& settings, std::string& error)
    {
        // u8path: Pfade kommen als UTF-8 (z.B. aus Qt).
        std::ofstream file(std::filesystem::u8path(path), std::ios::binary);
        if (file)
            file << WriteFlowsheetJson(graph, settings);
        if (!file)
        {
            error = "Datei laesst sich nicht schreiben: " + path;
            return false;
        }
        return true;
    }

    SFlowsheetReadResult LoadFlowsheetFile(const std::string& path, CFlowsheetGraph& graph)
    {
        std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
        if (!file)
        {
            SFlowsheetReadResult result;
            result.error = "Datei laesst sich nicht oeffnen: " + path;
            return result;
        }
        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return ReadFlowsheetJson(text, graph);
    }
}
