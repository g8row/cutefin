#pragma once

#include <QImage>
#include <QString>

// Self-contained BlurHash decoder (base83 -> DCT synthesis). No dependencies
// beyond Qt's QImage/QString. See https://blurha.sh for the format spec.
namespace BlurHash {

// Decodes `hash` into a QImage of the given size. Returns a null QImage if
// the hash is malformed (wrong length for its encoded component count, or
// contains characters outside the base83 alphabet).
QImage decode(const QString &hash, int width, int height);

} // namespace BlurHash
