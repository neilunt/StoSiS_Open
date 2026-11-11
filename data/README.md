# StoSiS material file

`Materialien.json` contains the material data StoSiS computes with. Every value has a
source (`source`) and a check status (`status`). Goal: every number that enters a
simulation is documented.

The initial content comes from the Dyssol material database (`Materials.dmdb`, BSD-3
license, see `LICENSE_Dyssol.txt`). It was converted with `tools/dmdb_to_json.pl`. The
JSON file is maintained by hand; the script is kept for traceability.

## Layout

```
materials[]                  one entry per compound
  name, dyssol_key           dyssol_key is kept: Dyssol flowsheets (.dflw)
                             refer to compounds by this key
  state                      gas / liquid / solid
  warnings                   issues found in the first check - look here first
  constants                  fixed values (molar mass, boiling point, Bond index ...)
  properties                 values depending on T and p (density, c_p, viscosity ...)
interactions[]               properties of two compounds (interfacial tension)
```

A property in `properties` is a list. Several entries apply to different temperature or
pressure ranges (`T_range`, `p_range`).

All values in SI units: T in K, p in Pa, density in kg/m3, c_p in J/(kg K).
Exception: `bond_work_index` in kWh/t.

## Entry types (`type`)

| type | Meaning |
|---|---|
| `constant` | `value` holds in the whole range |
| `table_T` | data points `[T, value]`, linearly interpolated in between |
| `table_relative_humidity` | data points `[relative humidity 0..1, value]` (equilibrium moisture) |
| `ideal_gas` | ρ = M·p/(R·T) with molar mass `molar_mass` |
| `sutherland` | y = y_ref·(T_ref + S)/(T + S)·(T/T_ref)^1.5 (gas viscosity, thermal conductivity) |
| `antoine` | vapor pressure p = 1e5·10^(A − B/(T + C)) Pa |
| `polynomial` | y = a0 + a1·T + a2·T² + … (`coefficients`) |
| `polynomial_cp` | y = a + b·T + c·T² + d·T³ + e/T² (`coefficients`) |

## Conventions

- **Density** (`density`) is the solid density. Particles are non-porous for now; porosity
  will be added later as a particle property at the inlet.
- **Heat:** only the heat capacity `heat_capacity`, i.e. c_p(T), is stored. StoSiS
  computes the enthalpy h(T) = ∫ c_p dT itself. Where Dyssol only has an enthalpy
  (CaCO3, CaO, CO2, O2, SO2, Al2O3), c_p = dh/dT was derived; `source` says so. For
  compounds with both, `note` gives the slope of the Dyssol enthalpy for comparison.

## Checking values

1. Compare the value with a source (data sheet, NIST WebBook, VDI Heat Atlas …).
2. If needed, correct `value` or the parameters.
3. Set `source` to your source and change `status` from `"unchecked"` to `"checked"`.

The file is plain text (UTF-8) and can be edited with any editor. JSON is strict: entries
are separated by a comma, there is none after the last one, and comments are not allowed.
StoSiS reports reading errors with line and column ("Materialien" tab, "Neu laden").

## Issues from the first check

Also listed under `warnings` for each compound:

- **Heat capacity missing:** Coal (Dyssol enthalpy is constant), NaCl (enthalpy only for
  the gas phase at 2500–6000 K), Cellets (parameters are unconverted values of water).
- **Derived c_p deviate from literature:** CaCO3 (+72 %), Al2O3 (+16 %), CO2 (+12 %).
- **Density probably not a solid density:** Milk (bulk density), Al2O3, FCC, Cellets
  (porous particles).
- **Placeholder constants:** Milk and Al2O3 (taken from sand), Cellets and FCC (taken
  from water), Coal.
- **Other:** H2O_gas viscosity about 100 times too small; permittivity 4 F/m for Milk,
  Al2O3, Cellets and FCC (probably relative permittivity); viscosities of sand and urea
  without meaning.
