#include "m6_fixture.hpp"
#include <QCoreApplication>
#include <iostream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        auto study = sketchy::m6::study();
        if (argc > 1)
            sketchy::m6::retain(study, QString::fromLocal8Bit(argv[1]));
        std::cout << "Integrated roof/stair/furniture, half-lap joinery, materials/provenance, "
                     "site placement, Undo and persistence passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
