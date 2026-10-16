#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "frame_stats.hpp"

int main(int argc, char* argv[]) {
    FrameStats::startProcessClock();

    QGuiApplication application(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("ShakePrototype"));
    QGuiApplication::setOrganizationName(QStringLiteral("Iain Chesworth Labs"));
    // One style on every machine, so a measurement is of the scene and not of
    // the platform's controls.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("The 3D board scene: shake, lid and drop, with timing."));
    parser.addHelpOption();
    const QCommandLineOption autoOption(
        QStringLiteral("auto"),
        QStringLiteral("Play the measurement runs, write the results and quit."));
    const QCommandLineOption outOption(QStringLiteral("out"),
                                       QStringLiteral("Directory for results and screenshots."),
                                       QStringLiteral("dir"), QStringLiteral("shake-results"));
    const QCommandLineOption probeOption(QStringLiteral("probe"),
                                         QStringLiteral("Light probe: none, procedural or baked."),
                                         QStringLiteral("mode"), QStringLiteral("baked"));
    const QCommandLineOption leanOption(
        QStringLiteral("lean"), QStringLiteral("Camera lean from straight down, in degrees."),
        QStringLiteral("degrees"), QStringLiteral("24"));
    const QCommandLineOption runsOption(QStringLiteral("runs"),
                                        QStringLiteral("Repetitions of each measured case."),
                                        QStringLiteral("count"), QStringLiteral("6"));
    const QCommandLineOption shotsOption(
        QStringLiteral("shots"),
        QStringLiteral("With --auto: save screenshots through one shake instead of timing."));
    const QCommandLineOption boardOption(QStringLiteral("board"),
                                         QStringLiteral("Board size: 3 or 4."),
                                         QStringLiteral("size"), QStringLiteral("4"));
    const QCommandLineOption digestOption(
        QStringLiteral("digest"),
        QStringLiteral("With --auto: print the replay checksums of a few boards and quit."));
    const QCommandLineOption warmOption(
        QStringLiteral("warm"),
        QStringLiteral("1: draw the lid, shadows and blended dice once while the first board is "
                       "shown, so the first shake does not pay for them; 0: do not."),
        QStringLiteral("0|1"), QStringLiteral("1"));
    const QCommandLineOption sizeOption(QStringLiteral("size"),
                                        QStringLiteral("Window size, WxH, in logical pixels."),
                                        QStringLiteral("WxH"), QStringLiteral("560x900"));
    parser.addOptions({autoOption, outOption, probeOption, leanOption, runsOption, shotsOption,
                       digestOption, boardOption, warmOption, sizeOption});
    parser.process(application);

    const QStringList size = parser.value(sizeOption).split(QLatin1Char('x'));
    QVariantMap initial;
    initial.insert(QStringLiteral("autoRun"), parser.isSet(autoOption));
    initial.insert(QStringLiteral("shotsOnly"), parser.isSet(shotsOption));
    initial.insert(QStringLiteral("digestOnly"), parser.isSet(digestOption));
    initial.insert(QStringLiteral("startBoard"), parser.value(boardOption).toInt() == 3 ? 3 : 4);
    initial.insert(QStringLiteral("outDir"), parser.value(outOption));
    initial.insert(QStringLiteral("probeMode"), parser.value(probeOption));
    initial.insert(QStringLiteral("leanDegrees"), parser.value(leanOption).toDouble());
    initial.insert(QStringLiteral("runCount"), parser.value(runsOption).toInt());
    initial.insert(QStringLiteral("warmUp"), parser.value(warmOption) != QStringLiteral("0"));
    initial.insert(QStringLiteral("startWidth"), size.value(0).toInt());
    initial.insert(QStringLiteral("startHeight"), size.value(1).toInt());

    QQmlApplicationEngine engine;
    engine.setInitialProperties(initial);
    engine.loadFromModule("ShakePrototype", "ShakeWindow");
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }
    return application.exec();
}
