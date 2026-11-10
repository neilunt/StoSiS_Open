// Flowsheet als Datei und Struktur (Methodenhandbuch 3.3, 3.4):
// Rundlauftest (schreiben, lesen, wieder schreiben = derselbe Text; gleiche
// Ergebnisse bei gleichem Seed), jede Einstellung abweichend vom Standard,
// Fehler und Hinweise beim Lesen, Rechenreihenfolge, Kreise, Schnitte und
// der Baum fuer das Fenster "Struktur".
#include "FlowsheetFile.h"
#include "TestSupport.h"
#include <filesystem>

using namespace stosis;
using namespace stosis_test;

namespace
{
    // Alle Unit-Typen mit Einstellungen abseits des Standards. Kontinuum:
    // mit Prozess (Verzoegerung), der Graph schneidet den Kreis selbst auf.
    // Partikelmodus: ohne Prozess (rechnet noch keine Karten), die
    // Rueckfuehrung ist von Hand markiert.
    void BuildAll(CFlowsheetGraph& g, bool particles)
    {
        g.ConfigureGrid(80, 2e-6, 800e-6, 2, 12.0, ESizeGridType::Logarithmic);
        g.ConfigureTime(12.0, 0.1);
        g.SetRandomSeed(4242);
        SCardSettings cards;
        cards.cardsPerSlice = 300;
        cards.rule = ECardDensityRule::Product;
        cards.numberShare = 0.3;
        cards.exponent = 0.7;
        cards.sliceLimits = true;
        cards.upperFactor = 3.0;
        cards.splitFactor = 4.0;
        g.SetCardSettings(cards);
        g.SetRepresentation(particles ? ERepresentation::Particles : ERepresentation::Continuum);
        g.SetDetailedLog(true);

        const std::string a = g.AddInletNode("Zulauf A", 0, 10);
        g.GetInlet(a)->SetTimeProfile({ {0, 2}, {3, 6}, {8, 1}, {12, 4} });
        SSizeDistribution rrsb;
        rrsb.type = EDistributionType::RRSB;
        rrsb.position = 120e-6;
        rrsb.width = 2.5;
        g.GetInlet(a)->SetSizeDistribution(rrsb);
        g.GetInlet(a)->SetMaterial("Sand");
        g.GetInlet(a)->SetTemperature(350.0);
        g.GetInlet(a)->SetPressure(2.0e5);

        const std::string b = g.AddInletNode("Zulauf B", 0, 90);
        g.GetInlet(b)->SetTimeProfile({ {0, 1}, {12, 1.5} });
        SSizeDistribution table;
        table.type = EDistributionType::Table;
        table.points = { {20e-6, 0.0}, {60e-6, 0.3}, {150e-6, 0.8}, {400e-6, 1.0} };
        g.GetInlet(b)->SetSizeDistribution(table);
        g.GetInlet(b)->SetMaterial("Quarz");
        g.GetInlet(b)->SetTemperature(300.0);
        g.GetInlet(b)->SetPressure(1.5e5);

        const std::string mg = g.AddMergerNode("Merger1", 3, 120, 40);
        const std::string vs = g.AddVesselNode("Behaelter1", 240, 40);
        SVesselSettings v;
        v.mixing = EVesselMixing::Cascade;
        v.residenceTime = 2.5;
        v.cells = 4;
        v.coreShare = 0.5;
        v.coreTime = 0.4;
        v.fill = EVesselFill::Prescribed;
        v.fillMass = 3.0;
        v.fillMaterial = "Quarz";
        v.fillDistribution.position = 90e-6;
        v.fillDistribution.width = 0.4;
        v.fillTemperature = 310.0;
        v.fillPressure = 1.2e5;
        v.stockFactor = 40.0;
        g.GetVessel(vs)->SetSettings(v);

        const std::string sp = g.AddSplitterNode("Splitter1", 3, 360, 40);
        g.GetSplitter(sp)->SetFractions({ 0.5, 0.2, 0.3 });
        g.GetSplitter(sp)->SetStochastic(true);
        g.GetSplitter(sp)->SetFluctuations({ {0.04, 0.6}, {0.03, 0.0} });
        const std::string p1 = g.AddOutletNode("Produkt", 500, 0);
        const std::string p2 = g.AddOutletNode("Abfall", 500, 120);

        g.AddConnection(a, 0, mg, 0);
        g.AddConnection(b, 0, mg, 1);
        g.AddConnection(mg, 0, vs, 0);
        g.AddConnection(vs, 0, sp, 0);
        g.AddConnection(sp, 0, p1, 0);
        g.AddConnection(sp, 1, p2, 0);
        if (particles)
        {
            g.SetConnectionRecycle(g.AddConnection(sp, 2, mg, 2), true);
            return;
        }
        const std::string pr = g.AddProcessNode("Verzoegerung1", 360, 160);
        g.GetProcessor(pr)->SetParameter("delayRef", 0.7);
        g.GetProcessor(pr)->SetParameter("referenceSize", 80e-6);
        g.GetProcessor(pr)->SetParameter("sizeExponent", -0.5);
        g.AddConnection(sp, 2, pr, 0);
        g.AddConnection(pr, 0, mg, 2);
    }

