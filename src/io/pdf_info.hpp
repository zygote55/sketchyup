#pragma once
#include <QByteArray>
#include <QDateTime>
namespace sketchy {
// D-036: repairs the document information dictionary that QPdfWriter writes on
// Qt 6.8.0 and 6.8.1. Those releases close the dictionary with "2" instead of ">>"
// (qtbase a30591603b58, fixed in 6.8.2 by 14bd53687faa), and Qt 6.8.0 writes the
// dates "D:00000000-1-1-1Z" when it cannot determine the system time zone. Both are
// patched in place with replacements of the same length, so every cross-reference
// offset stays valid. The "/Trapped /False" entry becomes "/Trapped/False", and an
// invalid date becomes `created` in UTC. Returns whether anything changed; any other
// PDF, including every one a fixed Qt writes, is left byte for byte unchanged.
bool repairQtPdfInfo(QByteArray &pdf, const QDateTime &created);
} // namespace sketchy
