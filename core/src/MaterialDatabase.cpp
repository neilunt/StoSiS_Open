#include "Materials.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

// Einlesen von data/Materialien.json. Die JSON-Bibliothek (nlohmann/json,
// MIT-Lizenz) wird nur in dieser Datei gebraucht; Materials.h bleibt frei
// davon.

namespace stosis
{
    namespace
    {
        // ordered_json behaelt die Reihenfolge der Felder aus der Datei.
        using json = nlohmann::ordered_json;

        constexpr const char* kFormatName = "StoSiS-Materialdatenbank";
        constexpr int kFormatVersion = 1;

        // Inhaltlicher Fehler mit Ortsangabe ("Stoff 3 (CaO), Eigenschaft ...").
        struct SLoadError : std::runtime_error
        {
            using std::runtime_error::runtime_error;
        };

        [[noreturn]] void Fail(const std::string& where, const std::string& message)
        {
            throw SLoadError(where + ": " + message);
        }

        std::string Quoted(const char* key) { return std::string("Feld \"") + key + "\""; }

        const json& Field(const json& object, const char* key, const std::string& where)
        {
            const auto it = object.find(key);
            if (it == object.end())
                Fail(where, Quoted(key) + " fehlt");
            return *it;
        }

        double ToNumber(const json& value, const std::string& what, const std::string& where)
        {
            if (!value.is_number())
                Fail(where, what + " ist keine Zahl");
            const double number = value.get<double>();
            if (!std::isfinite(number))
                Fail(where, what + " ist keine endliche Zahl");
            return number;
        }

        double Number(const json& object, const char* key, const std::string& where)
        {
            return ToNumber(Field(object, key, where), Quoted(key), where);
        }

        std::string Text(const json& object, const char* key, const std::string& where)
        {
            const json& value = Field(object, key, where);
            if (!value.is_string())
                Fail(where, Quoted(key) + " ist kein Text");
            return value.get<std::string>();
        }

        std::string OptionalText(const json& object, const char* key, const std::string& where)
        {
            return object.contains(key) ? Text(object, key, where) : std::string();
        }

        std::vector<std::string> OptionalTextList(const json& object, const char* key, const std::string& where)
        {
            std::vector<std::string> list;
            if (!object.contains(key))
                return list;
            const json& array = object.at(key);
            if (!array.is_array())
                Fail(where, Quoted(key) + " ist keine Liste");
            for (const json& item : array)
            {
                if (!item.is_string())
                    Fail(where, Quoted(key) + " enthaelt etwas anderes als Text");
                list.push_back(item.get<std::string>());
            }
            return list;
        }

        std::pair<double, double> Range(const json& object, const char* key, const std::string& where)
        {
            const json& range = Field(object, key, where);
            if (!range.is_array() || range.size() != 2)
                Fail(where, Quoted(key) + " muss [min, max] sein");
            const double low = ToNumber(range[0], Quoted(key), where);
            const double high = ToNumber(range[1], Quoted(key), where);
            if (low > high)
                Fail(where, Quoted(key) + ": min ist groesser als max");
            return { low, high };
        }

        std::vector<double> Coefficients(const json& object, const std::string& where)
        {
            const json& array = Field(object, "coefficients", where);
            if (!array.is_array() || array.empty())
                Fail(where, "Feld \"coefficients\" muss eine nicht leere Liste sein");
            std::vector<double> values;
            for (const json& item : array)
                values.push_back(ToNumber(item, "Feld \"coefficients\"", where));
            return values;
        }

        std::vector<std::pair<double, double>> Points(const json& object, const std::string& where)
        {
            const json& array = Field(object, "points", where);
            if (!array.is_array() || array.empty())
                Fail(where, "Feld \"points\" muss eine nicht leere Liste sein");
            std::vector<std::pair<double, double>> points;
            for (const json& point : array)
            {
                if (!point.is_array() || point.size() != 2)
                    Fail(where, "jeder Punkt in \"points\" muss [x, Wert] sein");
                points.emplace_back(ToNumber(point[0], "Punkt", where), ToNumber(point[1], "Punkt", where));
            }
            std::sort(points.begin(), points.end());
            for (size_t i = 1; i < points.size(); ++i)
                if (points[i].first == points[i - 1].first)
                    Fail(where, "zwei Punkte mit gleichem x-Wert " + std::to_string(points[i].first));
            return points;
        }

