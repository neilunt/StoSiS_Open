#include "MaterialsPage.h"
#include <QAbstractItemView>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTime>
#include <QVBoxLayout>
#include <map>

namespace
{
    const QColor kCheckedColor(46, 125, 50);   // gruen
    const QColor kUncheckedColor(179, 92, 0);  // orange
    const QColor kErrorColor(198, 40, 40);     // rot

    // Anzeigenamen; unbekannte Schluessel erscheinen so wie in der Datei.
    QString Label(const std::string& key)
    {
        static const std::map<std::string, const char*> labels = {
            { "density", "Dichte" },
            { "heat_capacity", "Waermekapazitaet c_p" },
            { "viscosity", "Viskositaet" },
            { "thermal_conductivity", "Waermeleitfaehigkeit" },
            { "vapor_pressure", "Dampfdruck" },
            { "equilibrium_moisture_content", "Gleichgewichtsfeuchte" },
            { "permittivity", "Permittivitaet" },
            { "mass_diffusion_coefficient", "Diffusionskoeffizient" },
            { "interface_tension", "Grenzflaechenspannung" },
            { "critical_pressure", "Kritischer Druck" },
            { "critical_temperature", "Kritische Temperatur" },
            { "heat_of_fusion", "Schmelzenthalpie" },
            { "heat_of_vaporization", "Verdampfungsenthalpie" },
            { "molar_mass", "Molmasse" },
            { "normal_boiling_point", "Siedepunkt (1 atm)" },
            { "normal_freezing_point", "Schmelzpunkt (1 atm)" },
            { "formation_enthalpy", "Bildungsenthalpie" },
            { "bond_work_index", "Bond-Arbeitsindex" },
        };
        const auto it = labels.find(key);
        return it != labels.end() ? QString(it->second) : QString::fromStdString(key);
    }

    QString StateLabel(const std::string& state)
    {
        if (state == "solid")
            return "fest";
        if (state == "liquid")
            return "fluessig";
        if (state == "gas")
            return "gasfoermig";
        return QString::fromStdString(state);
    }

    QString TypeLabel(const stosis::SCorrelation& c)
    {
        switch (c.type)
        {
        case stosis::ECorrelationType::Constant:              return "Konstante";
        case stosis::ECorrelationType::TableT:                return "Messpunkte ueber T";
        case stosis::ECorrelationType::TableRelativeHumidity: return "Messpunkte ueber rel. Luftfeuchte";
        case stosis::ECorrelationType::IdealGas:              return "Ideales Gas";
        case stosis::ECorrelationType::Sutherland:            return "Sutherland";
        case stosis::ECorrelationType::Antoine:               return "Antoine-Gleichung";
        case stosis::ECorrelationType::Polynomial:            return "Polynom";
        case stosis::ECorrelationType::PolynomialCp:          return "Polynom (c_p-Form)";
        }
        return QString::fromStdString(c.typeName);
    }

    QString Number(double value) { return QString::number(value, 'g', 6); }

    QString StatusText(const std::string& status)
    {
        if (status == "checked")
            return "geprueft";
        if (status == "unchecked")
            return "ungeprueft";
        return QString::fromStdString(status);
    }

    QTableWidgetItem* StatusItem(const std::string& status)
    {
        auto* item = new QTableWidgetItem(StatusText(status));
        item->setForeground(status == "checked" ? kCheckedColor : kUncheckedColor);
        return item;
    }

    QString Html(const std::string& text) { return QString::fromStdString(text).toHtmlEscaped(); }

    QString ParameterText(const stosis::SCorrelation& c)
    {
        const std::vector<double>& a = c.parameters;
        switch (c.type)
        {
        case stosis::ECorrelationType::Constant:
            return "Wert = " + Number(a[0]);
        case stosis::ECorrelationType::IdealGas:
            return "M = " + Number(a[0]) + " kg/mol";
        case stosis::ECorrelationType::Sutherland:
            return QString("y_ref = %1, T_ref = %2 K, S = %3 K").arg(Number(a[0]), Number(a[1]), Number(a[2]));
        case stosis::ECorrelationType::Antoine:
            return QString("A = %1, B = %2, C = %3").arg(Number(a[0]), Number(a[1]), Number(a[2]));
        case stosis::ECorrelationType::Polynomial:
        case stosis::ECorrelationType::PolynomialCp:
        {
            QStringList values;
            for (double value : a)
                values << Number(value);
            return "Koeffizienten: " + values.join(", ");
        }
        case stosis::ECorrelationType::TableT:
        case stosis::ECorrelationType::TableRelativeHumidity:
            return QString("%1 Messpunkte, x von %2 bis %3")
                .arg(c.points.size()).arg(Number(c.points.front().first), Number(c.points.back().first));
        }
        return QString();
    }

