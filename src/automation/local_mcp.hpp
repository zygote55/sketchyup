#pragma once
#include <QString>
namespace sketchy {
// Linux filesystem sockets only; the parent must be private to the current user.
QString checkedMcpSocketPath(const QString &path, bool existing);
void checkMcpPeer(int descriptor);
int bridgeMcpStdio(const QString &path);
} // namespace sketchy
