#pragma once
#include <QString>
#include <QStringList>
#include <string>

// Ergebnis-Eintraege (SResultsEntry) bestehen aus Knoten-ID + Port. Die
// Auswahllisten der Ergebnisfenster speichern beides als einen
// QString-Schluessel "nodeId|port" in den Item-Daten.
inline QString MakeResultsKey(const std::string& nodeId, size_t port)
{
    return QString::fromStdString(nodeId) + "|" + QString::number(port);
}

inline bool ParseResultsKey(const QString& key, std::string& nodeId, size_t& port)
{
    const QStringList parts = key.split('|');
    if (parts.size() != 2)
        return false;
    nodeId = parts[0].toStdString();
    port = parts[1].toULongLong();
    return true;
}