    QTableWidget* CreateTable(QWidget* parent)
    {
        auto* table = new QTableWidget(0, 4, parent);
        table->setHorizontalHeaderLabels({ "Eigenschaft", "Wert", "Einheit", "Status" });
        table->verticalHeader()->hide();
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        // Tabelle so hoch wie ihr Inhalt; gescrollt wird die ganze Seite.
        table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        table->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
        table->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        table->horizontalHeader()->setStretchLastSection(true);
        return table;
    }

    // Wie viele Werte (Konstanten + Eintraege) sind schon geprueft?
    std::pair<int, int> CountChecked(const stosis::CMaterialDatabase& database)
    {
        int checked = 0;
        int total = 0;
        for (const stosis::SMaterial& material : database.GetMaterials())
        {
            for (const auto& entry : material.constants)
            {
                ++total;
                checked += entry.second.status == "checked";
            }
            for (const auto& entry : material.properties)
                for (const stosis::SCorrelation& c : entry.second.correlations)
                {
                    ++total;
                    checked += c.status == "checked";
                }
        }
        return { checked, total };
    }
}

QString FindMaterialFile()
{
    const QString besideExe = QCoreApplication::applicationDirPath() + "/data/Materialien.json";
    if (QFileInfo::exists(besideExe))
        return besideExe;
    return QString(STOSIS_DATA_DIR) + "/Materialien.json";
}

MaterialsPage::MaterialsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    outerLayout->addWidget(scrollArea);

    auto* content = new QWidget(scrollArea);
    scrollArea->setWidget(content);
    auto* layout = new QVBoxLayout(content);

    auto* fileRow = new QHBoxLayout();
    fileRow->addWidget(new QLabel("Datei:", content));
    m_fileLabel = new QLabel(content);
    m_fileLabel->setWordWrap(true);
    m_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    fileRow->addWidget(m_fileLabel, 1);
    auto* reloadButton = new QPushButton("Neu laden", content);
    reloadButton->setToolTip("Liest die Datei erneut ein, z.B. nach einer Aenderung im Editor.");
    connect(reloadButton, &QPushButton::clicked, this, &MaterialsPage::OnReload);
    fileRow->addWidget(reloadButton);
    layout->addLayout(fileRow);

    m_statusLabel = new QLabel(content);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_statusLabel);

    auto* materialRow = new QHBoxLayout();
    materialRow->addWidget(new QLabel("Stoff:", content));
    m_materialCombo = new QComboBox(content);
    connect(m_materialCombo, &QComboBox::currentIndexChanged, this, &MaterialsPage::OnMaterialChanged);
    materialRow->addWidget(m_materialCombo, 1);
    layout->addLayout(materialRow);

    m_infoLabel = new QLabel(content);
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_infoLabel);

    m_warningsLabel = new QLabel(content);
    m_warningsLabel->setWordWrap(true);
    m_warningsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_warningsLabel->setStyleSheet("QLabel { background: #FFF4CE; color: #222; border: 1px solid #E0C060; padding: 4px; }");
    layout->addWidget(m_warningsLabel);

    // Bedingungen, bei denen die Werte ausgewertet werden.
    auto* conditions = new QGridLayout();
    conditions->addWidget(new QLabel("Temperatur T [K]:", content), 0, 0);
    m_temperatureSpin = new QDoubleSpinBox(content);
    m_temperatureSpin->setRange(1.0, 10000.0);
    m_temperatureSpin->setDecimals(2);
    m_temperatureSpin->setValue(stosis::kRoomTemperature);
    conditions->addWidget(m_temperatureSpin, 0, 1);
    m_temperatureHint = new QLabel(content);
    conditions->addWidget(m_temperatureHint, 0, 2);

    conditions->addWidget(new QLabel("Druck p [Pa]:", content), 1, 0);
    m_pressureSpin = new QDoubleSpinBox(content);
    m_pressureSpin->setRange(1.0, 1.0e9);
    m_pressureSpin->setDecimals(0);
    m_pressureSpin->setSingleStep(1000.0);
    m_pressureSpin->setValue(stosis::kRoomPressure);
    conditions->addWidget(m_pressureSpin, 1, 1);
    m_pressureHint = new QLabel(content);
    conditions->addWidget(m_pressureHint, 1, 2);
    conditions->setColumnStretch(2, 1);
    layout->addLayout(conditions);
    connect(m_temperatureSpin, &QDoubleSpinBox::valueChanged, this, &MaterialsPage::UpdateValues);
    connect(m_pressureSpin, &QDoubleSpinBox::valueChanged, this, &MaterialsPage::UpdateValues);

    layout->addWidget(new QLabel("<b>Eigenschaften bei T und p</b>", content));
    m_propertyTable = CreateTable(content);
    layout->addWidget(m_propertyTable);

    layout->addWidget(new QLabel("<b>Konstanten</b>", content));
    m_constantTable = CreateTable(content);
    layout->addWidget(m_constantTable);

    // Immer nur eine Zeile in beiden Tabellen markiert.
    connect(m_propertyTable, &QTableWidget::itemSelectionChanged, this, [this]()
        {
            if (!m_propertyTable->selectedItems().isEmpty())
            {
                m_constantTable->blockSignals(true);
                m_constantTable->clearSelection();
                m_constantTable->blockSignals(false);
            }
            UpdateDetails();
        });
    connect(m_constantTable, &QTableWidget::itemSelectionChanged, this, [this]()
        {
            if (!m_constantTable->selectedItems().isEmpty())
            {
                m_propertyTable->blockSignals(true);
                m_propertyTable->clearSelection();
                m_propertyTable->blockSignals(false);
            }
            UpdateDetails();
        });

    layout->addWidget(new QLabel("<b>Details</b>", content));
    m_detailLabel = new QLabel(content);
    m_detailLabel->setWordWrap(true);
    m_detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailLabel->setFrameShape(QFrame::StyledPanel);
    m_detailLabel->setMargin(4);
    m_detailLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->addWidget(m_detailLabel);

    m_notesLabel = new QLabel(content);
    m_notesLabel->setWordWrap(true);
    layout->addWidget(m_notesLabel);
    layout->addStretch();
}