        SCorrelation ReadCorrelation(const json& entry, const std::string& where)
        {
            if (!entry.is_object())
                Fail(where, "ist kein Objekt");

            SCorrelation c;
            c.typeName = Text(entry, "type", where);
            const std::string& type = c.typeName;
            if (type == "constant")
            {
                c.type = ECorrelationType::Constant;
                c.parameters = { Number(entry, "value", where) };
            }
            else if (type == "table_T" || type == "table_relative_humidity")
            {
                c.type = type == "table_T" ? ECorrelationType::TableT : ECorrelationType::TableRelativeHumidity;
                c.points = Points(entry, where);
            }
            else if (type == "ideal_gas")
            {
                c.type = ECorrelationType::IdealGas;
                c.parameters = { Number(entry, "molar_mass", where) };
                if (c.parameters[0] <= 0.0)
                    Fail(where, "Feld \"molar_mass\" muss positiv sein");
            }
            else if (type == "sutherland")
            {
                c.type = ECorrelationType::Sutherland;
                c.parameters = { Number(entry, "y_ref", where), Number(entry, "T_ref", where), Number(entry, "S", where) };
                if (c.parameters[1] <= 0.0)
                    Fail(where, "Feld \"T_ref\" muss positiv sein");
            }
            else if (type == "antoine")
            {
                c.type = ECorrelationType::Antoine;
                c.parameters = { Number(entry, "A", where), Number(entry, "B", where), Number(entry, "C", where) };
            }
            else if (type == "polynomial")
            {
                c.type = ECorrelationType::Polynomial;
                c.parameters = Coefficients(entry, where);
            }
            else if (type == "polynomial_cp")
            {
                c.type = ECorrelationType::PolynomialCp;
                c.parameters = Coefficients(entry, where);
                if (c.parameters.size() != 5)
                    Fail(where, "\"polynomial_cp\" braucht genau 5 Koeffizienten (a, b, c, d, e)");
            }
            else
            {
                Fail(where, "unbekannter Typ \"" + type + "\"");
            }

            c.formula = OptionalText(entry, "formula", where);
            c.unit = Text(entry, "unit", where);
            std::tie(c.TMin, c.TMax) = Range(entry, "T_range", where);
            if (c.TMin <= 0.0)
                Fail(where, "\"T_range\" muss oberhalb von 0 K liegen");
            std::tie(c.pMin, c.pMax) = Range(entry, c.DependsOnHumidity() ? "relative_humidity_range" : "p_range", where);
            c.source = Text(entry, "source", where);
            c.status = Text(entry, "status", where);
            c.reference = OptionalText(entry, "reference", where);
            c.note = OptionalText(entry, "note", where);
            return c;
        }

        std::vector<std::pair<std::string, SProperty>> ReadProperties(const json& entry, const std::string& where)
        {
            std::vector<std::pair<std::string, SProperty>> properties;
            if (!entry.contains("properties"))
                return properties;
            const json& object = entry.at("properties");
            if (!object.is_object())
                Fail(where, "Feld \"properties\" ist kein Objekt");
            for (auto it = object.begin(); it != object.end(); ++it)
            {
                const std::string propertyWhere = where + ", Eigenschaft \"" + it.key() + "\"";
                if (!it.value().is_array() || it.value().empty())
                    Fail(propertyWhere, "muss eine nicht leere Liste von Eintraegen sein");
                SProperty property;
                size_t index = 1;
                for (const json& correlation : it.value())
                    property.correlations.push_back(
                        ReadCorrelation(correlation, propertyWhere + ", Eintrag " + std::to_string(index++)));
                properties.emplace_back(it.key(), std::move(property));
            }
            return properties;
        }

        SMaterial ReadMaterial(const json& entry, size_t index)
        {
            std::string where = "Stoff " + std::to_string(index);
            if (!entry.is_object())
                Fail(where, "ist kein Objekt");

            SMaterial material;
            material.name = Text(entry, "name", where);
            where += " (" + material.name + ")";
            material.dyssolKey = OptionalText(entry, "dyssol_key", where);

            const json& state = Field(entry, "state", where);
            if (!state.is_object())
                Fail(where, "Feld \"state\" ist kein Objekt");
            material.state = Text(state, "value", where + ", state");
            material.stateSource = OptionalText(state, "source", where + ", state");

            material.warnings = OptionalTextList(entry, "warnings", where);
            material.notes = OptionalTextList(entry, "notes", where);

            if (entry.contains("constants"))
            {
                const json& constants = entry.at("constants");
                if (!constants.is_object())
                    Fail(where, "Feld \"constants\" ist kein Objekt");
                for (auto it = constants.begin(); it != constants.end(); ++it)
                {
                    const std::string constantWhere = where + ", Konstante \"" + it.key() + "\"";
                    if (!it.value().is_object())
                        Fail(constantWhere, "ist kein Objekt");
                    SConstant constant;
                    constant.value = Number(it.value(), "value", constantWhere);
                    constant.unit = Text(it.value(), "unit", constantWhere);
                    constant.source = Text(it.value(), "source", constantWhere);
                    constant.status = Text(it.value(), "status", constantWhere);
                    constant.reference = OptionalText(it.value(), "reference", constantWhere);
                    material.constants.emplace_back(it.key(), std::move(constant));
                }
            }

            material.properties = ReadProperties(entry, where);
            return material;
        }