    std::string NameOf(const CFlowsheetGraph& g, const std::string& id)
    {
        const SFlowsheetNode* node = g.FindNode(id);
        return node ? node->name : "?" + id;
    }

    void DescribeBranch(const CFlowsheetGraph& g, const SStructureBranch& branch, std::string& text)
    {
        const char* kind = branch.kind == SStructureBranch::EKind::Unit ? "" : branch.kind == SStructureBranch::EKind::Cut ? "Schnitt " : "Verweis ";
        text += std::string("(") + kind + NameOf(g, branch.nodeId);
        for (const SStructureBranch& child : branch.children)
            DescribeBranch(g, child, text);
        text += ")";
    }

    // Struktur als Text mit Namen statt IDs: Reihenfolge, Schnitte, Kreise, Baum.
    std::string Describe(const CFlowsheetGraph& g)
    {
        const SFlowsheetStructure s = g.GetStructure();
        std::string text = "Reihenfolge:";
        for (const std::string& id : s.order)
            text += " " + NameOf(g, id);
        text += " | Schnitte:";
        for (const SFlowsheetConnection& c : g.GetConnections())
            if (s.lagged.count(c.id))
                text += " " + NameOf(g, c.sourceNodeId) + "." + std::to_string(c.sourcePort + 1) + "->"
                    + NameOf(g, c.targetNodeId) + "." + std::to_string(c.targetPort + 1) + (c.isRecycle ? "(Hand)" : "");
        text += " | Kreise:";
        for (const auto& cycle : s.cycles)
        {
            text += " [";
            for (const std::string& id : cycle)
                text += " " + NameOf(g, id);
            text += " ]";
        }
        text += " | Baum: ";
        for (const SStructureBranch& root : s.roots)
            DescribeBranch(g, root, text);
        return text;
    }

    // Ergebnisse zweier Graphen gleich? Lauf 1 und Mittel aller Laeufe, je
    // Eintrag, Zeitpunkt und Groessenklasse exakt.
    bool SameResults(CFlowsheetGraph& g1, CFlowsheetGraph& g2, size_t& compared)
    {
        compared = 0;
        for (const SResultsEntry& entry : g1.GetResultsEntries())
        {
            const CSimulationResults* r1 = g1.GetDisplayResults(entry.nodeId, entry.port);
            const CSimulationResults* r2 = g2.GetDisplayResults(entry.nodeId, entry.port);
            const CEnsembleStatistics* s1 = g1.GetStatistics(entry.nodeId, entry.port);
            const CEnsembleStatistics* s2 = g2.GetStatistics(entry.nodeId, entry.port);
            if (!r1 || !r2 || !s1 || !s2 || r1->GetNumTimeSteps() != r2->GetNumTimeSteps()
                || r1->GetNumSizeClasses() != r2->GetNumSizeClasses())
                return false;
            for (size_t k = 0; k < r1->GetNumTimeSteps(); ++k)
                for (size_t s = 0; s < r1->GetNumSizeClasses(); ++s)
                {
                    if (r1->GetValue(k, s) != r2->GetValue(k, s) || s1->GetMean().GetValue(k, s) != s2->GetMean().GetValue(k, s))
                        return false;
                    ++compared;
                }
        }
        return compared > 0;
    }

