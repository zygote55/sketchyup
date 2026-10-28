#pragma once
// The stand-in Blender of the job tests (R109 S5, ADR 0183). The job starts its executable under
// the Blender sandbox, which refuses /bin/sh, so a stand-in cannot be a shell script. A stand-in is
// a hard link to one copy of the project's blender_job_tests binary, in the same directory, named
// sketchyup-fake-<mode>-<n>. blender_job_tests reads the mode from that name when it starts (its
// main). The job canonicalizes its executable when it starts, so a path that is a symbolic link to
// a stand-in runs that stand-in, and a test repairs a stand-in by pointing the link at a new one.
// The modes are those of blender_job_tests --fake, plus:
//   counted-<hang>: the launch numbered <hang> hangs and the others succeed. The launches are
//   counted in the
//                   file named by <stand-in>.counter, which the job's policy lets the stand-in
//                   write only when the test gave the directory (Options::testWritableDirectories);
//   slow:           every launch waits a few seconds, then succeeds;
//   hold-handoff:   a handoff hangs, and any other operation succeeds.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include <stdexcept>
#include <unistd.h>

namespace blender_stand_in {

// The directory's copy of the test binary: made on first use, and linked from for every stand-in
// there.
inline QString copyIn(const QString &directory) {
    const QString copy = QDir(directory).filePath("blender-job-tests-stand-in");
    if (!QFile::exists(copy) &&
        !QFile::copy(QCoreApplication::applicationDirPath() + "/blender_job_tests", copy))
        throw std::runtime_error("Copy the stand-in Blender");
    return copy;
}

// A new stand-in in directory, running mode: a hard link to the copy, named for the mode. The names
// of one process never repeat.
inline QString make(const QString &directory, const QString &mode) {
    static int sequence = 0;
    const QString path =
        QDir(directory).filePath("sketchyup-fake-" + mode + "-" + QString::number(++sequence));
    const QString copy = copyIn(directory);
    if (::link(QFile::encodeName(copy).constData(), QFile::encodeName(path).constData()) != 0)
        throw std::runtime_error("Link the stand-in Blender");
    if (!QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
        throw std::runtime_error("Stand-in Blender is executable");
    return path;
}

// The path a test gives the job: a symbolic link to a new stand-in in the path's directory. Placing
// it again repoints the link, so a retry that starts after a repair runs the repaired stand-in.
inline QString place(const QString &path, const QString &mode) {
    const QString target = make(QFileInfo(path).absolutePath(), mode);
    if (QFileInfo(path).isSymLink() || QFile::exists(path))
        if (!QFile::remove(path))
            throw std::runtime_error("Replace the stand-in Blender");
    if (!QFile::link(target, path))
        throw std::runtime_error("Link the stand-in Blender at its path");
    return path;
}

// A stand-in at path whose launch numbered hang hangs, counting its launches in counter (a file the
// test reads and the stand-in writes). A PATH name such as bin/blender is a path of its own.
inline QString countedAt(const QString &path, int hang, const QString &counter) {
    const QString linked = place(path, "counted-" + QString::number(hang));
    const QString target = QFileInfo(linked).symLinkTarget();
    QFile file(target + ".counter");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        file.write(QFile::encodeName(counter)) < 0)
        throw std::runtime_error("Write the stand-in's counter path");
    return linked;
}

} // namespace blender_stand_in
