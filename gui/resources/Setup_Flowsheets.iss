; Zusatz zum Installationsprogramm (CPACK_INNOSETUP_EXTRA_SCRIPTS im
; Haupt-CMakeLists): Der Ordner Flowsheets im Programmordner wird fuer alle
; Nutzer beschreibbar angelegt. Der Programmordner unter "Programme" ist sonst
; schreibgeschuetzt, und Speichern dort schluege fehl.
[Dirs]
Name: "{app}\Flowsheets"; Permissions: users-modify