    std::string Replace(std::string text, const std::string& from, const std::string& to)
    {
        const size_t at = text.find(from);
        if (at != std::string::npos)
            text.replace(at, from.size(), to);
        return text;
    }

    std::string ReplaceAll(std::string text, const std::string& from, const std::string& to)
    {
        for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
            text.replace(at, from.size(), to);
        return text;
    }

    // Dieselbe Datei in Formatversion 1: recycle als true (von Hand) / false.
    std::string AsVersion1(const std::string& text)
    {
        std::string old = Replace(text, "\"version\": 2", "\"version\": 1");
        old = ReplaceAll(old, "\"recycle\": \"manual\"", "\"recycle\": true");
        old = ReplaceAll(old, "\"recycle\": \"auto\"", "\"recycle\": false");
        return ReplaceAll(old, "\"recycle\": \"no\"", "\"recycle\": false");
    }

    bool Contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

    bool AnyContains(const std::vector<std::string>& lines, const std::string& part)
    {
        for (const std::string& line : lines)
            if (Contains(line, part))
                return true;
        return false;
    }

    // Rundlauftest fuer ein Flowsheet.
    void RoundTrip(bool particles)
    {
        const std::string mode = particles ? "Partikelmodus" : "Kontinuum";
        CFlowsheetGraph g1;
        BuildAll(g1, particles);
        SFlowsheetFileSettings settings;
        settings.runs = 3;
        const std::string text1 = WriteFlowsheetJson(g1, settings);

        CFlowsheetGraph g2;
        const SFlowsheetReadResult read = ReadFlowsheetJson(text1, g2);
        for (const std::string& warning : read.warnings)
            std::printf("      Hinweis: %s\n", warning.c_str());
        Check(read.ok && read.warnings.empty(), mode + ": Datei gelesen, ohne Hinweise" + (read.ok ? std::string() : " - " + read.error));
        const std::string text2 = WriteFlowsheetJson(g2, read.settings);
        Check(text1 == text2, mode + ": schreiben - lesen - schreiben ergibt denselben Text");
        Check(read.settings.runs == 3, mode + ": Laeufe 3");
        Check(Describe(g1) == Describe(g2), mode + ": gleiche Reihenfolge, Schnitte, Kreise und Baum");
        std::printf("      %s\n", Describe(g2).c_str());
        // Schnitt in der Datei: Kontinuum vom Graphen gewaehlt, Partikelmodus von Hand.
        Check(Contains(text1, particles ? "\"recycle\": \"manual\"" : "\"recycle\": \"auto\"")
            && !Contains(text1, particles ? "\"recycle\": \"auto\"" : "\"recycle\": \"manual\""),
            mode + (particles ? ": Schnitt als \"manual\" in der Datei" : ": Schnitt als \"auto\" in der Datei"));

        // Formatversion 1 (true/false) wird gelesen und ergibt dieselbe Datei.
        CFlowsheetGraph g3;
        const SFlowsheetReadResult old = ReadFlowsheetJson(AsVersion1(text1), g3);
        Check(old.ok && old.warnings.empty() && WriteFlowsheetJson(g3, old.settings) == text1,
            mode + ": Formatversion 1 gelesen, ergibt dieselbe Datei");

        // Jede Einstellung einzeln (fehlt sie beim Schreiben UND Lesen, faellt
        // sie im Textvergleich nicht auf).
        const SSimulationGrid& a = g1.GetGrid();
        const SSimulationGrid& b = g2.GetGrid();
        Check(a.sizeGridType == b.sizeGridType && a.numSizeClasses == b.numSizeClasses && a.sizeMin == b.sizeMin
            && a.sizeMax == b.sizeMax && a.numTimeSteps == b.numTimeSteps && a.totalTime == b.totalTime
            && a.GetDt() == b.GetDt() && a.randomSeed == b.randomSeed && a.representation == b.representation
            && g1.IsDetailedLog() == g2.IsDetailedLog(), mode + ": Raster, Zeit, Seed, Darstellung, Protokoll gleich");
        Check(a.cards.cardsPerSlice == b.cards.cardsPerSlice && a.cards.rule == b.cards.rule
            && a.cards.numberShare == b.cards.numberShare && a.cards.exponent == b.cards.exponent
            && a.cards.sliceLimits == b.cards.sliceLimits && a.cards.upperFactor == b.cards.upperFactor
            && a.cards.splitFactor == b.cards.splitFactor, mode + ": Karteneinstellungen gleich");
        bool unitsSame = g1.GetNodes().size() == g2.GetNodes().size() && g1.GetConnections().size() == g2.GetConnections().size();
        for (const SFlowsheetNode& n1 : g1.GetNodes())
        {
            const SFlowsheetNode* n2 = g2.FindNode(n1.id);
            unitsSame = unitsSame && n2 && n2->name == n1.name && n2->type == n1.type && n2->posX == n1.posX
                && n2->posY == n1.posY && n2->numInputs == n1.numInputs && n2->numOutputs == n1.numOutputs;
            if (!n2)
                continue;
            if (const CInletUnit* i1 = g1.GetInlet(n1.id))
            {
                const CInletUnit* i2 = g2.GetInlet(n1.id);
                const SSizeDistribution& d1 = i1->GetSizeDistribution();
                const SSizeDistribution& d2 = i2->GetSizeDistribution();
                bool same = i2->GetMaterial() == i1->GetMaterial() && i2->GetTemperature() == i1->GetTemperature()
                    && i2->GetPressure() == i1->GetPressure() && i2->GetTimeProfile().size() == i1->GetTimeProfile().size()
                    && d1.type == d2.type && d1.position == d2.position && d1.width == d2.width && d1.points.size() == d2.points.size();
                for (size_t k = 0; same && k < i1->GetTimeProfile().size(); ++k)
                    same = i1->GetTimeProfile()[k].time == i2->GetTimeProfile()[k].time
                        && i1->GetTimeProfile()[k].massFlow == i2->GetTimeProfile()[k].massFlow;
                for (size_t k = 0; same && k < d1.points.size(); ++k)
                    same = d1.points[k].size == d2.points[k].size && d1.points[k].fraction == d2.points[k].fraction;
                unitsSame = unitsSame && same;
            }
            if (const CSplitterUnit* s1 = g1.GetSplitter(n1.id))
            {
                const CSplitterUnit* s2 = g2.GetSplitter(n1.id);
                bool same = s2->GetFractions() == s1->GetFractions() && s2->IsStochastic() == s1->IsStochastic()
                    && s2->GetFluctuations().size() == s1->GetFluctuations().size();
                for (size_t k = 0; same && k < s1->GetFluctuations().size(); ++k)
                    same = s1->GetFluctuations()[k].sigma == s2->GetFluctuations()[k].sigma
                        && s1->GetFluctuations()[k].tau == s2->GetFluctuations()[k].tau;
                unitsSame = unitsSame && same;
            }
            if (const IStreamProcessor* p1 = g1.GetProcessor(n1.id))
            {
                const IStreamProcessor* p2 = g2.GetProcessor(n1.id);
                bool same = p2->GetTypeName() == p1->GetTypeName();
                for (const SParameterDescriptor& d : p1->GetParameterDescriptors())
                    same = same && p2->GetParameterValue(d.id) == p1->GetParameterValue(d.id);
                unitsSame = unitsSame && same;
            }
            if (const CVesselUnit* v1 = g1.GetVessel(n1.id))
            {
                const SVesselSettings& x = v1->GetSettings();
                const SVesselSettings& y = g2.GetVessel(n1.id)->GetSettings();
                unitsSame = unitsSame && x.mixing == y.mixing && x.residenceTime == y.residenceTime && x.batch == y.batch
                    && x.cells == y.cells && x.coreShare == y.coreShare && x.coreTime == y.coreTime && x.fill == y.fill
                    && x.fillMass == y.fillMass && x.fillMaterial == y.fillMaterial
                    && x.fillDistribution.type == y.fillDistribution.type
                    && x.fillDistribution.position == y.fillDistribution.position
                    && x.fillDistribution.width == y.fillDistribution.width && x.fillTemperature == y.fillTemperature
                    && x.fillPressure == y.fillPressure && x.stockFactor == y.stockFactor;
            }
        }
        for (size_t k = 0; unitsSame && k < g1.GetConnections().size(); ++k)
        {
            const SFlowsheetConnection& c1 = g1.GetConnections()[k];
            const SFlowsheetConnection& c2 = g2.GetConnections()[k];
            unitsSame = c1.sourceNodeId == c2.sourceNodeId && c1.sourcePort == c2.sourcePort && c1.targetNodeId == c2.targetNodeId
                && c1.targetPort == c2.targetPort && c1.isRecycle == c2.isRecycle;
        }
        Check(unitsSame, mode + ": alle Units, Einstellungen und Verbindungen gleich");

        // Gleicher Seed, gleiche IDs -> dieselben Zufallszahlen, dieselben Ergebnisse.
        const size_t runs1 = g1.RunSimulations(settings.runs);
        const size_t runs2 = g2.RunSimulations(read.settings.runs);
        size_t compared = 0;
        const bool same = SameResults(g1, g2, compared);
        Check(runs1 == 3 && runs2 == 3 && g1.GetSimulationWarnings() == g2.GetSimulationWarnings() && same,
            mode + ": 3 Laeufe, Ergebnisse exakt gleich (" + std::to_string(compared) + " Werte je Lauf 1 und Mittel)");
    }
}

