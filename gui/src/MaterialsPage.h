#pragma once
#include <QWidget>
#include "Materials.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;

// Sucht die Materialdatei: zuerst data/Materialien.json neben der .exe
// (spaetere Weitergabe), sonst im Quellordner des Projekts. So wirken
// Aenderungen an der Datei im Repository ohne neues Bauen.
QString FindMaterialFile();

// Reiter "Materialien" im Einstellungsfenster: zeigt die Stoffdaten aus
// data/Materialien.json mit Wert bei waehlbarem T und p, Quelle und
// Pruefstatus. Nur zum Ansehen - bearbeitet wird vorerst die Datei selbst,
// "Neu laden" liest sie wieder ein.
class MaterialsPage : public QWidget
{
    Q_OBJECT

public:
    explicit MaterialsPage(QWidget* parent = nullptr);

    // loadError: Meldung, falls das Einlesen beim Programmstart scheiterte.
    void SetDatabase(stosis::CMaterialDatabase* database, const QString& loadError);

signals:
    // Datei erfolgreich neu eingelesen (Stoffliste kann sich geaendert haben).
    void DatabaseReloaded();

private slots:
    void OnReload();
    void OnMaterialChanged();
    void UpdateValues();
    void UpdateDetails();

private:
    const stosis::SMaterial* CurrentMaterial() const;
    void RefreshMaterialList(const QString& selectName);
    void ShowLoadStatus(bool ok, const QString& message);

    stosis::CMaterialDatabase* m_database = nullptr;

    QLabel* m_fileLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QComboBox* m_materialCombo = nullptr;
    QLabel* m_infoLabel = nullptr;
    QLabel* m_warningsLabel = nullptr;
    QDoubleSpinBox* m_temperatureSpin = nullptr;
    QDoubleSpinBox* m_pressureSpin = nullptr;
    QLabel* m_temperatureHint = nullptr;
    QLabel* m_pressureHint = nullptr;
    QTableWidget* m_propertyTable = nullptr;
    QTableWidget* m_constantTable = nullptr;
    QLabel* m_detailLabel = nullptr;
    QLabel* m_notesLabel = nullptr;
};
