// Materialdatei lesen und Stoffwerte bei T und p auswerten (Methoden-Dokument
// Kapitel 14.4, Pruefung Schritt 2).
#include "Materials.h"
#include <cstdio>
#include <cmath>
#include <string>

#include "TestSupport.h"

using namespace stosis;
using namespace stosis_test;

static bool Near(double a, double b, double rel) { return NearRel(a, b, rel); }

int main()
{
    const std::string path = DataFile("Materialien.json");
    CMaterialDatabase db;
    std::string error;
    const bool loaded = db.LoadFromFile(path, error);
    Check(loaded, "Materialien.json eingelesen");
    if (!loaded)
    {
        std::printf("Fehler: %s\n", error.c_str());
        return 1;
    }
    Check(db.GetMaterials().size() == 18, "18 Stoffe");
    Check(db.GetInteractions().size() == 1, "1 Wechselwirkung");

    const double T = kRoomTemperature, p = kRoomPressure;
    const SMaterial* sand = db.FindByName("Sand");
    Check(sand && sand->GetProperty("density", T, p).value == 2200.0, "Sand: Dichte 2200 kg/m3");
    Check(sand && sand->GetProperty("heat_capacity", T, p).value == 830.0, "Sand: c_p 830 J/(kg K)");
    Check(sand && sand->FindConstant("bond_work_index") && sand->FindConstant("bond_work_index")->value == 15.51, "Sand: Bond-Index 15.51");
    Check(sand && sand->state == "solid", "Sand: fest");

    const SMaterial* air = db.FindByName("Air");
    const double rhoAir = air ? air->GetProperty("density", T, p).value : 0.0;
    std::printf("      Luftdichte bei 293.15 K, 101325 Pa: %.5f kg/m3\n", rhoAir);
    Check(Near(rhoAir, 0.028951 * 101325.0 / (8.314462618 * 293.15), 1e-12) && Near(rhoAir, 1.2035, 1e-4), "Luft: ideales Gas, 1.2035 kg/m3");
    const SPropertyValue hot = air ? air->GetProperty("density", 600.0, p) : SPropertyValue{};
    Check(hot.found && hot.outOfRange && Near(hot.value, 0.028951 * 101325.0 / (8.314462618 * 498.15), 1e-12),
        "Luft bei 600 K: ausserhalb des Bereichs, Randwert 498.15 K");

    const SMaterial* n2 = db.FindByName("N2");
    const double muN2 = n2 ? n2->GetProperty("viscosity", 373.15, p).value : 0.0;
    std::printf("      N2-Viskositaet bei 373.15 K: %.4e Pa s\n", muN2);
    Check(Near(muN2, 2.1026e-5, 1e-3), "N2: Sutherland 2.10e-5 Pa s");

    const SMaterial* co2 = db.FindByName("CO2");
    Check(co2 && Near(co2->GetProperty("density", 323.0, p).value, 0.5 * (1.96456 + 1.43787), 1e-12), "CO2: Tabelle linear interpoliert (323 K)");
    const double cpCO2 = co2 ? co2->GetProperty("heat_capacity", 298.15, p).value : 0.0;
    std::printf("      CO2 c_p (abgeleitet) bei 298.15 K: %.1f J/(kg K)\n", cpCO2);
    Check(Near(cpCO2, 947.0, 1e-3), "CO2: abgeleitetes Polynom ergibt 947 J/(kg K)");

    const SMaterial* water = db.FindByName("H2O");
    const SPropertyValue p350 = water ? water->GetProperty("vapor_pressure", 350.0, p) : SPropertyValue{};
    std::printf("      Wasser-Dampfdruck bei 350 K: %.0f Pa\n", p350.value);
    Check(p350.found && !p350.outOfRange && Near(p350.value, 41680.0, 0.01), "H2O: Antoine bei 350 K etwa 41.7 kPa");
    const SPropertyValue p373 = water ? water->GetProperty("vapor_pressure", 373.15, p) : SPropertyValue{};
    std::printf("      Wasser-Dampfdruck bei 373.15 K: %.0f Pa (ausserhalb: %d, Bereich %.1f-%.1f K)\n",
        p373.value, p373.outOfRange, p373.correlation ? p373.correlation->TMin : 0.0, p373.correlation ? p373.correlation->TMax : 0.0);
    Check(p373.outOfRange && p373.correlation && p373.correlation->TMax == 373.0 && Near(p373.value, 1.0e5, 0.01),
        "H2O bei 373.15 K: zwischen den Bereichen -> naechster Eintrag (bis 373 K), etwa 1 bar");
    const SPropertyValue rhoWater = water ? water->GetProperty("density", 373.15, p) : SPropertyValue{};
    Check(rhoWater.outOfRange && rhoWater.value == 958.64, "H2O-Dichte bei 373.15 K: im Bereich, aber hinter dem letzten Messpunkt -> markiert");
    Check(co2 && !co2->GetProperty("density", 323.0, p).outOfRange, "CO2-Dichte bei 323 K: zwischen Messpunkten -> nicht markiert");
    const SPropertyValue p400 = water ? water->GetProperty("vapor_pressure", 400.0, p) : SPropertyValue{};
    Check(p400.found && !p400.outOfRange && p400.correlation->TMin == 379.0, "H2O bei 400 K: zweiter Bereich (379-573 K)");

    const SMaterial* coal = db.FindByName("Coal");
    Check(coal && !coal->GetProperty("heat_capacity", T, p).found, "Coal: keine Waermekapazitaet");
    Check(coal && coal->warnings.size() == 2, "Coal: 2 Warnungen");

    const SMaterial* milk = db.FindByName("Milk");
    const SProperty* moisture = milk ? milk->FindProperty("equilibrium_moisture_content") : nullptr;
    Check(moisture && moisture->correlations.size() == 3 && moisture->correlations[0].DependsOnHumidity(), "Milk: Gleichgewichtsfeuchte, 3 Bereiche, ueber Luftfeuchte");

    Check(db.FindByDyssolKey("4031BC62EC7F17EFA33F") == sand, "Suche ueber Dyssol-Schluessel");

    // Fehlerfaelle: der alte Inhalt muss erhalten bleiben.
    CMaterialDatabase copy = db;
    std::string syntaxError;
    const bool badSyntax = copy.LoadFromText("{\n  \"format\": \"StoSiS-Materialdatenbank\"\n  \"version\": 1\n}", syntaxError);
    std::printf("      Meldung: %s\n", syntaxError.c_str());
    Check(!badSyntax && syntaxError.find("Zeile 3") != std::string::npos && copy.GetMaterials().size() == 18,
        "Fehlendes Komma: Syntaxfehler mit Zeile, Inhalt bleibt");

    std::string fieldError;
    const bool badField = copy.LoadFromText(R"({"format": "StoSiS-Materialdatenbank", "version": 1, "materials": [
        {"name": "X", "state": {"value": "solid"}, "properties": {"density": [{"type": "constant", "unit": "kg/m3",
         "T_range": [10, 100], "p_range": [1, 2], "source": "s", "status": "unchecked"}]}}]})", fieldError);
    std::printf("      Meldung: %s\n", fieldError.c_str());
    Check(!badField && fieldError.find("Stoff 1 (X), Eigenschaft \"density\", Eintrag 1") != std::string::npos
        && fieldError.find("\"value\" fehlt") != std::string::npos, "Fehlender Wert: Meldung mit Stoff, Eigenschaft, Feld");

    std::string typeError;
    const bool badType = copy.LoadFromText(R"({"format": "StoSiS-Materialdatenbank", "version": 1, "materials": [
        {"name": "X", "state": {"value": "solid"}, "properties": {"density": [{"type": "magic", "unit": "kg/m3",
         "T_range": [10, 100], "p_range": [1, 2], "source": "s", "status": "unchecked"}]}}]})", typeError);
    Check(!badType && typeError.find("unbekannter Typ \"magic\"") != std::string::npos, "Unbekannter Typ wird gemeldet");

    std::string missingError;
    Check(!copy.LoadFromFile(DataFile("gibt_es_nicht.json"), missingError) && missingError.find("nicht gefunden") != std::string::npos,
        "Fehlende Datei wird gemeldet");

    return Summary();
}
