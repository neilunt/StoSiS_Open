#!/usr/bin/perl
# Erstuebernahme der Dyssol-Materialdatenbank in die StoSiS-Materialdatei.
#
# Einmalig verwendet: liest Dyssols Materials.dmdb (Format 3,
# BSD-3-Lizenz, siehe data/LICENSE_Dyssol.txt) und schreibt
# data/Materialien.json. Danach wird die JSON-Datei von Hand gepflegt; das
# Skript bleibt nur zur Nachvollziehbarkeit im Repository.
#
# Aufruf (Git Bash, im Ordner StoSiS):
#   perl tools/dmdb_to_json.pl "/c/Program Files/Dyssol/Materials.dmdb" data/Materialien.json
#
# Was das Skript tut:
#   - uebernimmt alle Konstanten und T/p-abhaengigen Eigenschaften woertlich
#     (Zahlen unveraendert als Text), Status "unchecked";
#   - speichert keine Enthalpie (nur c_p(T) wird gespeichert). Fehlt
#     eine Waermekapazitaet, wird c_p = dh/dT aus der Dyssol-Enthalpie
#     abgeleitet, sofern das bei Raumtemperatur ein plausibler Wert ist;
#   - markiert Auffaelligkeiten aus der Erstpruefung (warnings);
#   - liest die geschriebene Datei wieder ein und prueft, dass jede
#     uebernommene Zahl dort wieder auftaucht.
#
# Formeln der Dyssol-Korrelationen: ModelsCreatorSDK/MaterialsDatabase/
# Correlation.cpp (CCorrelation::GetValue), Schluessel: DefinesMDB.h.

use strict;
use warnings;
use utf8;
use JSON::PP;
use Scalar::Util qw(looks_like_number);

binmode STDOUT, ':encoding(UTF-8)';

my ($inPath, $outPath) = @ARGV;
die "Aufruf: perl dmdb_to_json.pl <Materials.dmdb> <Materialien.json>\n" unless $inPath && $outPath;

my $SOURCE  = 'Dyssol Materials.dmdb';
my $DERIVED = 'abgeleitet: c_p = dh/dT aus der Enthalpie in Dyssol Materials.dmdb';
my $T_ROOM  = 298.15; # Bezugstemperatur fuer Plausibilitaetspruefungen [K]

# Schluessel aus Dyssol DefinesMDB.h -> Name in StoSiS, Einheit (SI).
my %CONST = (
    107 => ['critical_pressure',     'Pa'],
    108 => ['critical_temperature',  'K'],
    114 => ['heat_of_fusion',        'J/mol'],
    115 => ['heat_of_vaporization',  'J/mol'],
    121 => ['molar_mass',            'kg/mol'],
    122 => ['normal_boiling_point',  'K'],
    123 => ['normal_freezing_point', 'K'],
    131 => ['formation_enthalpy',    'J/mol'],
    132 => ['bond_work_index',       'kWh/t'],
);
my %TPD = (
    234 => ['density',                      'kg/m3'],
    207 => ['heat_capacity',                'J/(kg K)'],
    226 => ['viscosity',                    'Pa s'],
    222 => ['thermal_conductivity',         'W/(m K)'],
    224 => ['vapor_pressure',               'Pa'],
    223 => ['equilibrium_moisture_content', 'kg/kg'],
    235 => ['permittivity',                 'F/m'],
    236 => ['mass_diffusion_coefficient',   'm2/s'],
);
my @TPD_ORDER = (234, 207, 226, 222, 224, 223, 235, 236);
my %INTER = (303 => ['interface_tension', 'N/m']);
my $SOA      = 141;
my $ENTHALPY = 208;
my $CP       = 207;

# Waermekapazitaeten aus der Literatur bei 298 K (NIST/JANAF, J/(mol K) durch
# Molmasse) - nur als Vergleich fuer abgeleitete Werte, vor Verwendung pruefen.
my %LIT_CP = (CaCO3 => 834, CaO => 750, CO2 => 843, O2 => 918, SO2 => 622, NaCl => 864, Al2O3 => 775);

