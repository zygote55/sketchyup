#include "automation/measurements.hpp"
#include "core/components.hpp"
#include "presentation/unit_display.hpp"
#include <QCoreApplication>
#include <iostream>
using namespace sketchy;
void check(bool value, const std::string &message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F operation) {
    bool rejected = false;
    try {
        operation();
    } catch (const std::exception &) {
        rejected = true;
    }
    check(rejected, "Expected display precision rejection");
}
void same(const QString &actual, const QString &expected) {
    check(actual == expected, "Formatted \"" + actual.toStdString() + "\", expected \"" +
                                  expected.toStdString() + "\"");
}
void meters(const QString &text, const QString &unit, double expected, const QLocale &locale) {
    const auto value = parseLength(text, unit, locale);
    check(std::abs(value - expected) <= 1e-12,
          "Entry " + text.toStdString() + " parsed as " + std::to_string(value));
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QLocale::setDefault(QLocale::c());
    try {
        // Core edit semantics.
        Document doc(DisplayUnit::Millimeters);
        check(doc.displayPrecision() == fullDisplayPrecision && !doc.dirty(),
              "New documents default to Full precision");
        rejects([] { Document invalid(DisplayUnit::Millimeters, 4); });
        rejects([] { Document invalid(DisplayUnit::Meters, -2); });
        check(Document(DisplayUnit::Meters, 6).displayPrecision() == 6 &&
                  !Document(DisplayUnit::FeetInches, 3).canUndo(),
              "Initial precision is a clean baseline");
        doc.addFace({{{0, 0, 0}, {2, 0, 0}, {2, 3, 0}, {0, 3, 0}}});
        doc.markSaved();
        const auto model = doc.bodies();
        const auto saved = doc.saveStamp();
        const auto revision = doc.revision();
        doc.setDisplayPrecision(2);
        check(doc.displayPrecision() == 2 && doc.displayUnits() == DisplayUnit::Millimeters &&
                  doc.revision() == revision + 1 && doc.dirty() && doc.bodies() == model &&
                  doc.history().entries.back().label == "Change display precision",
              "Precision change is one labeled edit without touching geometry");
        doc.undo();
        check(doc.displayPrecision() == fullDisplayPrecision && doc.isCurrentSnapshot(saved),
              "Undo restores Full precision and saved state");
        doc.redo();
        check(doc.displayPrecision() == 2 && doc.dirty(), "Redo restores precision");
        const auto unchanged = doc.revision();
        doc.setDisplayPrecision(2);
        check(doc.revision() == unchanged, "Unchanged precision is a no-op");
        rejects([&] { doc.setDisplayPrecision(4); });
        rejects([&] { doc.setDisplayPrecision(-2); });
        rejects([&] { doc.setDisplayUnits(DisplayUnit::Meters, 7); });
        check(doc.revision() == unchanged && doc.displayPrecision() == 2,
              "Out-of-range precision rejects atomically");
        Edit stale{"Stale precision", {}};
        stale.displayPrecision = std::pair{3, 1};
        rejects([&] { doc.apply(stale, doc.revision()); });
        stale.displayPrecision = std::pair{2, 2};
        rejects([&] { doc.apply(stale, doc.revision()); });
        stale.displayPrecision = std::pair{2, 1};
        rejects([&] { doc.apply(stale, doc.revision() - 1); });
        Edit invalidUnits{"Unit without valid precision", {}};
        invalidUnits.displayUnits = std::pair{DisplayUnit::Millimeters, DisplayUnit::Meters};
        invalidUnits.displayPrecision = std::pair{2, 5};
        doc.apply(invalidUnits, doc.revision());
        check(doc.displayUnits() == DisplayUnit::Meters && doc.displayPrecision() == 5,
              "Combined unit and precision edit validates against the new unit");
        doc.undo();
        invalidUnits.displayUnits = std::pair{DisplayUnit::Millimeters, DisplayUnit::FeetInches};
        rejects([&] { doc.apply(invalidUnits, doc.revision()); });
        doc.setDisplayUnits(DisplayUnit::Meters);
        check(doc.displayUnits() == DisplayUnit::Meters &&
                  doc.displayPrecision() == fullDisplayPrecision &&
                  doc.history().entries.back().label == "Change document units",
              "A unit change resets precision to Full");
        doc.undo();
        check(doc.displayUnits() == DisplayUnit::Millimeters && doc.displayPrecision() == 2,
              "Undoing a unit change restores the previous precision");
        doc.setDisplayUnits(DisplayUnit::FeetInches, 3);
        check(doc.displayUnits() == DisplayUnit::FeetInches && doc.displayPrecision() == 3 &&
                  doc.history().entries.back().label == "Change document units",
              "A unit change keeps an explicit precision");
        const auto stamp = doc.amendmentStamp();
        doc.amendLast(stamp, [](Document &candidate) {
            candidate.setDisplayUnits(DisplayUnit::Meters, 4);
        });
        check(doc.displayUnits() == DisplayUnit::Meters && doc.displayPrecision() == 4,
              "Unit/precision amendment stays one edit");
        doc.move(1, {1, 0, 0});
        const auto move = doc.amendmentStamp();
        rejects([&] {
            doc.amendLast(move, [](Document &candidate) { candidate.setDisplayPrecision(1); });
        });
        check(doc.canAmend(move) && doc.displayPrecision() == 4,
              "Geometry amendment cannot change precision");
        auto component = createComponent(doc, 1, "Panel");
        editComponentDefinition(doc, component.definition, [](Document &draft) {
            check(draft.displayPrecision() == 4, "Shared edit inherits precision");
            for (const auto &[id, body] : draft.bodies())
                if (!body->surface.faces.empty()) {
                    draft.paint(id, {.1f, .2f, .3f});
                    break;
                }
            return ChangeReport{};
        });
        const auto beforeRejected = doc.saveStamp();
        rejects([&] {
            editComponentDefinition(doc, component.definition, [](Document &draft) {
                draft.setDisplayPrecision(1);
                return ChangeReport{};
            });
        });
        check(doc.isCurrentSnapshot(beforeRejected), "Shared geometry scope cannot change precision");
        check(doc.readSnapshot().displayPrecision() == 4, "Read snapshots carry precision");

        // Formatting table: Full is the historical trimmed form; explicit precision
        // keeps trailing zeros and carries rounding.
        const auto F = fullDisplayPrecision;
        const auto m = DisplayUnit::Meters, mm = DisplayUnit::Millimeters,
                   ft = DisplayUnit::FeetInches;
        same(displayLength(1.2345678, m, F), "1.2345678 m");
        same(displayLength(1, m, F), "1 m");
        same(displayLength(1.2345678, m, 0), "1 m");
        same(displayLength(1.2345678, m, 2), "1.23 m");
        same(displayLength(1.2345678, m, 6), "1.234568 m");
        same(displayLength(.9999, m, 2), "1.00 m");
        same(displayLength(.9999, m, 0), "1 m");
        same(displayLength(-.0001, m, 2), "0.00 m");
        same(displayLength(-1.5, m, 1), "-1.5 m");
        same(displayLength(1.2345678, mm, F), "1234.5678 mm");
        same(displayLength(1.2345678, mm, 0), "1235 mm");
        same(displayLength(1.2345678, mm, 3), "1234.568 mm");
        same(displayLength(.0099996, mm, 2), "10.00 mm");
        same(displayLength(.3048, ft, F), "1' 0\"");
        same(displayLength(1.2345678, ft, F), "4' 0.605031\"");
        same(displayLength(1.2345678, ft, 0), "4' 1\"");
        same(displayLength(1.2345678, ft, 3), "4' 0.605\"");
        same(displayLength(.3047, ft, 0), "1' 0\"");
        same(displayLength(.3047, ft, 2), "1' 0.00\"");
        same(displayLength(.3047, ft, 3), "0' 11.996\"");
        same(displayLength(.30479, ft, 3), "1' 0.000\"");
        same(displayLength(-.3048, ft, 1), "-1' 0.0\"");
        same(displayLength(-.0001, ft, 1), "0' 0.0\"");
        same(displayMeasure(6, 2, m, F), "6 m²");
        same(displayMeasure(6, 2, m, 2), "6.00 m²");
        same(displayMeasure(1.23456789, 2, m, 6), "1.234568 m²");
        same(displayMeasure(1, 2, mm, 0), "1000000 mm²");
        same(displayMeasure(1, 2, ft, F), "10.76391 ft²");
        same(displayMeasure(1, 2, ft, 3), "10.764 ft²");
        same(displayMeasure(1, 3, ft, 1), "35.3 ft³");
        same(displayMeasure(1, 3, ft, 0), "35 ft³");
        same(precisionSample(m, F), "Full (1.2345678 m)");
        same(precisionSample(mm, 1), "1234.6 mm");
        rejects([] { displayLength(1, m, 7); });
        rejects([] { displayLength(1, mm, 4); });
        rejects([] { displayMeasure(1, 2, ft, 4); });
        QLocale::setDefault(QLocale(QLocale::German, QLocale::Germany));
        same(displayLength(1234.5, m, 2), "1234,50 m");
        same(displayLength(.9999, ft, 1), "3' 3,4\"");
        QLocale::setDefault(QLocale::c());

        // Decimal imperial entry converts to exact metres, including the locale
        // decimal separator.
        const QLocale c = QLocale::c(), german(QLocale::German, QLocale::Germany);
        const auto feet = QString::fromLatin1(defaultLengthUnit(ft).data());
        meters("2.5in", "m", 2.5 * .0254, c);
        meters("2.5 in", "m", .0635, c);
        meters("1.25ft", "m", 1.25 * .3048, c);
        meters("1.25ft", "m", .381, c);
        meters("1.5'", "m", .4572, c);
        meters("3' 4.5\"", "m", 40.5 * .0254, c);
        meters("3' 4.5\"", "mm", 1.0287, c);
        meters("2.5", feet, .762, c);
        meters("-1.25ft", feet, -.381, c);
        meters("2,5in", "m", .0635, german);
        meters("1,25ft", "m", .381, german);
        meters("3' 4,5\"", "m", 1.0287, german);
        meters("2,5", feet, .762, german);
        rejects([&] { parseLength("3' 12.5\"", feet, c); });
        // Fixed precision never feeds back into input: Full text reparses exactly.
        for (auto unit : {m, mm, ft})
            for (double length : {.0635, .381, .4572, 1.0287, .762})
                meters(displayLength(length, unit, F), QString::fromLatin1(defaultLengthUnit(unit).data()),
                       length, c);
        std::cout << "Display precision edits, formatting table and decimal imperial entry passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