void MaterialsPage::SetDatabase(stosis::CMaterialDatabase* database, const QString& loadError)
{
    m_database = database;
    m_fileLabel->setText(database && !database->GetFilePath().empty()
        ? QString::fromStdString(database->GetFilePath()) : FindMaterialFile());
    if (!loadError.isEmpty())
        ShowLoadStatus(false, "Einlesen fehlgeschlagen:\n" + loadError);
    else
        ShowLoadStatus(true, "Geladen.");
    RefreshMaterialList(QString());
}

void MaterialsPage::OnReload()
{
    if (!m_database)
        return;
    const QString previous = m_materialCombo->currentData().toString();
    const QString path = m_database->GetFilePath().empty()
        ? FindMaterialFile() : QString::fromStdString(m_database->GetFilePath());
    m_fileLabel->setText(path);

    std::string error;
    if (m_database->LoadFromFile(path.toStdString(), error))
    {
        ShowLoadStatus(true, "Neu geladen um " + QTime::currentTime().toString("HH:mm:ss") + ".");
        emit DatabaseReloaded();
    }
    else
        ShowLoadStatus(false, "Neu laden fehlgeschlagen - es gilt weiter der vorherige Stand:\n" + QString::fromStdString(error));
    RefreshMaterialList(previous);
}

void MaterialsPage::ShowLoadStatus(bool ok, const QString& message)
{
    QString text = message;
    if (ok && m_database)
    {
        int withWarnings = 0;
        for (const stosis::SMaterial& material : m_database->GetMaterials())
            withWarnings += !material.warnings.empty();
        const auto [checked, total] = CountChecked(*m_database);
        text += QString(" %1 Stoffe, davon %2 mit Auffaelligkeiten; %3 von %4 Werten geprueft.")
            .arg(m_database->GetMaterials().size()).arg(withWarnings).arg(checked).arg(total);
    }
    m_statusLabel->setText(text);
    QPalette palette = m_statusLabel->palette();
    palette.setColor(QPalette::WindowText, ok ? kCheckedColor : kErrorColor);
    m_statusLabel->setPalette(palette);
}

