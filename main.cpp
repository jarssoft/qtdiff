#include <QGuiApplication>
#include <QImage>
#include <QDebug>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <utility>
#include <vector>

static int clamp255(int value)
{
    return std::min(255, std::max(0, value));
}

static QString offsetWord(int offset)
{
    static const QString words[8] = {
        "nolla", "yksi", "kaksi", "kolme",
        "neljä", "viisi", "kuusi", "seitsemän"
    };
    if (offset >= 0 && offset < 8)
        return words[offset];
    return QString::number(offset);
}

static QString directionLabel(int dx, int dy)
{
    if (dx == 0 && dy == 0)
        return "sama kohta";

    const int ay = std::abs(dy);
    const int ax = std::abs(dx);
    const QString vertical = (dy == 0) ? QString()
                                       : (ay == 1 ? QString(dy < 0 ? "ylä" : "ala")
                                                  : offsetWord(ay) + (dy < 0 ? " ylhäällä" : " alhaalla"));
    const QString horizontal = (dx == 0) ? QString()
                                         : (ax == 1 ? QString(dx < 0 ? "vasen" : "oikea")
                                                    : offsetWord(ax) + (dx < 0 ? " vasemmalla" : " oikealla"));

    if (vertical.isEmpty())
        return horizontal;
    if (horizontal.isEmpty())
        return vertical;
    if (ay <= 1 && ax <= 1)
        return vertical + horizontal;
    return vertical + " " + horizontal;
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    const QStringList args = app.arguments();
    int neighborhood = 3;

    if (args.size() == 5) {
        bool ok = false;
        neighborhood = args.at(4).toInt(&ok);
        if (!ok || neighborhood < 1 || neighborhood % 2 == 0) {
            qWarning() << "Virheellinen naapuruston koko:" << args.at(4)
                       << "(sallitut arvot: pariton luku, esim. 1, 3, 5, 7)";
            return EXIT_FAILURE;
        }
    } else if (args.size() != 4) {
        qWarning() << "Käyttö:" << args.first()
                   << "<kuva1.png> <kuva2.png> <erotus.png> [1|3|5]";
        return EXIT_FAILURE;
    }

    const int radius = neighborhood / 2;

    const QString firstPath = args.at(1);
    const QString secondPath = args.at(2);
    const QString outputPath = args.at(3);

    QImage firstImage(firstPath);
    QImage secondImage(secondPath);

    if (firstImage.isNull()) {
        qWarning() << "Kuvan lataus ei onnistunut:" << firstPath;
        return EXIT_FAILURE;
    }
    if (secondImage.isNull()) {
        qWarning() << "Kuvan lataus ei onnistunut:" << secondPath;
        return EXIT_FAILURE;
    }
    if (firstImage.size() != secondImage.size()) {
        qWarning() << "Kuvien koot eroavat:" << firstImage.size() << "vs" << secondImage.size();
        return EXIT_FAILURE;
    }

    QImage firstRgb = firstImage.convertToFormat(QImage::Format_RGB32);
    QImage secondRgb = secondImage.convertToFormat(QImage::Format_RGB32);

    QImage result(firstRgb.size(), QImage::Format_RGB32);

    const int side = 2 * radius + 1;
    std::vector<double> histogram(side * side, 0.0);
    std::vector<std::pair<int, int>> ties;
    ties.reserve(side * side);

    for (int y = 0; y < firstRgb.height(); ++y) {
        const QRgb *rowFirst = reinterpret_cast<const QRgb *>(firstRgb.constScanLine(y));
        QRgb *rowResult = reinterpret_cast<QRgb *>(result.scanLine(y));

        for (int x = 0; x < firstRgb.width(); ++x) {
            const double firstRed = qRed(rowFirst[x]);
            const double firstGreen = qGreen(rowFirst[x]);
            const double firstBlue = qBlue(rowFirst[x]);

            double bestCost = std::numeric_limits<double>::infinity();
            ties.clear();

            for (int dy = -radius; dy <= radius; ++dy) {
                const int ny = y + dy;
                if (ny < 0 || ny >= secondRgb.height())
                    continue;
                const QRgb *rowSecond = reinterpret_cast<const QRgb *>(secondRgb.constScanLine(ny));

                for (int dx = -radius; dx <= radius; ++dx) {
                    const int nx = x + dx;
                    if (nx < 0 || nx >= secondRgb.width())
                        continue;

                    const double dr = qRed(rowSecond[nx]) - firstRed;
                    const double dg = qGreen(rowSecond[nx]) - firstGreen;
                    const double db = qBlue(rowSecond[nx]) - firstBlue;
                    const double cost = dr * dr + dg * dg + db * db;

                    if (cost < bestCost) {
                        bestCost = cost;
                        ties.clear();
                        ties.emplace_back(dx, dy);
                    } else if (cost == bestCost) {
                        ties.emplace_back(dx, dy);
                    }
                }
            }

            const double share = 1.0 / double(ties.size());
            for (std::size_t i = 0; i < ties.size(); ++i)
                histogram[(ties[i].second + radius) * side + (ties[i].first + radius)] += share;

            const int brightness = clamp255(
                static_cast<int>(std::lround(std::sqrt(bestCost))));

            rowResult[x] = qRgb(brightness, brightness, brightness);
        }
    }

    const QImage gray = result.convertToFormat(QImage::Format_Grayscale8);

    if (!gray.save(outputPath, "PNG")) {
        qWarning() << "Erotuskuvan tallennus ei onnistunut:" << outputPath;
        return EXIT_FAILURE;
    }

    const double total = double(firstRgb.width()) * firstRgb.height();

    qDebug() << "Erotuskuva tallennettu:" << outputPath
             << "(" << gray.width() << "x" << gray.height() << ")";
    qDebug() << QString("Parallaksin suuntajakauma (%1x%2-alue, osuus kaikista pikseleistä):")
                    .arg(neighborhood).arg(neighborhood);

    for (int row = -radius; row <= radius; ++row) {
        QString line = QString("  dy = %1 :").arg(row);
        for (int col = -radius; col <= radius; ++col)
            line += QString("  %1%").arg(100.0 * histogram[(row + radius) * side + (col + radius)] / total, 7, 'f', 2);
        qDebug() << line;
    }

    double bestCount = -1.0;
    int bestRow = radius, bestCol = radius;
    for (int row = 0; row < side; ++row)
        for (int col = 0; col < side; ++col)
            if (histogram[row * side + col] > bestCount) {
                bestCount = histogram[row * side + col];
                bestRow = row;
                bestCol = col;
            }

    qDebug() << "Yleisin naapuri:" << directionLabel(bestCol - radius, bestRow - radius)
             << QString("(dx = %1, dy = %2),").arg(bestCol - radius).arg(bestRow - radius)
             << QString("%1% kaikista pikseleistä.").arg(100.0 * bestCount / total, 0, 'f', 2);

    return EXIT_SUCCESS;
}
