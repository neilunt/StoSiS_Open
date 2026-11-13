; Addition to the installer (CPACK_INNOSETUP_EXTRA_SCRIPTS in the top-level
; CMakeLists): the folders Flowsheets, Laufzeiten and Kurven in the program folder are
; created writable for all users. The program folder under "Program Files" is
; read-only otherwise, and saving there would fail.
[Dirs]
Name: "{app}\Flowsheets"; Permissions: users-modify
Name: "{app}\Laufzeiten"; Permissions: users-modify
Name: "{app}\Kurven"; Permissions: users-modify
