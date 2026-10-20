# Materialdatei von StoSiS

`Materialien.json` enthält die Stoffdaten, mit denen StoSiS rechnet. Jeder Wert hat eine
Quelle (`source`) und einen Prüfstatus (`status`). Ziel: Jede Zahl, die in eine Simulation
eingeht, ist belegt.

Der Startinhalt stammt aus der Materialdatenbank von Dyssol (`Materials.dmdb`, BSD-3-Lizenz,
siehe `LICENSE_Dyssol.txt`). Umgewandelt wurde er am 12.10.2026 mit
`tools/dmdb_to_json.pl`. Seitdem wird die JSON-Datei von Hand gepflegt; das Skript dient nur
noch der Nachvollziehbarkeit.

## Aufbau

```
materials[]                  ein Eintrag je Stoff
  name, dyssol_key           dyssol_key bleibt erhalten: Dyssol-Flowsheets (.dflw)
                             sprechen Stoffe über diesen Schlüssel an
  state                      gas / liquid / solid
  warnings                   Auffälligkeiten aus der Erstprüfung - dort zuerst nachsehen
  constants                  feste Werte (Molmasse, Siedepunkt, Bond-Index ...)
  properties                 Werte, die von T und p abhängen (Dichte, c_p, Viskosität ...)
interactions[]               Eigenschaften zweier Stoffe (Grenzflächenspannung)
```

Eine Eigenschaft in `properties` ist eine Liste. Mehrere Einträge gelten für verschiedene
Temperatur- oder Druckbereiche (`T_range`, `p_range`).

Alle Werte in SI-Einheiten: T in K, p in Pa, Dichte in kg/m3, c_p in J/(kg K).
Ausnahme: `bond_work_index` in kWh/t.

## Arten von Einträgen (`type`)

| type | Bedeutung |
|---|---|
| `constant` | `value` gilt im ganzen Bereich |
| `table_T` | Messpunkte `[T, Wert]`, dazwischen linear interpoliert |
| `table_relative_humidity` | Messpunkte `[relative Luftfeuchte 0..1, Wert]` (Gleichgewichtsfeuchte) |
| `ideal_gas` | ρ = M·p/(R·T) mit Molmasse `molar_mass` |
| `sutherland` | y = y_ref·(T_ref + S)/(T + S)·(T/T_ref)^1,5 (Gasviskosität, Wärmeleitfähigkeit) |
| `antoine` | Dampfdruck p = 1e5·10^(A − B/(T + C)) Pa |
| `polynomial` | y = a0 + a1·T + a2·T² + … (`coefficients`) |
| `polynomial_cp` | y = a + b·T + c·T² + d·T³ + e/T² (`coefficients`) |

## Festlegungen (12.10.2026)

- **Dichte** (`density`) ist die Feststoffdichte. Partikel gelten zunächst als unporös; die
  Porosität kommt später als Partikeleigenschaft an den Inlet.
- **Wärme:** Gespeichert wird nur die Wärmekapazität `heat_capacity`, also c_p(T). Die
  Enthalpie h(T) = ∫ c_p dT berechnet StoSiS selbst. Wo Dyssol nur eine Enthalpie hat
  (CaCO3, CaO, CO2, O2, SO2, Al2O3), wurde c_p = dh/dT abgeleitet; `source` sagt das dann.
  Bei Stoffen mit beiden Angaben steht in `note` die Steigung der Dyssol-Enthalpie zum
  Vergleich.

## Werte prüfen

1. Wert mit einer Quelle vergleichen (Datenblatt, NIST WebBook, VDI-Wärmeatlas …).
2. Bei Bedarf `value` bzw. die Parameter korrigieren.
3. `source` auf die eigene Quelle setzen und `status` von `"unchecked"` auf `"checked"` ändern.

Die Datei ist reiner Text (UTF-8) und lässt sich mit jedem Editor bearbeiten. JSON ist streng:
Zwischen zwei Einträgen steht ein Komma, nach dem letzten keines, und Kommentare sind nicht
erlaubt. StoSiS meldet Fehler beim Einlesen mit Zeile und Spalte (Reiter "Materialien", "Neu laden").

## Auffälligkeiten aus der Erstprüfung

Steht jeweils auch unter `warnings` beim Stoff:

- **Wärmekapazität fehlt:** Coal (Dyssol-Enthalpie ist konstant), NaCl (Enthalpie nur für die
  Gasphase bei 2500–6000 K), Cellets (Parameter sind unumgerechnete Werte von Wasser).
- **Abgeleitete c_p weichen von der Literatur ab:** CaCO3 (+72 %), Al2O3 (+16 %), CO2 (+12 %).
- **Dichte ist vermutlich keine Feststoffdichte:** Milk (Schüttdichte), Al2O3, FCC, Cellets
  (poröse Partikel).
- **Platzhalter-Konstanten:** Milk und Al2O3 (von Sand übernommen), Cellets und FCC (von
  Wasser übernommen), Coal.
- **Sonstiges:** H2O_gas-Viskosität etwa 100-fach zu klein; Permittivität 4 F/m bei Milk,
  Al2O3, Cellets und FCC (vermutlich relative Permittivität); Viskositäten bei Sand und Urea
  ohne Bedeutung.
