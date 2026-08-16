#include "BlurHash.h"

#include <array>
#include <cmath>
#include <vector>

namespace BlurHash {
namespace {

constexpr const char *kBase83Chars =
    "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    "#$%*+,-.:;=?@[]^_{|}~";

struct LinearColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

// Returns -1 if `ch` is not part of the base83 alphabet.
int base83CharValue(QChar ch)
{
    const char c = ch.toLatin1();
    for (int i = 0; kBase83Chars[i] != '\0'; ++i) {
        if (kBase83Chars[i] == c)
            return i;
    }
    return -1;
}

bool decode83(const QString &str, int *outValue)
{
    int value = 0;
    for (const QChar &ch : str) {
        const int digit = base83CharValue(ch);
        if (digit < 0)
            return false;
        value = value * 83 + digit;
    }
    *outValue = value;
    return true;
}

float srgbToLinear(int value)
{
    const float v = static_cast<float>(value) / 255.0f;
    if (v <= 0.04045f)
        return v / 12.92f;
    return std::pow((v + 0.055f) / 1.055f, 2.4f);
}

int linearToSrgb(float value)
{
    const float v = std::clamp(value, 0.0f, 1.0f);
    if (v <= 0.0031308f)
        return static_cast<int>(v * 12.92f * 255.0f + 0.5f);
    return static_cast<int>((1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f) * 255.0f + 0.5f);
}

LinearColor decodeDC(int value)
{
    LinearColor color;
    color.r = srgbToLinear((value >> 16) & 0xff);
    color.g = srgbToLinear((value >> 8) & 0xff);
    color.b = srgbToLinear(value & 0xff);
    return color;
}

float signedPow(float value, float exponent)
{
    const float sign = value < 0.0f ? -1.0f : 1.0f;
    return sign * std::pow(std::fabs(value), exponent);
}

LinearColor decodeAC(int value, float maximumValue)
{
    const int quantR = value / (19 * 19);
    const int quantG = (value / 19) % 19;
    const int quantB = value % 19;

    LinearColor color;
    color.r = signedPow((static_cast<float>(quantR) - 9.0f) / 9.0f, 2.0f) * maximumValue;
    color.g = signedPow((static_cast<float>(quantG) - 9.0f) / 9.0f, 2.0f) * maximumValue;
    color.b = signedPow((static_cast<float>(quantB) - 9.0f) / 9.0f, 2.0f) * maximumValue;
    return color;
}

} // namespace

QImage decode(const QString &hash, int width, int height)
{
    if (hash.length() < 6 || width <= 0 || height <= 0)
        return QImage();

    int sizeFlag = 0;
    if (!decode83(hash.mid(0, 1), &sizeFlag))
        return QImage();
    const int numY = sizeFlag / 9 + 1;
    const int numX = sizeFlag % 9 + 1;

    const int expectedLength = 4 + 2 * numX * numY;
    if (hash.length() != expectedLength)
        return QImage();

    int quantizedMaximumValue = 0;
    if (!decode83(hash.mid(1, 1), &quantizedMaximumValue))
        return QImage();
    const float maximumValue = static_cast<float>(quantizedMaximumValue + 1) / 166.0f;

    std::vector<LinearColor> colors(static_cast<size_t>(numX) * numY);

    int dcValue = 0;
    if (!decode83(hash.mid(2, 4), &dcValue))
        return QImage();
    colors[0] = decodeDC(dcValue);

    for (int i = 1; i < numX * numY; ++i) {
        int acValue = 0;
        if (!decode83(hash.mid(4 + i * 2, 2), &acValue))
            return QImage();
        colors[static_cast<size_t>(i)] = decodeAC(acValue, maximumValue);
    }

    QImage image(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            for (int j = 0; j < numY; ++j) {
                for (int i = 0; i < numX; ++i) {
                    const float basis = std::cos(M_PI * x * i / width) * std::cos(M_PI * y * j / height);
                    const LinearColor &c = colors[static_cast<size_t>(i + j * numX)];
                    r += c.r * basis;
                    g += c.g * basis;
                    b += c.b * basis;
                }
            }
            image.setPixel(x, y, qRgb(linearToSrgb(r), linearToSrgb(g), linearToSrgb(b)));
        }
    }
    return image;
}

} // namespace BlurHash