int main()
{
    // 1) Rundlauf: alle Unit-Typen, Einstellungen abseits des Standards.
    RoundTrip(false);
    RoundTrip(true);

    // 2) So sieht die Datei aus (Kontinuum-Flowsheet, Ausschnitte).
    CFlowsheetGraph g;
    BuildAll(g, false);
    const std::string text = WriteFlowsheetJson(g, SFlowsheetFileSettings());
    Check(Contains(text, "\"format\": \"StoSiS-Flowsheet\"") && Contains(text, "\"version\": 2")
        && Contains(text, "\"detailedLog\": true") && Contains(text, "\"runs\": 20")
        && Contains(text, "\"dt\": 0.1") && Contains(text, "\"fractions\": [0.5, 0.2, 0.3]")
        && Contains(text, "{ \"from\": \"node_1\", \"output\": 1, \"to\": \"node_2\", \"input\": 2, \"recycle\": \"no\" }")
        && Contains(text, "\"recycle\": \"auto\""),
        "Datei: Kopf, Ausfuehrung, Laeufe, dt, Anteile und Verbindungen lesbar auf einer Zeile");
    Check(!Contains(text, "rangeFactor") && !Contains(text, "historyBudget") && !Contains(text, "compounds"),
        "Datei: ohne Kartenansicht und ohne abgeleitete Stoffliste");

    // 3) Fehler und Hinweise beim Lesen.
    {
        CFlowsheetGraph e;
        Check(!ReadFlowsheetJson("{ nicht json", e).ok, "Fehler: kein JSON");
    }
    {
        CFlowsheetGraph e;
        const SFlowsheetReadResult r = ReadFlowsheetJson(Replace(text, "StoSiS-Flowsheet", "Etwas anderes"), e);
        Check(!r.ok && Contains(r.error, "keine StoSiS-Flowsheet-Datei"), "Fehler: anderes Format");
    }
    {
        CFlowsheetGraph e;
        const SFlowsheetReadResult r = ReadFlowsheetJson(Replace(text, "\"version\": 2", "\"version\": 3"), e);
        Check(!r.ok && Contains(r.error, "Formatversion 3"), "Fehler: neuere Formatversion");
    }
    {
        CFlowsheetGraph e;
        const SFlowsheetReadResult r = ReadFlowsheetJson(Replace(text, "[0.5, 0.2, 0.3]", "[0.5, 0.2, 0.2]"), e);
        Check(!r.ok && Contains(r.error, "Splitter1") && Contains(r.error, "fractions"), "Fehler: Splitter-Anteile ohne Summe 1 (mit Ort)");
    }
    {
        CFlowsheetGraph e;
        const SFlowsheetReadResult r = ReadFlowsheetJson(Replace(text, "\"to\": \"node_2\"", "\"to\": \"node_99\""), e);
        Check(!r.ok && Contains(r.error, "Verbindung 1") && Contains(r.error, "node_99"), "Fehler: Verbindung zu unbekannter Unit");
    }
    {
        CFlowsheetGraph e;
        const SFlowsheetReadResult r = ReadFlowsheetJson(Replace(text, "\"mixing\": \"cascade\"", "\"mixing\": \"wirbel\""), e);
        Check(!r.ok && Contains(r.error, "wirbel") && Contains(r.error, "coreFlow"), "Fehler: unbekannte Mischung, moegliche Werte genannt");
    }
    {
        CFlowsheetGraph e;
        const SFlowsheetReadResult r = ReadFlowsheetJson(Replace(text, "\"recycle\": \"no\"", "\"recycle\": \"vielleicht\""), e);
        Check(!r.ok && Contains(r.error, "recycle") && Contains(r.error, "manual, auto, no"), "Fehler: unbekannter recycle-Wert");
    }
    {
        // "auto"/"no" sind nur Information; passt es nicht mehr, ein Hinweis.
        CFlowsheetGraph e;
        const SFlowsheetReadResult r = ReadFlowsheetJson(Replace(text, "\"recycle\": \"auto\"", "\"recycle\": \"no\""), e);
        Check(r.ok && r.warnings.size() == 1 && AnyContains(r.warnings, "in der Datei \"no\", der Graph schneidet hier jetzt auf")
            && WriteFlowsheetJson(e, r.settings) == text, "Hinweis: \"no\" an einer Stelle, die der Graph aufschneidet");
    }
    {
        CFlowsheetGraph e;
        Check(!ReadFlowsheetJson(text, g).ok, "Fehler: Laden nur in einen leeren Graphen");
        const SFlowsheetReadResult r = ReadFlowsheetJson(
            Replace(Replace(text, "\"seed\": 4242,", ""), "\"name\": \"Produkt\",", "\"name\": \"Produkt\", \"farbe\": \"blau\","), e);
        Check(r.ok && r.warnings.size() == 2 && AnyContains(r.warnings, "\"seed\" fehlt")
            && AnyContains(r.warnings, "\"farbe\"") && e.GetGrid().randomSeed == SSimulationGrid().randomSeed,
            "Hinweise: fehlendes Feld -> Standardwert, unbekanntes Feld uebergangen");
        // Neue Knoten nach dem Laden bekommen eine freie ID.
        const std::string fresh = e.AddOutletNode("Neu", 0, 0);
        size_t sameId = 0;
        for (const SFlowsheetNode& node : e.GetNodes())
            sameId += node.id == fresh ? 1 : 0;
        Check(sameId == 1 && fresh != "node_0", "Nach dem Laden: neue Unit mit freier ID (" + fresh + ")");
    }

    // 4) Datei auf der Platte.
    {
        const std::string path = (std::filesystem::temp_directory_path() / "stosis_test_flowsheet.json").u8string();
        std::string error;
        CFlowsheetGraph e;
        const bool saved = SaveFlowsheetFile(path, g, SFlowsheetFileSettings(), error);
        const SFlowsheetReadResult r = LoadFlowsheetFile(path, e);
        Check(saved && r.ok && WriteFlowsheetJson(e, r.settings) == text, "Datei speichern und laden");
        std::filesystem::remove(std::filesystem::u8path(path));
    }

    // 5) Struktur des Start-Flowsheets: Inlet -> Merger -> Splitter ->
    //    Outlet, Splitter -> Prozess -> Merger. Geschnitten wird Prozess ->
    //    Merger (Eingang 2), der Kreis ist Merger, Splitter, Prozess.
    {
        // Wie MainWindow::BuildTestFlowsheet (Lage, Anteile, Raster).
        CFlowsheetGraph s;
        const std::string in = s.AddInletNode("Inlet1", 0.0, 55.0);
        const std::string mg = s.AddMergerNode("Merger1", 2, 160.0, 60.0);
        const std::string sp = s.AddSplitterNode("Splitter1", 2, 320.0, 60.0);
        const std::string out = s.AddOutletNode("Outlet1", 500.0, 0.0);
        const std::string pr = s.AddProcessNode("Prozess1", 500.0, 120.0);
        s.GetSplitter(sp)->SetFractions({ 0.7, 0.3 });
        s.ConfigureGrid(300, 1e-6, 1e-3, 201, 40.0);
        s.AddConnection(in, 0, mg, 0);
        s.AddConnection(mg, 0, sp, 0);
        s.AddConnection(sp, 0, out, 0);
        s.AddConnection(sp, 1, pr, 0);
        s.AddConnection(pr, 0, mg, 1);
        const std::string d = Describe(s);
        std::printf("      %s\n", d.c_str());
        Check(d == "Reihenfolge: Inlet1 Merger1 Splitter1 Outlet1 Prozess1 | Schnitte: Prozess1.1->Merger1.2"
                   " | Kreise: [ Merger1 Splitter1 Prozess1 ] | Baum: (Inlet1(Merger1(Splitter1(Outlet1)(Prozess1(Schnitt Merger1)))))",
            "Struktur Start-Flowsheet: Reihenfolge, Schnitt, Kreis, Baum");
        const SFlowsheetStructure st = s.GetStructure();
        Check(st.GetStep(out) == 4 && st.GetCycle(sp) == 0 && st.GetCycle(out) == -1, "Struktur: Schritt und Kreis je Unit");

        // Datei des Start-Flowsheets: Zeilen, die als Beispiel in der Doku stehen.
        const std::string start = WriteFlowsheetJson(s, SFlowsheetFileSettings());
        std::printf("%s", start.c_str());
        Check(Contains(start, "        \"dt\": 0.2,\n")
            && Contains(start, "        \"sizeGrid\": { \"type\": \"logarithmic\", \"classes\": 300, \"dMin\": 1e-06, \"dMax\": 0.001 },\n")
            && Contains(start, "            \"position\": [320.0, 60.0],\n            \"outputs\": 2,\n")
            && Contains(start, "                \"fractions\": [0.7, 0.3],\n                \"stochastic\": false,\n"
                               "                \"fluctuations\": [{ \"sigma\": 0.05, \"tau\": 1.0 }]\n")
            && Contains(start, "        { \"from\": \"node_0\", \"output\": 1, \"to\": \"node_1\", \"input\": 1, \"recycle\": \"no\" },\n")
            && Contains(start, "        { \"from\": \"node_4\", \"output\": 1, \"to\": \"node_1\", \"input\": 2, \"recycle\": \"auto\" }\n"),
            "Datei des Start-Flowsheets: Beispielzeilen der Doku");
    }

    // 6) Zwei Inlets in einen Merger, kein Kreis: der Merger haengt unter
    //    Inlet1 (Eingang 1), unter Inlet2 steht ein Verweis.
    {
        CFlowsheetGraph s;
        const std::string i1 = s.AddInletNode("Inlet1", 0, 0);
        const std::string i2 = s.AddInletNode("Inlet2", 0, 0);
        const std::string mg = s.AddMergerNode("Merger1", 2, 0, 0);
        const std::string out = s.AddOutletNode("Outlet1", 0, 0);
        s.AddConnection(i2, 0, mg, 1);
        s.AddConnection(i1, 0, mg, 0);
        s.AddConnection(mg, 0, out, 0);
        const std::string d = Describe(s);
        std::printf("      %s\n", d.c_str());
        Check(d == "Reihenfolge: Inlet1 Inlet2 Merger1 Outlet1 | Schnitte: | Kreise: | Baum: (Inlet1(Merger1(Outlet1)))(Inlet2(Verweis Merger1))",
            "Struktur ohne Kreis: Merger unter Eingang 1, Verweis unter Inlet2");
    }

    return Summary();
}