# Auffaelligkeiten aus der Erstpruefung, je Stoff.
my %WARNINGS = (
    Coal => [
        'Dyssol-Enthalpie ist konstant (46105 J/kg), daraus folgt c_p = 0 - keine Wärmekapazität vorhanden. Literatur für trockene Kohle etwa 1000-1300 J/(kg K), z. B. Merrick (1983) - ergänzen.',
        'Siede-/Schmelzpunkt (2000/1800 K) und Molmasse sind für Kohle (zersetzt sich, kein Reinstoff) vermutlich Platzhalter.',
    ],
    H2O_gas => [
        'Viskosität 9.6e-08 Pa s ist etwa 100-fach zu klein; Wasserdampf hat bei 373 K etwa 1.2e-05 Pa s (IAPWS, Huber et al. 2009).',
    ],
    Sand => [
        'Viskosität 1 Pa s ist für einen Feststoff ohne Bedeutung.',
    ],
    Urea => [
        'Viskosität 0.00178 Pa s ist für einen Feststoff ohne Bedeutung (evtl. Wert der Schmelze).',
    ],
    Milk => [
        'Dichte 751 kg/m3 ist vermutlich eine Schüttdichte (gerüttelte Schüttdichte Magermilchpulver etwa 737 kg/m3); Feststoffdichte Milchpulver etwa 1300 (Vollmilch) bis 1460 kg/m3 (Magermilch) - eigene Recherche, noch zu prüfen.',
        'Konstanten offensichtlich von Sand übernommen (Molmasse 0.06008 = SiO2, Bildungsenthalpie -911000 J/mol, Siede-/Schmelzpunkt) - Platzhalter.',
        'Permittivität 4 F/m ist unmöglich (Vakuum 8.85e-12 F/m); vermutlich relative Permittivität 4, also etwa 3.5e-11 F/m.',
    ],
    Al2O3 => [
        'Dichte 1045 kg/m3 passt nicht zu unporösen Teilchen: kompaktes Al2O3 (Korund) hat etwa 3950 kg/m3 - vermutlich Partikeldichte poröser Teilchen.',
        'Konstanten außer der Molmasse offensichtlich von Sand übernommen (Bildungsenthalpie -911000 J/mol = SiO2, Siede-/Schmelzpunkt) - Platzhalter.',
        'Permittivität 4 F/m ist unmöglich (Vakuum 8.85e-12 F/m); vermutlich relative Permittivität 4.',
        'Gleichgewichtsfeuchte, erster Bereich (273-308 K): Feuchtebereich [1, 1] und erster Punkt (0; 0.1) sehen nach Tippfehler aus (die anderen Bereiche: [0, 1] und (0; 0)).',
    ],
    Cellets => [
        'Konstanten offensichtlich von Wasser übernommen (Verdampfungs-/Bildungsenthalpie von Wasserdampf; Siede-/Schmelzpunkt 3730.15/2730.15 K = 373.15/273.15 K mit zusätzlicher Ziffer) - Platzhalter. Keine Molmasse.',
        'Permittivität 4 F/m ist unmöglich (Vakuum 8.85e-12 F/m); vermutlich relative Permittivität 4.',
        'Dichte 1114 kg/m3: bei porösen Pellets evtl. Partikel- statt Feststoffdichte - prüfen.',
    ],
    FCC => [
        'Konstanten offensichtlich von Wasser übernommen (wie Cellets) - Platzhalter. Keine Molmasse.',
        'Permittivität 4 F/m ist unmöglich (Vakuum 8.85e-12 F/m); vermutlich relative Permittivität 4.',
        'Dichte 1632.5 kg/m3: FCC-Katalysator ist porös, vermutlich Partikel- statt Feststoffdichte - prüfen.',
    ],
);

###########################################################################
# Lesen

open my $fh, '<:encoding(UTF-8)', $inPath or die "Kann $inPath nicht öffnen: $!\n";
my $header = <$fh>;
$header =~ s/\r?\n$//;
die "Unerwartetes Format: '$header'\n" unless $header =~ /^DyssolMaterialsDatabase 3\s*$/;

