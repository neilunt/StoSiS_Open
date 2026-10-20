#pragma once
#include <QWidget>
#include <QPoint>
#include <QColor>
#include <QString>

// Eine Kachel in der Block-Palette: farbig wie der Block im Flowsheet,
// per Maus ins Flowsheet ziehbar (Drag & Drop).
class PaletteTile : public QWidget
{
public:
    PaletteTile(size_t templateIndex, QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    size_t m_templateIndex = 0;
    QString m_text;
    QColor m_color;
    QPoint m_pressPos;
    bool m_pressed = false;
};

// Waagerechte Leiste ueber dem Flowsheet mit allen Bloecken aus
// GetNodeTemplates(), nach Gruppen geordnet. Wird sie zu schmal, laesst
// sie sich seitlich scrollen.
class FlowsheetPaletteWidget : public QWidget
{
public:
    explicit FlowsheetPaletteWidget(QWidget* parent = nullptr);
};
