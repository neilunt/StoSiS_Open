#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>

// Folders and suggested names for files that the user saves.
namespace save_locations
{
    // StoSiS folder: when installed, data/ lies next to the program;
    // otherwise the source folder.
    inline QString StoSiSFolder()
    {
        const QString besideExe = QCoreApplication::applicationDirPath();
        return QFileInfo::exists(besideExe + "/data") ? besideExe : QDir(QString(STOSIS_DATA_DIR) + "/..").absolutePath();
    }

    // Subfolder of the StoSiS folder, created if it is missing. If it cannot
    // be created (no write access), the StoSiS folder itself.
    inline QString Subfolder(const QString& name)
    {
        const QDir root(StoSiSFolder());
        return root.mkpath(name) ? root.filePath(name) : root.path();
    }

    // Name of the open flowsheet file without extension ("" = no file).
    inline QString& CurrentFlowsheetName()
    {
        static QString name;
        return name;
    }

    inline void SetCurrentFlowsheet(const QString& path)
    {
        CurrentFlowsheetName() = path.isEmpty() ? QString() : QFileInfo(path).completeBaseName();
    }

    // "StoSiS_<kind>_<flowsheet>.<extension>"; without the flowsheet part
    // as long as the flowsheet has no file.
    inline QString SuggestedName(const QString& kind, const QString& extension)
    {
        const QString& flowsheet = CurrentFlowsheetName();
        return "StoSiS_" + kind + (flowsheet.isEmpty() ? QString() : "_" + flowsheet) + "." + extension;
    }
}