        // "[json.exception.parse_error.101] parse error at line 3, column 5: ..."
        // -> "Syntaxfehler in Zeile 3, Spalte 5 (...)"
        std::string DescribeParseError(const std::string& message)
        {
            const size_t linePos = message.find("at line ");
            const size_t columnPos = message.find(", column ", linePos);
            const size_t detail = message.find(": ", columnPos);
            if (linePos == std::string::npos || columnPos == std::string::npos)
                return "Syntaxfehler: " + message;
            const unsigned long line = std::strtoul(message.c_str() + linePos + 8, nullptr, 10);
            const unsigned long column = std::strtoul(message.c_str() + columnPos + 9, nullptr, 10);
            return "Syntaxfehler in Zeile " + std::to_string(line) + ", Spalte " + std::to_string(column) +
                (detail != std::string::npos ? " (" + message.substr(detail + 2) + ")" : std::string());
        }
    }

    bool CMaterialDatabase::LoadFromText(const std::string& text, std::string& error)
    {
        try
        {
            const json document = json::parse(text);
            if (!document.is_object())
                Fail("Datei", "oberste Ebene ist kein Objekt");
            if (Text(document, "format", "Datei") != kFormatName)
                Fail("Datei", std::string("Feld \"format\" muss \"") + kFormatName + "\" sein");
            if (Number(document, "version", "Datei") != kFormatVersion)
                Fail("Datei", "unbekannte Version (erwartet " + std::to_string(kFormatVersion) + ")");

            const json& materialList = Field(document, "materials", "Datei");
            if (!materialList.is_array())
                Fail("Datei", "Feld \"materials\" ist keine Liste");

            std::vector<SMaterial> materials;
            std::set<std::string> names;
            for (const json& entry : materialList)
            {
                materials.push_back(ReadMaterial(entry, materials.size() + 1));
                if (!names.insert(materials.back().name).second)
                    Fail("Stoff " + std::to_string(materials.size()), "Name \"" + materials.back().name + "\" kommt doppelt vor");
            }

            std::vector<SInteraction> interactions;
            if (document.contains("interactions"))
            {
                const json& list = document.at("interactions");
                if (!list.is_array())
                    Fail("Datei", "Feld \"interactions\" ist keine Liste");
                for (const json& entry : list)
                {
                    const std::string where = "Wechselwirkung " + std::to_string(interactions.size() + 1);
                    const json& pair = Field(entry, "materials", where);
                    if (!pair.is_array() || pair.size() != 2 || !pair[0].is_string() || !pair[1].is_string())
                        Fail(where, "Feld \"materials\" muss zwei Stoffnamen enthalten");
                    SInteraction interaction;
                    interaction.material1 = pair[0].get<std::string>();
                    interaction.material2 = pair[1].get<std::string>();
                    for (const std::string& name : { interaction.material1, interaction.material2 })
                        if (!names.count(name))
                            Fail(where, "unbekannter Stoff \"" + name + "\"");
                    interaction.properties = ReadProperties(entry, where);
                    interactions.push_back(std::move(interaction));
                }
            }

            m_materials = std::move(materials);
            m_interactions = std::move(interactions);
            return true;
        }
        catch (const json::parse_error& e)
        {
            error = DescribeParseError(e.what());
        }
        catch (const SLoadError& e)
        {
            error = e.what();
        }
        catch (const json::exception& e)
        {
            error = e.what();
        }
        return false;
    }

    bool CMaterialDatabase::LoadFromFile(const std::string& path, std::string& error)
    {
        // u8path: Pfade kommen als UTF-8 (z.B. aus Qt), Windows braucht sonst
        // die lokale Codepage.
        std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
        if (!file)
        {
            error = "Datei nicht gefunden: " + path;
            return false;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        if (!LoadFromText(buffer.str(), error))
        {
            error = path + ": " + error;
            return false;
        }
        m_filePath = path;
        return true;
    }
}
