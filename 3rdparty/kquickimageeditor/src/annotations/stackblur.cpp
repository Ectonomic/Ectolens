// SPDX-FileCopyrightText: 2010 Mario Klingemann <mario@quasimondo.com>
// SPDX-FileCopyrightText: 2026 Noah Davis <noahadvs@gmail.com>
// SPDX-FileCopyrightText: 2026 Ectolens contributors
//
// SPDX-License-Identifier: BSD-2-Clause

// Ectolens: the stack blur in plain C++, in place of KQuickImageEditor 0.7.0's Highway (SIMD)
// version, so Ectolens's own copy of the library builds without Highway's headers. Same
// interface (stackblur.h, unchanged), same formats, same kernel: each output pixel is the
// average of its neighbors within the radius, weighted (radius + 1 - distance), with the
// image's edge pixels repeated past the edge. Rounded to the nearest value. The running sums
// make it the same cost at any radius. autotests/stackblurtest.cpp holds it to a direct,
// pixel-by-pixel version of that average.

#include "stackblur.h"

#include <QImage>
#include <QtConcurrent/QtConcurrentMap>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace StackBlur
{
namespace
{
using Px = int;

// Rows (or columns) are blurred in parallel, a band at a time.
template<typename Function>
void forBands(Px count, Px bandSize, Function function)
{
    std::vector<std::pair<Px, Px>> bands;
    for (Px start = 0; start < count; start += bandSize) {
        bands.emplace_back(start, std::min(count, start + bandSize));
    }
    if (bands.size() == 1) {
        function(bands.front());
        return;
    }
    QtConcurrent::blockingMap(bands, function);
}

// One line of values (a row, or a column read with a step), blurred into `out`.
// `in(i)` gives the line's value at i, clamped to the line by the caller.
template<typename Sum, typename Get, typename Put>
inline void blurLine(Px length, Px radius, Get in, Put out)
{
    const Sum divisor = Sum(radius + 1) * Sum(radius + 1);
    const Sum half = divisor / 2;
    const auto at = [&](Px i) {
        return Sum(in(std::clamp(i, 0, length - 1)));
    };
    // S: the weighted sum around x; A: the values that come nearer as x moves on (x+1 … x+r+1);
    // B: the values that move away (x-r … x).
    Sum S = 0, A = 0, B = 0;
    for (Px i = -radius; i <= radius; ++i) {
        S += Sum(radius + 1 - std::abs(i)) * at(i);
    }
    for (Px j = 1; j <= radius + 1; ++j) {
        A += at(j);
    }
    for (Px j = -radius; j <= 0; ++j) {
        B += at(j);
    }
    for (Px x = 0; x < length; ++x) {
        out(x, (S + half) / divisor);
        S += A - B;
        A += at(x + radius + 2) - at(x + 1);
        B += at(x + 1) - at(x - radius);
    }
}

template<typename C_t, int Channels>
void blurImage(QImage &image, Px radiusX, Px radiusY)
{
    using Sum = std::conditional_t<sizeof(C_t) == 1, int32_t, int64_t>;
    const Px w = image.width();
    const Px h = image.height();
    const qsizetype stride = image.bytesPerLine();
    uchar *bits = image.bits();

    if (radiusX >= minRadius()) {
        forBands(h, 16, [&](const std::pair<Px, Px> &band) {
            std::vector<C_t> line(size_t(w) * Channels);
            for (Px y = band.first; y < band.second; ++y) {
                auto row = reinterpret_cast<C_t *>(bits + y * stride);
                std::copy(row, row + size_t(w) * Channels, line.begin());
                for (int c = 0; c < Channels; ++c) {
                    blurLine<Sum>(
                        w,
                        radiusX,
                        [&](Px i) {
                            return line[size_t(i) * Channels + c];
                        },
                        [&](Px x, Sum v) {
                            row[size_t(x) * Channels + c] = C_t(v);
                        });
                }
            }
        });
    }
    if (radiusY >= minRadius()) {
        forBands(w, 64, [&](const std::pair<Px, Px> &band) {
            std::vector<C_t> line(static_cast<size_t>(h));
            for (Px x = band.first; x < band.second; ++x) {
                for (int c = 0; c < Channels; ++c) {
                    for (Px y = 0; y < h; ++y) {
                        line[y] = reinterpret_cast<const C_t *>(bits + y * stride)[size_t(x) * Channels + c];
                    }
                    blurLine<Sum>(
                        h,
                        radiusY,
                        [&](Px i) {
                            return line[i];
                        },
                        [&](Px y, Sum v) {
                            reinterpret_cast<C_t *>(bits + y * stride)[size_t(x) * Channels + c] = C_t(v);
                        });
                }
            }
        });
    }
}
} // namespace

void blur(QImage &image, Px radiusX, Px radiusY)
{
    static constexpr auto supportedFormats = supportedImageFormats();
    // Null images will have Format_Invalid. Images with (width < 1 || height < 1)
    // will be null, so this also checks if the size is valid.
    if (std::find(supportedFormats.begin(), supportedFormats.end(), image.format()) == supportedFormats.end()) {
        return;
    }
    // Radius is never more than (size-1)/2 because the kernel should always
    // have an odd size and should never be bigger than the image.
    radiusX = std::max(0, std::min({radiusX, (image.width() - 1) / 2, maxRadius()}));
    radiusY = std::max(0, std::min({radiusY, (image.height() - 1) / 2, maxRadius()}));
    if (radiusX < minRadius() && radiusY < minRadius()) {
        return;
    }
    const auto pixelFormat = image.pixelFormat();
    const auto channelCount = pixelFormat.channelCount();
    const auto bytesPerChannel = (pixelFormat.bitsPerPixel() / channelCount) >> 3;
    // Only 1 or 4 channels, as upstream: Alpha8 and the common RGB formats.
    if (bytesPerChannel == sizeof(uint8_t)) {
        if (channelCount == 1) {
            blurImage<uint8_t, 1>(image, radiusX, radiusY);
        } else if (channelCount == 4) {
            blurImage<uint8_t, 4>(image, radiusX, radiusY);
        }
    } else if (bytesPerChannel == sizeof(uint16_t)) {
        if (channelCount == 1) {
            blurImage<uint16_t, 1>(image, radiusX, radiusY);
        } else if (channelCount == 4) {
            blurImage<uint16_t, 4>(image, radiusX, radiusY);
        }
    }
}

} // namespace StackBlur