my (@materials, @interactions, $mat, $inter);
while (my $line = <$fh>)
{
    $line =~ s/\r?\n$//;
    $line =~ s/\s*\$.*$//;        # Dyssol-Kommentar ab '$'
    next if $line =~ /^\s*$/;
    my ($code, $rest) = $line =~ /^(\d+)\s+(.*?)\s*$/ or die "Zeile nicht lesbar: $line\n";

    if ($code == 21) {
        $mat = { key => $rest, const => {}, constDescr => {}, tpd => {}, tpdDescr => {}, corrDescr => {} };
        push @materials, $mat;
        undef $inter;
    }
    elsif ($code == 22) { NeedMat($line); $mat->{name} = $rest; }
    elsif ($code == 23) { NeedMat($line); $mat->{description} = Text($rest); }
    elsif ($code == 26) { NeedMat($line); my ($k, $v) = split /\s+/, $rest; $mat->{const}{$k} = $v; }
    elsif ($code == 27) { NeedMat($line); my ($k, @c) = split /\s+/, $rest; push @{ $mat->{tpd}{$k} }, ParseCorrelation(@c); }
    elsif ($code == 41) { NeedMat($line); my ($k, $t) = $rest =~ /^(\d+)\s+(.*)$/; $mat->{constDescr}{$k} = Text($t); }
    elsif ($code == 42) { NeedMat($line); my ($k, $t) = $rest =~ /^(\d+)\s+(.*)$/; $mat->{tpdDescr}{$k} = Text($t); }
    elsif ($code == 43) { NeedMat($line); my ($k, $i, $t) = $rest =~ /^(\d+)\s+(\d+)\s+(.*)$/; $mat->{corrDescr}{$k}[$i] = Text($t); }
    elsif ($code == 31) {
        my ($k1, $k2) = split /\s+/, $rest;
        $inter = { keys => [$k1, $k2], tpd => {} };
        push @interactions, $inter;
        undef $mat;
    }
    elsif ($code == 32) { die "Eigenschaft ohne Wechselwirkung: $line\n" unless $inter; my ($k, @c) = split /\s+/, $rest; push @{ $inter->{tpd}{$k} }, ParseCorrelation(@c); }
    else { die "Unbekannter Eintrag $code: $line\n"; }
}
close $fh;

sub NeedMat { die "Eintrag ohne Stoff: $_[0]\n" unless $mat; }