void MaterialsPage::RefreshMaterialList(const QString& selectName)
{
    m_materialCombo->blockSignals(true);
    m_materialCombo->clear();
    if (m_database)
    {
        for (const stosis::SMaterial& material : m_database->GetMaterials())
        {
            QString text = QString::fromStdString(material.name) + " (" + StateLabel(material.state);
            if (!material.warnings.empty())
                text += QString(", %1 Auffaelligkeit%2").arg(material.warnings.size()).arg(material.warnings.size() == 1 ? "" : "en");
            m_materialCombo->addItem(text + ")", QString::fromStdString(material.name));
        }
    }
    const int index = m_materialCombo->findData(selectName);
    m_materialCombo->setCurrentIndex(index >= 0 ? index : (m_materialCombo->count() > 0 ? 0 : -1));
    m_materialCombo->blockSignals(false);
    OnMaterialChanged();
}

const stosis::SMaterial* MaterialsPage::CurrentMaterial() const
{
    if (!m_database || m_materialCombo->currentIndex() < 0)
        return nullptr;
    return m_database->FindByName(m_materialCombo->currentData().toString().toStdString());
}

void MaterialsPage::OnMaterialChanged()
{
    const stosis::SMaterial* material = CurrentMaterial();

    m_infoLabel->setText(material
        ? QString("Zustand: %1 (Quelle: %2)<br>Dyssol-Schluessel: %3")
              .arg(StateLabel(material->state), Html(material->stateSource), Html(material->dyssolKey))
        : QString());

    if (material && !material->warnings.empty())
    {
        QString html = "<b>Auffaelligkeiten - hier zuerst pruefen:</b><ul style=\"margin: 0px;\">";
        for (const std::string& warning : material->warnings)
            html += "<li>" + Html(warning) + "</li>";
        m_warningsLabel->setText(html + "</ul>");
        m_warningsLabel->show();
    }
    else
    {
        m_warningsLabel->hide();
    }

    QString notes;
    if (material)
        for (const std::string& note : material->notes)
            notes += "Hinweis: " + Html(note) + "<br>";
    m_notesLabel->setText(notes);
    m_notesLabel->setVisible(!notes.isEmpty());

    m_constantTable->clearContents();
    m_constantTable->setRowCount(material ? static_cast<int>(material->constants.size()) : 0);
    if (material)
    {
        int row = 0;
        for (const auto& [key, constant] : material->constants)
        {
            auto* labelItem = new QTableWidgetItem(Label(key));
            labelItem->setData(Qt::UserRole, QString::fromStdString(key));
            m_constantTable->setItem(row, 0, labelItem);
            m_constantTable->setItem(row, 1, new QTableWidgetItem(Number(constant.value)));
            m_constantTable->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(constant.unit)));
            m_constantTable->setItem(row, 3, StatusItem(constant.status));
            ++row;
        }
    }

    m_propertyTable->clearSelection();
    UpdateValues();
}

void MaterialsPage::UpdateValues()
{
    const double T = m_temperatureSpin->value();
    const double p = m_pressureSpin->value();
    const QLocale locale;
    m_temperatureHint->setText(QString("= %1 %2C").arg(locale.toString(T - 273.15, 'f', 2)).arg(QChar(0x00B0)));
    m_pressureHint->setText(QString("= %1 bar").arg(locale.toString(p / 1.0e5, 'f', 3)));

    // Markierte Eigenschaft bleibt markiert.
    QString selectedKey;
    const QList<QTableWidgetItem*> selection = m_propertyTable->selectedItems();
    if (!selection.isEmpty())
        selectedKey = m_propertyTable->item(selection.first()->row(), 0)->data(Qt::UserRole).toString();

    const stosis::SMaterial* material = CurrentMaterial();
    m_propertyTable->blockSignals(true);
    m_propertyTable->clearContents();
    m_propertyTable->setRowCount(material ? static_cast<int>(material->properties.size()) : 0);
    if (material)
    {
        int row = 0;
        for (const auto& [key, property] : material->properties)
        {
            const stosis::SPropertyValue value = property.Evaluate(T, p);
            const stosis::SCorrelation& used = *value.correlation;

            auto* labelItem = new QTableWidgetItem(Label(key));
            labelItem->setData(Qt::UserRole, QString::fromStdString(key));
            m_propertyTable->setItem(row, 0, labelItem);

            QTableWidgetItem* valueItem = nullptr;
            if (used.DependsOnHumidity())
            {
                valueItem = new QTableWidgetItem("haengt von der Luftfeuchte ab");
            }
            else
            {
                valueItem = new QTableWidgetItem(Number(value.value) + (value.outOfRange ? " *" : ""));
                if (value.outOfRange)
                {
                    valueItem->setForeground(kUncheckedColor);
                    if (used.IsInRange(T, p))
                        valueItem->setToolTip(QString("Ausserhalb der Messpunkte (%1 bis %2 K) - es gilt der Wert des Randpunkts.")
                            .arg(Number(used.points.front().first), Number(used.points.back().first)));
                    else
                        valueItem->setToolTip(QString("Ausserhalb des Gueltigkeitsbereichs (T %1 bis %2 K, p %3 bis %4 Pa) - "
                                                      "es gilt der Wert am Rand des Bereichs.")
                            .arg(Number(used.TMin), Number(used.TMax), Number(used.pMin), Number(used.pMax)));
                }
            }
            m_propertyTable->setItem(row, 1, valueItem);
            m_propertyTable->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(used.unit)));
            m_propertyTable->setItem(row, 3, StatusItem(used.status));
            if (QString::fromStdString(key) == selectedKey)
                m_propertyTable->selectRow(row);
            ++row;
        }
    }
    m_propertyTable->blockSignals(false);
    UpdateDetails();
}

