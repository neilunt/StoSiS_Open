#pragma once
#include <QWidget>
#include <QPoint>
#include <QColor>
#include <QString>
#include <vector>

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

// Bar above the flowsheet with all blocks from GetNodeTemplates(), ordered
// by groups with a line between neighbouring groups. If the width is not
// enough, the groups wrap into further rows and the bar grows in height.
class FlowsheetPaletteWidget : public QWidget
{
public:
    explicit FlowsheetPaletteWidget(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    // Places the groups row by row for the given width (only if apply) and
    // returns the height needed.
    int ArrangeGroups(int width, bool apply) const;

    std::vector<QWidget*> m_groups;
};