# Dyssol ersetzt Zeilenumbrueche in Beschreibungen durch '%@#'.
sub Text { my $t = shift; $t =~ s/%@#/ | /g; return $t; }

sub ParseCorrelation
{
    my ($type, $tmin, $tmax, $pmin, $pmax, @params) = @_;
    return { type => $type, T => [$tmin, $tmax], p => [$pmin, $pmax], params => \@params };
}

###########################################################################
# Umwandeln

my %nameOfKey = map { $_->{key} => $_->{name} } @materials;
my @jsonMaterials;
my @derivedReport; # fuer die Ausgabe am Ende

for my $m (@materials)
{
    my $name = $m->{name};
    my @warnings = @{ $WARNINGS{$name} // [] };
    my @notes;

    # Aggregatzustand
    my $state;
    if (exists $m->{const}{$SOA}) {
        my %soa = (0 => 'solid', 1 => 'liquid', 2 => 'gas', 3 => 'unknown');
        $state = Obj(1, value => $soa{ $m->{const}{$SOA} } // 'unknown', source => $SOURCE);
    }
    else {
        $state = Obj(1, value => ($name eq 'H2O' ? 'liquid' : 'solid'), source => 'Annahme StoSiS (in Dyssol nicht angegeben)');
    }

    # Konstanten
    my @constants;
    for my $k (sort { $a <=> $b } keys %CONST) {
        if (!exists $m->{const}{$k}) {
            push @notes, "$CONST{$k}[0]: kein Wert; Dyssol-Hinweis: '$m->{constDescr}{$k}'" if exists $m->{constDescr}{$k};
            next;
        }
        my @fields = (value => Num($m->{const}{$k}), unit => $CONST{$k}[1], source => $SOURCE, status => 'unchecked');
        push @fields, reference => $m->{constDescr}{$k} if exists $m->{constDescr}{$k};
        push @constants, $CONST{$k}[0] => Obj(1, @fields);
    }
    for my $k (keys %{ $m->{const} }) {
        die "Unbekannte Konstante $k bei $name\n" unless exists $CONST{$k} || $k == $SOA;
    }

    # T/p-abhaengige Eigenschaften
    my @properties;
    for my $k (keys %{ $m->{tpd} }) {
        die "Unbekannte Eigenschaft $k bei $name\n" unless exists $TPD{$k} || $k == $ENTHALPY;
    }
    for my $k (@TPD_ORDER) {
        my @list;
        if (exists $m->{tpd}{$k}) {
            my $i = 0;
            for my $c (@{ $m->{tpd}{$k} }) {
                my $ref = Reference($m->{corrDescr}{$k}[$i], $m->{tpdDescr}{$k});
                push @list, CorrelationToJson($c, $TPD{$k}[1], "$name/$TPD{$k}[0]", $SOURCE, $ref);
                ++$i;
            }
        }
        if ($k == $CP) {
            my @h = @{ $m->{tpd}{$ENTHALPY} // [] };
            my $hRef = $m->{tpdDescr}{$ENTHALPY};
            if (@list && @h) {
                # Waermekapazitaet vorhanden: Enthalpie nur zum Vergleich.
                my ($cp, $why) = DerivedCpAt($h[0], $T_ROOM);
                my $text = defined $cp
                    ? sprintf('Zum Vergleich: Steigung der Dyssol-Enthalpie bei %.2f K = %.0f J/(kg K).', $T_ROOM, $cp)
                    : "Dyssol-Enthalpie zum Vergleich unbrauchbar: $why";
                push @{ $list[0] }, note => $text;
                push @derivedReport, sprintf('%-8s c_p vorhanden; %s', $name, $text);
            }
            elsif (@h) {
                for my $hc (@h) {
                    my ($cp, $why) = DerivedCpAt($hc, $T_ROOM);
                    if (!defined $cp) {
                        push @warnings, "Keine Wärmekapazität: aus der Dyssol-Enthalpie lässt sich kein brauchbares c_p ableiten ($why)."
                            . (exists $LIT_CP{$name} ? " Literatur (NIST, 298 K) etwa $LIT_CP{$name} J/(kg K) - prüfen." : '')
                            unless $name eq 'Coal'; # hat eine eigene Warnung (%WARNINGS)
                        push @derivedReport, sprintf('%-8s kein c_p: %s', $name, $why);
                        next;
                    }
                    my $entry = DerivedCpJson($hc, $hRef);
                    my $text = sprintf('c_p(%.2f K) = %.0f J/(kg K)', $T_ROOM, $cp);
                    if (exists $LIT_CP{$name}) {
                        my $dev = ($cp - $LIT_CP{$name}) / $LIT_CP{$name};
                        $text .= sprintf('; Literatur (NIST, 298 K) etwa %d J/(kg K), Abweichung %+.0f %%', $LIT_CP{$name}, 100 * $dev);
                        push @warnings, "Abgeleitete Wärmekapazität weicht stark von der Literatur ab: $text - prüfen." if abs($dev) > 0.10;
                    }
                    push @$entry, note => "$text.";
                    push @list, $entry;
                    push @derivedReport, sprintf('%-8s c_p abgeleitet (%s): %s', $name, $entry->[1], $text);
                }
            }
            elsif (!@list) {
                push @warnings, 'Weder Wärmekapazität noch Enthalpie vorhanden.' unless $name eq 'Coal';
            }
        }
        push @properties, $TPD{$k}[0] => \@list if @list;
    }

    my @fields = (
        name       => $name,
        dyssol_key => $m->{key},
        state      => $state,
    );
    push @fields, description => $m->{description} if exists $m->{description};
    push @fields, warnings => \@warnings if @warnings;
    push @fields, notes    => \@notes    if @notes;
    push @fields, constants  => Obj(0, @constants);
    push @fields, properties => Obj(0, @properties);
    push @jsonMaterials, Obj(0, @fields);
}

my @jsonInteractions;
for my $in (@interactions) {
    my @props;
    for my $k (sort keys %{ $in->{tpd} }) {
        die "Unbekannte Wechselwirkung $k\n" unless exists $INTER{$k};
        push @props, $INTER{$k}[0] => [ map { CorrelationToJson($_, $INTER{$k}[1], "Wechselwirkung/$INTER{$k}[0]", $SOURCE, undef) } @{ $in->{tpd}{$k} } ];
    }
    push @jsonInteractions, Obj(0,
        materials   => [ map { $nameOfKey{$_} // die "Unbekannter Schlüssel $_\n" } @{ $in->{keys} } ],
        dyssol_keys => $in->{keys},
        properties  => Obj(0, @props));
}

my $root = Obj(0,
    format  => 'StoSiS-Materialdatenbank',
    version => Num(1),
    info    => Obj(0,
        beschreibung => 'Stoffdaten für StoSiS. Jeder Wert trägt eine Quelle (source) und einen Prüfstatus (status).',
        einheiten    => 'SI: Temperatur T in K, Druck p in Pa, Masse in kg, Länge in m, Energie in J, Zeit in s. Ausnahme: bond_work_index in kWh/t (üblich).',
        status       => 'unchecked = aus Dyssol übernommen bzw. abgeleitet, noch nicht geprüft; checked = geprüft (Beleg in source bzw. reference eintragen).',
        dichte       => 'density ist die Feststoffdichte: Partikel gelten zunächst als unporös. Die Porosität kommt später als Partikeleigenschaft an den Inlet.',
        waerme       => 'Gespeichert wird nur die Wärmekapazität heat_capacity c_p(T); die Enthalpie h(T) berechnet StoSiS daraus.',
        bereiche     => 'T_range und p_range: Gültigkeitsbereich. Mehrere Einträge je Eigenschaft gelten für verschiedene Bereiche.',
        warnungen    => 'warnings: Auffälligkeiten aus der Erstprüfung - dort zuerst nachsehen.',
        herkunft     => 'Startinhalt aus Dyssol Materials.dmdb (Format 3, BSD-3-Lizenz, siehe LICENSE_Dyssol.txt), umgewandelt mit tools/dmdb_to_json.pl. dyssol_key bleibt erhalten, weil Dyssol-Flowsheets (.dflw) Stoffe über diesen Schlüssel ansprechen.',
    ),
    materials    => \@jsonMaterials,
    interactions => \@jsonInteractions,
);

open my $out, '>:encoding(UTF-8)', $outPath or die "Kann $outPath nicht schreiben: $!\n";
print $out J($root, ''), "\n";
close $out;

###########################################################################
# Korrelationen

# Beschreibung der Korrelation und der Eigenschaft zusammenfassen; Dyssol
# wiederholt dieselbe Quelle oft auf beiden Ebenen.
sub Reference
{
    my (@parts, %seen);
    for my $text (grep { defined && length } @_) {
        push @parts, grep { !$seen{$_}++ } split / \| /, $text;
    }
    return @parts ? join(' | ', @parts) : undef;
}

sub CorrelationToJson
{
    my ($c, $unit, $what, $source, $reference) = @_;
    my @p = @{ $c->{params} };
    my @o;
    my $points;
    my $t = $c->{type};
    if ($t == 2) {
        die "$what: Konstante mit " . scalar(@p) . " Parametern\n" unless @p == 1;
        push @o, type => 'constant', value => Num($p[0]);
    }
    elsif ($t == 0 || $t == 1) {
        die "$what: ungerade Anzahl Tabellenwerte\n" if @p % 2;
        push @o, type => ($t == 0 ? 'table_T' : 'table_relative_humidity'),
                 formula => ($t == 0 ? 'Messpunkte [T in K, Wert]; dazwischen linear interpoliert'
                                     : 'Messpunkte [relative Luftfeuchte 0..1, Wert]; dazwischen linear interpoliert (in Dyssol über die Druckachse kodiert)');
        $points = [ map { [ Num($p[2 * $_]), Num($p[2 * $_ + 1]) ] } 0 .. @p / 2 - 1 ];
    }
    elsif ($t == 10) {
        die "$what: Sutherland braucht 3 Parameter\n" unless @p == 3;
        push @o, type => 'sutherland', formula => 'y = y_ref * (T_ref + S) / (T + S) * (T / T_ref)^1.5',
                 y_ref => Num($p[0]), T_ref => Num($p[1]), S => Num($p[2]);
    }
    elsif ($t == 12) {
        die "$what: ideales Gas braucht 1 Parameter\n" unless @p == 1;
        push @o, type => 'ideal_gas', formula => 'y = M * p / (R * T), R = universelle Gaskonstante', molar_mass => Num($p[0]);
    }
    elsif ($t == 4) {
        # Dyssol EXPONENT_1: y = a*b^(c + d*T + (e*T + f)/(g*T + h)) + i. Alle
        # Eintraege der Datei sind Antoine-Gleichungen in NIST-Form.
        die "$what: kein Antoine-Muster\n" unless @p == 9 && $p[0] == 1e5 && $p[1] == 10 && $p[3] == 0 && $p[4] == 0 && $p[6] == 1 && $p[8] == 0;
        push @o, type => 'antoine', formula => 'p_sat = 1e5 * 10^(A - B / (T + C)) Pa (NIST-Form: log10(p/bar) = A - B/(T + C))',
                 A => Num($p[2]), B => Num(Negate($p[5])), C => Num($p[7]);
    }
    else {
        die "$what: Korrelationstyp $t wird nicht unterstützt\n";
    }
    push @o, unit => $unit;
    push @o, T_range => [ Num($c->{T}[0]), Num($c->{T}[1]) ];
    if ($t == 1) { push @o, relative_humidity_range => [ Num($c->{p}[0]), Num($c->{p}[1]) ]; }
    else         { push @o, p_range => [ Num($c->{p}[0]), Num($c->{p}[1]) ]; }
    push @o, source => $source, status => 'unchecked';
    push @o, reference => $reference if defined $reference;
    push @o, points => $points if $points;
    return Obj(0, @o);
}

# Negiert eine Zahl als Text (Ziffern bleiben unveraendert).
sub Negate { my $s = shift; return $s =~ /^-/ ? substr($s, 1) : "-$s"; }

# c_p = dh/dT aus einer Dyssol-Enthalpie-Korrelation als Funktion (Koeffizienten)
# bzw. Tabelle. Rueckgabe: [Typ, Koeffizienten/Punkte, T-Bereich].
sub DerivedCp
{
    my $c = shift;
    my @p = @{ $c->{params} };
    my $t = $c->{type};
    if ($t == 3) { return ['constant', $p[0]]; }                                       # h = a*T + b*p + c
    if ($t == 6) { return ['polynomial', [ map { $_ * $p[$_] } 1 .. 7 ]]; }             # h = sum a_i T^i
    if ($t == 8) { return ['polynomial_cp', [ @p[0 .. 4] ]]; }                          # Shomate-Form
    if ($t == 0) {                                                                      # Tabelle: Steigungen
        my @pts = map { [ $p[2 * $_], $p[2 * $_ + 1] ] } 0 .. @p / 2 - 1;
        @pts = sort { $a->[0] <=> $b->[0] } @pts;
        my @slopes = map { [ 0.5 * ($pts[$_][0] + $pts[$_ + 1][0]), ($pts[$_ + 1][1] - $pts[$_][1]) / ($pts[$_ + 1][0] - $pts[$_][0]) ] } 0 .. $#pts - 1;
        my $allEqual = !grep { abs($_->[1] - $slopes[0][1]) > 1e-6 * abs($slopes[0][1]) } @slopes;
        return $allEqual ? ['constant', $slopes[0][1]] : ['table_T', \@slopes];
    }
    if ($t == 2) { return undef; }                                                      # konstant -> c_p = 0
    die "Enthalpie-Typ $t wird nicht unterstützt\n";
}

sub EvalCp
{
    my ($d, $T) = @_;
    my ($type, $x) = @$d;
    return $x if $type eq 'constant';
    if ($type eq 'polynomial') { my $s = 0; $s += $x->[$_] * $T**$_ for 0 .. $#$x; return $s; }
    if ($type eq 'polynomial_cp') { return $x->[0] + $x->[1] * $T + $x->[2] * $T**2 + $x->[3] * $T**3 + $x->[4] / $T**2; }
    if ($type eq 'table_T') {
        return $x->[0][1] if $T <= $x->[0][0];
        return $x->[-1][1] if $T >= $x->[-1][0];
        for my $i (0 .. $#$x - 1) {
            next if $T > $x->[$i + 1][0];
            my $f = ($T - $x->[$i][0]) / ($x->[$i + 1][0] - $x->[$i][0]);
            return $x->[$i][1] + $f * ($x->[$i + 1][1] - $x->[$i][1]);
        }
    }
    die "EvalCp: $type\n";
}

# Plausibler c_p-Wert bei T aus einer Enthalpie-Korrelation, sonst (undef, Grund).
sub DerivedCpAt
{
    my ($h, $T) = @_;
    my $d = DerivedCp($h);
    return (undef, 'Enthalpie ist konstant, also c_p = 0') unless $d;
    return (undef, "Enthalpie gilt nur für $h->{T}[0]-$h->{T}[1] K")
        if $T < $h->{T}[0] || $T > $h->{T}[1];
    my $cp = EvalCp($d, $T);
    if ($cp < 100 || $cp > 20000) {
        my $why = sprintf('dh/dT bei %.2f K wäre %.3g J/(kg K)', $T, $cp);
        $why .= '; die Parameter sind die NIST-Shomate-Koeffizienten von flüssigem Wasser, ohne Umrechnung T/1000 und mol -> kg'
            if $h->{type} == 8 && $h->{params}[0] == -203.606;
        return (undef, $why);
    }
    return ($cp, undef);
}

sub DerivedCpJson
{
    my ($h, $reference) = @_;
    my ($type, $x) = @{ DerivedCp($h) };
    my @o;
    if ($type eq 'constant') { push @o, type => 'constant', value => NumD($x); }
    elsif ($type eq 'polynomial') {
        my @a = @$x;
        pop @a while @a > 1 && $a[-1] == 0;
        push @o, type => 'polynomial', formula => 'y = a0 + a1*T + a2*T^2 + ... (T in K)', coefficients => [ map { NumD($_) } @a ];
    }
    elsif ($type eq 'polynomial_cp') {
        push @o, type => 'polynomial_cp', formula => 'y = a + b*T + c*T^2 + d*T^3 + e/T^2 (T in K)',
                 coefficients => [ map { NumD($_) } @$x ];
    }
    else { push @o, type => 'table_T', formula => 'Messpunkte [T in K, Wert]; dazwischen linear interpoliert'; }
    push @o, unit => 'J/(kg K)', T_range => [ Num($h->{T}[0]), Num($h->{T}[1]) ], p_range => [ Num($h->{p}[0]), Num($h->{p}[1]) ];
    push @o, source => $DERIVED, status => 'unchecked';
    push @o, reference => $reference if defined $reference;
    push @o, points => [ map { [ NumD($_->[0]), NumD($_->[1]) ] } @$x ] if $type eq 'table_T';
    return Obj(0, @o);
}

###########################################################################
# JSON-Ausgabe mit gezielter Formatierung (Reihenfolge der Felder bleibt
# erhalten, kurze Objekte und Zahlenpaare stehen in einer Zeile).

# Zahl unveraendert als Text, wenn sie gueltiges JSON ist.
sub Num
{
    my $s = shift;
    return bless \$s, 'NUM' if $s =~ /^-?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?$/;
    die "Keine Zahl: '$s'\n" unless looks_like_number($s);
    return NumD($s);
}

# Berechnete Zahl mit 6 gueltigen Stellen.
sub NumD
{
    my $x = shift;
    my $s = sprintf('%.6g', $x);
    die "Ungültige Zahl: $s\n" unless $s =~ /^-?(0|[1-9]\d*)(\.\d+)?([eE][+-]?\d+)?$/;
    return bless \$s, 'NUM';
}

# Geordnetes Objekt; $inline = 1 schreibt es in eine Zeile.
sub Obj { my ($inline, @pairs) = @_; return bless [@pairs], $inline ? 'IOBJ' : 'OBJ'; }

my $JSON_STRING;
sub Str { $JSON_STRING //= JSON::PP->new->allow_nonref; return $JSON_STRING->encode($_[0]); }

sub J
{
    my ($v, $ind) = @_;
    my $r = ref $v;
    return 'null' unless defined $v;
    return $$v if $r eq 'NUM';
    return Str($v) if $r eq '';
    my $in = "$ind    ";
    if ($r eq 'IOBJ' || $r eq 'OBJ') {
        my @a = @$v;
        return '{}' unless @a;
        my @parts;
        while (@a) { my ($k, $x) = splice @a, 0, 2; push @parts, Str($k) . ': ' . J($x, $in); }
        return '{ ' . join(', ', @parts) . ' }' if $r eq 'IOBJ';
        return "{\n" . join(",\n", map { "$in$_" } @parts) . "\n$ind}";
    }
    if ($r eq 'ARRAY') {
        return '[]' unless @$v;
        return '[' . join(', ', map { J($_, $in) } @$v) . ']' unless grep { ref($_) ne 'NUM' } @$v;
        return "[\n" . join(",\n", map { $in . J($_, $in) } @$v) . "\n$ind]";
    }
    die "J: unbekannter Typ $r\n";
}

###########################################################################
# Pruefung: Datei wieder einlesen und jede uebernommene Zahl vergleichen.

open my $chk, '<:raw', $outPath or die;
my $doc = JSON::PP->new->utf8->decode(do { local $/; <$chk> });
close $chk;

my ($nConst, $nCorr, $nNumbers, $nMat) = (0, 0, 0, 0);
my %byName = map { $_->{name} => $_ } @{ $doc->{materials} };
die "Anzahl Stoffe: " . scalar(keys %byName) . " statt " . scalar(@materials) . "\n" unless keys %byName == @materials;
for my $m (@materials) {
    my $jm = $byName{ $m->{name} } or die "Stoff $m->{name} fehlt\n";
    die "$m->{name}: Schlüssel\n" unless $jm->{dyssol_key} eq $m->{key};
    ++$nMat;
    for my $k (keys %{ $m->{const} }) {
        next if $k == $SOA;
        Same($jm->{constants}{ $CONST{$k}[0] }{value}, $m->{const}{$k}, "$m->{name}/$CONST{$k}[0]");
        ++$nConst;
    }
    for my $k (keys %{ $m->{tpd} }) {
        next if $k == $ENTHALPY;
        my $list = $jm->{properties}{ $TPD{$k}[0] } or die "$m->{name}/$TPD{$k}[0] fehlt\n";
        my $i = 0;
        for my $c (@{ $m->{tpd}{$k} }) {
            CheckCorrelation($list->[$i++], $c, "$m->{name}/$TPD{$k}[0]");
            ++$nCorr;
        }
    }
}
for my $i (0 .. $#interactions) {
    for my $k (keys %{ $interactions[$i]{tpd} }) {
        my $j = 0;
        CheckCorrelation($doc->{interactions}[$i]{properties}{ $INTER{$k}[0] }[$j++], $_, "Wechselwirkung") for @{ $interactions[$i]{tpd}{$k} };
        $nCorr += @{ $interactions[$i]{tpd}{$k} };
    }
}

sub Same
{
    my ($json, $text, $what) = @_;
    die "$what: $json statt $text\n" unless defined $json && $json == $text;
    ++$nNumbers;
}

sub CheckCorrelation
{
    my ($j, $c, $what) = @_;
    die "$what: Eintrag fehlt\n" unless $j;
    my @p = @{ $c->{params} };
    Same($j->{T_range}[0], $c->{T}[0], $what); Same($j->{T_range}[1], $c->{T}[1], $what);
    my $pr = $j->{p_range} // $j->{relative_humidity_range};
    Same($pr->[0], $c->{p}[0], $what); Same($pr->[1], $c->{p}[1], $what);
    if    ($c->{type} == 2)  { Same($j->{value}, $p[0], $what); }
    elsif ($c->{type} == 10) { Same($j->{y_ref}, $p[0], $what); Same($j->{T_ref}, $p[1], $what); Same($j->{S}, $p[2], $what); }
    elsif ($c->{type} == 12) { Same($j->{molar_mass}, $p[0], $what); }
    elsif ($c->{type} == 4)  { Same($j->{A}, $p[2], $what); Same(-$j->{B}, $p[5], $what); Same($j->{C}, $p[7], $what); }
    else {
        die "$what: Anzahl Punkte\n" unless @{ $j->{points} } * 2 == @p;
        for my $i (0 .. $#{ $j->{points} }) { Same($j->{points}[$i][0], $p[2 * $i], $what); Same($j->{points}[$i][1], $p[2 * $i + 1], $what); }
    }
}

print "Geschrieben: $outPath\n";
print "Prüfung bestanden: $nMat Stoffe, $nConst Konstanten, $nCorr Korrelationen, $nNumbers Zahlen identisch mit der Dyssol-Datei.\n";
print "Wärmekapazität:\n";
print "  $_\n" for @derivedReport;
my $nWarn = 0; $nWarn += @{ $_->{warnings} // [] } for @{ $doc->{materials} };
print "Warnungen: $nWarn\n";