void MaterialsPage::UpdateDetails()
{
    const stosis::SMaterial* material = CurrentMaterial();
    const QList<QTableWidgetItem*> propertySelection = m_propertyTable->selectedItems();
    const QList<QTableWidgetItem*> constantSelection = m_constantTable->selectedItems();
    if (!material || (propertySelection.isEmpty() && constantSelection.isEmpty()))
    {
        m_detailLabel->setText("Eine Zeile anklicken, um Formel, Gueltigkeitsbereich und Quelle zu sehen.\n"
                               "* = ausserhalb des Gueltigkeitsbereichs bzw. der Messpunkte, es gilt der Randwert.");
        return;
    }

    QString html;
    if (!constantSelection.isEmpty())
    {
        const std::string key = m_constantTable->item(constantSelection.first()->row(), 0)->data(Qt::UserRole).toString().toStdString();
        const stosis::SConstant* constant = material->FindConstant(key);
        if (!constant)
            return;
        html = QString("<b>%1</b> (<code>%2</code>)<br>Wert: %3 %4<br>Quelle: %5")
            .arg(Label(key), Html(key), Number(constant->value), Html(constant->unit), Html(constant->source));
        if (!constant->reference.empty())
            html += "<br>Referenz: " + Html(constant->reference);
        html += "<br>Status: " + StatusText(constant->status);
        m_detailLabel->setText(html);
        return;
    }

    const std::string key = m_propertyTable->item(propertySelection.first()->row(), 0)->data(Qt::UserRole).toString().toStdString();
    const stosis::SProperty* property = material->FindProperty(key);
    if (!property)
        return;
    const stosis::SPropertyValue value = property->Evaluate(m_temperatureSpin->value(), m_pressureSpin->value());

    html = QString("<b>%1</b> (<code>%2</code>)").arg(Label(key), Html(key));
    const size_t count = property->correlations.size();
    for (size_t i = 0; i < count; ++i)
    {
        const stosis::SCorrelation& c = property->correlations[i];
        html += "<hr>";
        if (count > 1)
            html += QString("Eintrag %1 von %2%3<br>").arg(i + 1).arg(count)
                .arg(&c == value.correlation ? " - <b>wird bei T und p verwendet</b>" : "");
        html += "Art: " + TypeLabel(c);
        if (!c.formula.empty())
            html += " - " + Html(c.formula);
        html += "<br>" + ParameterText(c);
        html += QString("<br>Gueltig fuer: T = %1 bis %2 K, %3 = %4 bis %5")
            .arg(Number(c.TMin), Number(c.TMax), c.DependsOnHumidity() ? "rel. Luftfeuchte" : "p")
            .arg(Number(c.pMin), Number(c.pMax) + (c.DependsOnHumidity() ? "" : " Pa"));
        html += "<br>Quelle: " + Html(c.source);
        if (!c.reference.empty())
            html += "<br>Referenz: " + Html(c.reference);
        if (!c.note.empty())
            html += "<br>Hinweis: " + Html(c.note);
        html += "<br>Status: " + StatusText(c.status);
    }
    m_detailLabel->setText(html);
}
