#include "ScreenEditorWidget.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <cmath>
#include <algorithm>

// Grade efficiency T(d) over the size range of the grid, size axis
// logarithmic (linear if the range starts at 0), cut size dashed.
class GradeEfficiencyPreview : public QWidget
{
public:
    explicit GradeEfficiencyPreview(QWidget* parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(180);
    }

    void SetCurve(const stosis::SScreenSettings& settings, double sizeMin, double sizeMax, bool valid)
    {
        m_settings = settings;
        m_sizeMin = sizeMin;
        m_sizeMax = sizeMax;
        m_valid = valid;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QColor ink = palette().color(QPalette::WindowText);
        QColor faint = ink;
        faint.setAlpha(70);
        const QRect plot = rect().adjusted(50, 10, -15, -38);
        if (plot.width() < 40 || plot.height() < 40 || !(m_sizeMax > m_sizeMin))
            return;

        const double f = stosis::kSizeDisplayFactor;
        const bool logAxis = m_sizeMin > 0.0;
        const double x0 = logAxis ? std::log10(m_sizeMin * f) : m_sizeMin * f;
        const double x1 = logAxis ? std::log10(m_sizeMax * f) : m_sizeMax * f;
        const auto toPixelX = [&](double sizeDisplay)
        {
            const double x = logAxis ? std::log10(sizeDisplay) : sizeDisplay;
            return plot.left() + (x - x0) / (x1 - x0) * plot.width();
        };
        const auto toPixelY = [&](double t) { return plot.bottom() - t * plot.height(); };

        // Grid and ticks: T in steps of 0.25; size 1-2-5 per decade (log) or 5 steps (linear).
        QPen gridPen(faint, 1, Qt::DotLine);
        painter.setPen(ink);
        const QFontMetrics metrics(font());
        for (int i = 0; i <= 4; ++i)
        {
            const double t = 0.25 * i;
            const double y = toPixelY(t);
            painter.setPen(gridPen);
            painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            painter.setPen(ink);
            const QString label = QString::number(t, 'g', 2).replace('.', ',');
            painter.drawText(QRectF(0, y - 8, plot.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter, label);
        }
        std::vector<double> ticks;
        if (logAxis)
        {
            for (int k = static_cast<int>(std::floor(x0)); k <= static_cast<int>(std::ceil(x1)); ++k)
                for (double m : { 1.0, 2.0, 5.0 })
                {
                    const double value = m * std::pow(10.0, k);
                    const double x = std::log10(value);
                    if (x >= x0 - 1e-9 && x <= x1 + 1e-9)
                        ticks.push_back(value);
                }
        }
        else
        {
            for (int i = 0; i <= 5; ++i)
                ticks.push_back(x0 + (x1 - x0) * i / 5.0);
        }
        double lastRight = -1e9;
        for (double value : ticks)
        {
            const double x = toPixelX(value);
            painter.setPen(gridPen);
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            painter.setPen(ink);
            const QString label = QString::number(value, 'g', 3).replace('.', ',');
            const int width = metrics.horizontalAdvance(label);
            if (x - width / 2.0 > lastRight + 4)
            {
                painter.drawText(QRectF(x - width / 2.0, plot.bottom() + 3, width, 16), Qt::AlignCenter, label);
                lastRight = x + width / 2.0;
            }
        }
        painter.setPen(ink);
        painter.drawRect(plot);
        painter.drawText(QRectF(plot.left(), plot.bottom() + 19, plot.width(), 16), Qt::AlignCenter,
            QString("Partikelgroesse d [%1]").arg(QString::fromUtf8(stosis::kSizeDisplayUnit)));
        painter.save();
        painter.translate(12, plot.center().y());
        painter.rotate(-90);
        painter.drawText(QRectF(-plot.height() / 2.0, -8, plot.height(), 16), Qt::AlignCenter, "T(d) ins Grobgut");
        painter.restore();
        if (!m_valid)
            return;

        // Cut size
        const double cut = m_settings.cutSize * f;
        if (cut > 0.0 && cut >= m_sizeMin * f && cut <= m_sizeMax * f)
        {
            painter.setPen(QPen(ink, 1, Qt::DashLine));
            const double x = toPixelX(cut);
            painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            painter.drawText(QPointF(x + 4, plot.top() + 14), "x_cut");
        }

        // Curve
        QPainterPath path;
        const int numPoints = std::max(2, plot.width());
        for (int i = 0; i <= numPoints; ++i)
        {
            const double x = x0 + (x1 - x0) * i / numPoints;
            const double sizeDisplay = logAxis ? std::pow(10.0, x) : x;
            const QPointF point(toPixelX(sizeDisplay), toPixelY(m_settings.GradeEfficiency(sizeDisplay / f)));
            if (i == 0)
                path.moveTo(point);
            else
                path.lineTo(point);
        }
        painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
        painter.drawPath(path);
    }

private:
    stosis::SScreenSettings m_settings;
    double m_sizeMin = 0.0;
    double m_sizeMax = 0.0;
    bool m_valid = false;
};

ScreenEditorWidget::ScreenEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    SetupUi();
}

