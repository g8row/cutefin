#include "core/BlurHash.h"

#include <QTest>

class tst_BlurHash : public QObject
{
    Q_OBJECT
private slots:
    void flatColorRoundTrips();
    void malformedHashReturnsNull();
    void realFixtureHashDecodesWithoutCrashing();
};

void tst_BlurHash::flatColorRoundTrips()
{
    // "00M{oP" is a single-component (numX=1, numY=1) hash hand-built to
    // encode the exact flat sRGB color (200, 80, 40) as its DC term. With no
    // AC components the cosine basis is 1 everywhere, so every decoded pixel
    // must reproduce that color (mod +/-1 rounding through the sRGB<->linear
    // conversion).
    const QImage image = BlurHash::decode(QStringLiteral("00M{oP"), 8, 8);
    QVERIFY(!image.isNull());
    QCOMPARE(image.width(), 8);
    QCOMPARE(image.height(), 8);

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            QVERIFY2(qAbs(c.red() - 200) <= 1, "red channel out of tolerance");
            QVERIFY2(qAbs(c.green() - 80) <= 1, "green channel out of tolerance");
            QVERIFY2(qAbs(c.blue() - 40) <= 1, "blue channel out of tolerance");
        }
    }
}

void tst_BlurHash::malformedHashReturnsNull()
{
    QVERIFY(BlurHash::decode(QString(), 8, 8).isNull());
    QVERIFY(BlurHash::decode(QStringLiteral("x"), 8, 8).isNull());
    // Valid alphabet but wrong length for its declared component count.
    QVERIFY(BlurHash::decode(QStringLiteral("00M{oPAB"), 8, 8).isNull());
    // Contains a character outside the base83 alphabet.
    QVERIFY(BlurHash::decode(QStringLiteral(" 0M{oP"), 8, 8).isNull());
}

void tst_BlurHash::realFixtureHashDecodesWithoutCrashing()
{
    // Taken from item_movie.json's ImageBlurHashes.Primary tag.
    const QImage image = BlurHash::decode(QStringLiteral("d69$h2%M019F@sMxx^?b?H$+9tInM{V[xtxtEKIo$*%L"), 32, 32);
    QVERIFY(!image.isNull());
    QCOMPARE(image.width(), 32);
    QCOMPARE(image.height(), 32);
}

QTEST_APPLESS_MAIN(tst_BlurHash)
#include "tst_blurhash.moc"