void ScreenEditorWidget::SetupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* group = new QGroupBox("Trennkurve", this);
    auto* form = new QFormLayout(group);

    m_modelCombo = new QComboBox(group);
    m_modelCombo->addItem("Plitt", static_cast<int>(stosis::EScreenModel::Plitt));
    m_modelCombo->addItem("Molerus-Hoffmann", static_cast<int>(stosis::EScreenModel::MolerusHoffmann));
    m_modelCombo->addItem("Teipel-Hennig", static_cast<int>(stosis::EScreenModel::TeipelHennig));
    form->addRow("Modell:", m_modelCombo);

    m_cutSizeSpin = new QDoubleSpinBox(group);
    m_cutSizeSpin->setRange(1e-3, 1e7);
    m_cutSizeSpin->setDecimals(3);
    m_cutSizeSpin->setSuffix(QString(" ") + QString::fromUtf8(stosis::kSizeDisplayUnit));
    form->addRow("Trenngroesse x_cut:", m_cutSizeSpin);

    m_alphaSpin = new QDoubleSpinBox(group);
    m_alphaSpin->setRange(0.0, 100.0);
    m_alphaSpin->setDecimals(3);
    m_alphaSpin->setSingleStep(0.5);
    form->addRow("Trennschaerfe alpha:", m_alphaSpin);

    m_betaLabel = new QLabel("beta:", group);
    m_betaSpin = new QDoubleSpinBox(group);
    m_betaSpin->setRange(0.001, 100.0);
    m_betaSpin->setDecimals(3);
    m_betaSpin->setSingleStep(0.1);
    form->addRow(m_betaLabel, m_betaSpin);

    m_offsetLabel = new QLabel("Offset (Feingut im Grobgut):", group);
    m_offsetSpin = new QDoubleSpinBox(group);
    m_offsetSpin->setRange(0.0, 1.0);
    m_offsetSpin->setDecimals(3);
    m_offsetSpin->setSingleStep(0.01);
    form->addRow(m_offsetLabel, m_offsetSpin);

    m_errorLabel = new QLabel(group);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setStyleSheet("color: rgb(200, 40, 40);");
    m_errorLabel->setVisible(false);
    form->addRow(m_errorLabel);
    mainLayout->addWidget(group);

    auto* previewGroup = new QGroupBox("Vorschau", this);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    m_preview = new GradeEfficiencyPreview(previewGroup);
    previewLayout->addWidget(m_preview, 1);
    mainLayout->addWidget(previewGroup, 1);

    m_applyButton = new QPushButton("Uebernehmen", this);
    mainLayout->addWidget(m_applyButton);

    connect(m_modelCombo, &QComboBox::currentIndexChanged, this, &ScreenEditorWidget::OnValuesChanged);
    for (QDoubleSpinBox* spin : { m_cutSizeSpin, m_alphaSpin, m_betaSpin, m_offsetSpin })
        connect(spin, &QDoubleSpinBox::valueChanged, this, &ScreenEditorWidget::OnValuesChanged);
    connect(m_applyButton, &QPushButton::clicked, this, &ScreenEditorWidget::OnApply);
}

void ScreenEditorWidget::SetGraph(stosis::CFlowsheetGraph* graph)
{
    m_graph = graph;
    LoadNode();
}

void ScreenEditorWidget::SetNode(const std::string& nodeId)
{
    m_nodeId = nodeId;
    LoadNode();
}

void ScreenEditorWidget::LoadNode()
{
    const stosis::CScreenUnit* screen = m_graph ? m_graph->GetScreen(m_nodeId) : nullptr;
    const stosis::SScreenSettings settings = screen ? screen->GetSettings() : stosis::SScreenSettings{};
    const QSignalBlocker b0(m_modelCombo), b1(m_cutSizeSpin), b2(m_alphaSpin), b3(m_betaSpin), b4(m_offsetSpin);
    m_modelCombo->setCurrentIndex(std::max(0, m_modelCombo->findData(static_cast<int>(settings.model))));
    m_cutSizeSpin->setValue(settings.cutSize * stosis::kSizeDisplayFactor);
    m_alphaSpin->setValue(settings.alpha);
    m_betaSpin->setValue(settings.beta);
    m_offsetSpin->setValue(settings.offset);
    OnValuesChanged();
}

stosis::SScreenSettings ScreenEditorWidget::CurrentSettings() const
{
    stosis::SScreenSettings settings;
    settings.model = static_cast<stosis::EScreenModel>(m_modelCombo->currentData().toInt());
    settings.cutSize = m_cutSizeSpin->value() / stosis::kSizeDisplayFactor;
    settings.alpha = m_alphaSpin->value();
    settings.beta = m_betaSpin->value();
    settings.offset = m_offsetSpin->value();
    return settings;
}

void ScreenEditorWidget::OnValuesChanged()
{
    const bool teipelHennig = CurrentSettings().model == stosis::EScreenModel::TeipelHennig;
    m_betaLabel->setVisible(teipelHennig);
    m_betaSpin->setVisible(teipelHennig);
    m_offsetLabel->setVisible(teipelHennig);
    m_offsetSpin->setVisible(teipelHennig);

    const QString problems = QString::fromStdString(CurrentSettings().Check());
    m_errorLabel->setText(problems);
    m_errorLabel->setVisible(!problems.isEmpty());
    m_applyButton->setEnabled(m_graph && m_graph->GetScreen(m_nodeId) && problems.isEmpty());
    UpdatePreview();
}

void ScreenEditorWidget::UpdatePreview()
{
    if (!m_graph)
    {
        m_preview->SetCurve(CurrentSettings(), 0.0, 0.0, false);
        return;
    }
    const stosis::SSimulationGrid& grid = m_graph->GetGrid();
    m_preview->SetCurve(CurrentSettings(), grid.sizeMin, grid.sizeMax, CurrentSettings().Check().empty());
}

void ScreenEditorWidget::OnApply()
{
    stosis::CScreenUnit* screen = m_graph ? m_graph->GetScreen(m_nodeId) : nullptr;
    if (!screen)
        return;
    if (!screen->SetSettings(CurrentSettings()))
    {
        m_errorLabel->setText(QString::fromStdString(CurrentSettings().Check()));
        m_errorLabel->setVisible(true);
        return;
    }
    emit Applied();
}
