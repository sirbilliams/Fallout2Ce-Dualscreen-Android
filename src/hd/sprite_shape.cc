#include "hd/sprite_shape.h"

#include <math.h>

#include <algorithm>
#include <vector>

#include "obj_types.h"

namespace fallout::hd {

namespace {

    // The pipeline tilts pillow normals by 0.6 per Sobel unit on 4x masks;
    // the same silhouette at 1x has slopes 4x steeper per pixel.
    constexpr float kPillowTilt = 0.15f;
    // Iso walls run at +-0.5 px per px.
    constexpr float kIsoSlope = 0.5f;

    // The three ways a wall can run on screen: the map's two axes and flat.
    // Falling to the right (3 down per 4 across) faces south-west, rising (1
    // up per 4) faces south-east, flat faces the camera. The facings are what
    // the old slope blend gave at these slopes, so walls keep their brightness.
    constexpr int kWallRunKinds = 3;
    constexpr float kWallRunSlope[kWallRunKinds] = { 0.75f, 0.0f, -0.25f };
    constexpr float kWallRunFacing[kWallRunKinds] = { -0.70710678f, 0.0f, 0.38268343f };
    // Slope is measured across +-4 columns: the stair-steps of a rising wall repeat every 4.
    constexpr int kWallRunWindow = 4;
    // What a change of facing must save in summed slope error, so that only a
    // real corner makes one.
    constexpr float kWallRunSwitchCost = 3.0f;

    // Scenery without a body of its own stays flat, facing the camera. The
    // pillow only bends the outermost pixels of a large sprite, which makes
    // a bright or dark band along it (cliffs are built from rectangles), and
    // a thin one is all edge.
    constexpr float kPillowMaxRadius = 16.0f;
    constexpr float kPillowMinRadius = 2.5f;
    constexpr float kPillowMaxFill = 0.95f;

    // Round scenery (the author, 2026-11-09: trees, cacti, bushes, rocks and
    // barrels should look more 3D): one rounded body over the whole object
    // instead of a pillow along its edge. The silhouette with its gaps closed
    // (between leaves and branches), blurred into a soft mound as tall as the
    // body is thick; the mound's slope tilts the normals. Scale-free: at
    // strength 1 the rim of a wide body tilts about 34 degrees, the middle
    // faces the camera.
    constexpr float kRoundClose = 0.08f; // gap closing radius, share of the smaller side (1..6 px)
    constexpr float kRoundSigma = 0.6f; // blur, share of the body's radius

    // Gaussian blur of a 1D signal, border replicated.
    void blur1d(std::vector<float>& values, float sigma)
    {
        int n = static_cast<int>(values.size());
        int radius = static_cast<int>(ceilf(sigma * 4.0f));
        if (n < 2 || radius < 1) {
            return;
        }
        std::vector<float> kernel(radius * 2 + 1);
        float sum = 0.0f;
        for (int i = -radius; i <= radius; i++) {
            kernel[i + radius] = expf(-0.5f * (i * i) / (sigma * sigma));
            sum += kernel[i + radius];
        }
        std::vector<float> out(n);
        for (int x = 0; x < n; x++) {
            float acc = 0.0f;
            for (int i = -radius; i <= radius; i++) {
                acc += kernel[i + radius] * values[std::min(std::max(x + i, 0), n - 1)];
            }
            out[x] = acc / sum;
        }
        values.swap(out);
    }

    // Lowest opaque row per column, -1 where the column is empty.
    std::vector<float> columnBottoms(const unsigned char* pixels, int width, int height)
    {
        std::vector<float> bottoms(width, -1.0f);
        for (int x = 0; x < width; x++) {
            for (int y = height - 1; y >= 0; y--) {
                if (pixels[y * width + x] != 0) {
                    bottoms[x] = static_cast<float>(y);
                    break;
                }
            }
        }
        return bottoms;
    }

    // Screen row of the ground under each column (fo2hd.hdassets.ground_line):
    // the lower envelope of the column bottoms under the constraint that the
    // ground can't rise faster than the iso slope, slightly smoothed.
    std::vector<float> groundLine(const std::vector<float>& bottoms, int height)
    {
        int width = static_cast<int>(bottoms.size());
        std::vector<float> line(width, -1e9f);
        bool any = false;
        for (int vx = 0; vx < width; vx++) {
            if (bottoms[vx] < 0.0f) {
                continue;
            }
            any = true;
            for (int x = 0; x < width; x++) {
                line[x] = std::max(line[x], bottoms[vx] - kIsoSlope * abs(x - vx));
            }
        }
        if (!any) {
            return std::vector<float>(width, static_cast<float>(height - 1));
        }
        if (width > 2) {
            blur1d(line, 1.5f);
        }
        return line;
    }

    // Wall facing per column, constant along each run of the bottom edge, so
    // pieces of one straight wall share one normal and stair-steps leave no
    // stripes: each column's slope is matched to the nearest kind of run, and
    // a shortest-path search over the columns keeps changes to real corners.
    // Returns u per column.
    std::vector<float> wallFacing(const std::vector<float>& bottoms)
    {
        int width = static_cast<int>(bottoms.size());
        std::vector<float> bottom(bottoms);
        std::vector<int> valid;
        for (int x = 0; x < width; x++) {
            if (bottom[x] >= 0.0f) {
                valid.push_back(x);
            }
        }
        if (valid.size() < 2 || width < 3) {
            return std::vector<float>(width, 0.0f);
        }
        // Linear interpolation over empty columns, ends held.
        size_t k = 0;
        for (int x = 0; x < width; x++) {
            while (k + 1 < valid.size() && valid[k + 1] <= x) {
                k++;
            }
            if (x <= valid.front()) {
                bottom[x] = bottoms[valid.front()];
            } else if (x >= valid.back()) {
                bottom[x] = bottoms[valid.back()];
            } else if (bottoms[x] < 0.0f) {
                int a = valid[k];
                int b = valid[k + 1];
                float t = static_cast<float>(x - a) / (b - a);
                bottom[x] = bottoms[a] + (bottoms[b] - bottoms[a]) * t;
            }
        }

        std::vector<int> from(static_cast<size_t>(width) * kWallRunKinds, 0);
        float best[kWallRunKinds] = { 0.0f, 0.0f, 0.0f };
        for (int x = 0; x < width; x++) {
            int lo = std::max(x - kWallRunWindow, 0);
            int hi = std::min(x + kWallRunWindow, width - 1);
            float slope = (bottom[hi] - bottom[lo]) / static_cast<float>(hi - lo);
            int cheapest = 0;
            for (int i = 1; i < kWallRunKinds; i++) {
                if (best[i] < best[cheapest]) {
                    cheapest = i;
                }
            }
            float next[kWallRunKinds];
            for (int i = 0; i < kWallRunKinds; i++) {
                bool change = x > 0 && best[cheapest] + kWallRunSwitchCost < best[i];
                from[static_cast<size_t>(x) * kWallRunKinds + i] = change ? cheapest : i;
                next[i] = (change ? best[cheapest] + kWallRunSwitchCost : best[i]) + fabsf(slope - kWallRunSlope[i]);
            }
            std::copy(next, next + kWallRunKinds, best);
        }
        int kind = 0;
        for (int i = 1; i < kWallRunKinds; i++) {
            if (best[i] < best[kind]) {
                kind = i;
            }
        }
        std::vector<float> u(width);
        for (int x = width - 1; x >= 0; x--) {
            u[x] = kWallRunFacing[kind];
            kind = from[static_cast<size_t>(x) * kWallRunKinds + kind];
        }
        return u;
    }

    // Distance from each texel to the nearest transparent one (outside the
    // frame counts as transparent), two-pass chamfer.
    std::vector<float> insideDistance(const unsigned char* pixels, int width, int height)
    {
        const float kInf = 1e9f;
        const float kDiagonal = 1.41421356f;
        int w = width + 2;
        int h = height + 2;
        std::vector<float> d(static_cast<size_t>(w) * h, 0.0f);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                d[(y + 1) * w + x + 1] = pixels[y * width + x] != 0 ? kInf : 0.0f;
            }
        }
        for (int y = 1; y < h - 1; y++) {
            for (int x = 1; x < w - 1; x++) {
                float& c = d[y * w + x];
                c = std::min({ c, d[y * w + x - 1] + 1.0f, d[(y - 1) * w + x] + 1.0f, d[(y - 1) * w + x - 1] + kDiagonal, d[(y - 1) * w + x + 1] + kDiagonal });
            }
        }
        for (int y = h - 2; y >= 1; y--) {
            for (int x = w - 2; x >= 1; x--) {
                float& c = d[y * w + x];
                c = std::min({ c, d[y * w + x + 1] + 1.0f, d[(y + 1) * w + x] + 1.0f, d[(y + 1) * w + x + 1] + kDiagonal, d[(y + 1) * w + x - 1] + kDiagonal });
            }
        }
        std::vector<float> out(static_cast<size_t>(width) * height);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                out[y * width + x] = d[(y + 1) * w + x + 1];
            }
        }
        return out;
    }

    // Separable gaussian blur of a w x h image, border replicated.
    void blur2d(std::vector<float>& image, int w, int h, float sigma)
    {
        std::vector<float> line;
        for (int y = 0; y < h; y++) {
            line.assign(image.begin() + static_cast<size_t>(y) * w, image.begin() + static_cast<size_t>(y + 1) * w);
            blur1d(line, sigma);
            std::copy(line.begin(), line.end(), image.begin() + static_cast<size_t>(y) * w);
        }
        line.resize(h);
        for (int x = 0; x < w; x++) {
            for (int y = 0; y < h; y++) {
                line[y] = image[static_cast<size_t>(y) * w + x];
            }
            blur1d(line, sigma);
            for (int y = 0; y < h; y++) {
                image[static_cast<size_t>(y) * w + x] = line[y];
            }
        }
    }

    // Base normals (u, z) of round scenery: see kRoundClose.
    void roundBody(const unsigned char* pixels, int width, int height, float strength, std::vector<float>& nu, std::vector<float>& nz)
    {
        int c = std::min(std::max(static_cast<int>(lroundf(kRoundClose * std::min(width, height))), 1), 6);
        // Room around the frame, so its border (which insideDistance counts
        // as empty) doesn't grow or eat the closed shape.
        int pad = 2 * c + 3;
        int w = width + 2 * pad;
        int h = height + 2 * pad;
        size_t n = static_cast<size_t>(w) * h;
        std::vector<unsigned char> object(n, 0);
        std::vector<unsigned char> empty(n, 1);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                bool opaque = pixels[y * width + x] != 0;
                object[(y + pad) * w + x + pad] = opaque ? 1 : 0;
                empty[(y + pad) * w + x + pad] = opaque ? 0 : 1;
            }
        }
        // Closing: grow by c, then shrink by c.
        std::vector<float> away = insideDistance(empty.data(), w, h);
        std::vector<unsigned char> grown(n);
        for (size_t i = 0; i < n; i++) {
            grown[i] = object[i] != 0 || away[i] <= static_cast<float>(c) ? 1 : 0;
        }
        std::vector<float> in = insideDistance(grown.data(), w, h);
        std::vector<unsigned char> closed(n);
        std::vector<float> thickness;
        for (size_t i = 0; i < n; i++) {
            closed[i] = object[i] != 0 || in[i] > static_cast<float>(c) ? 1 : 0;
        }
        std::vector<float> depth = insideDistance(closed.data(), w, h);
        for (size_t i = 0; i < n; i++) {
            if (closed[i] != 0) {
                thickness.push_back(depth[i]);
            }
        }
        float radius = 1.5f;
        if (!thickness.empty()) {
            size_t k = std::min(thickness.size() - 1, static_cast<size_t>(thickness.size() * 0.9));
            std::nth_element(thickness.begin(), thickness.begin() + k, thickness.end());
            radius = std::max(radius, thickness[k]);
        }
        std::vector<float> mound(n);
        for (size_t i = 0; i < n; i++) {
            mound[i] = closed[i] != 0 ? radius : 0.0f;
        }
        blur2d(mound, w, h, std::max(1.0f, kRoundSigma * radius));
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                size_t i = static_cast<size_t>(y + pad) * w + x + pad;
                float gx = (mound[i + 1] - mound[i - 1]) * 0.5f;
                float gy = (mound[i + w] - mound[i - w]) * 0.5f;
                // Facing the camera (v); screen x tilts toward u, screen up toward z.
                float u = -gx * strength;
                float z = gy * strength;
                float length = sqrtf(u * u + 1.0f + z * z);
                nu[static_cast<size_t>(y) * width + x] = u / length;
                nz[static_cast<size_t>(y) * width + x] = z / length;
            }
        }
    }

    void put(unsigned char* rgba, float u, float z, float heightPx)
    {
        rgba[0] = static_cast<unsigned char>(lroundf(std::min(std::max(u * 0.5f + 0.5f, 0.0f), 1.0f) * 255.0f));
        rgba[1] = static_cast<unsigned char>(lroundf(std::min(std::max(z * 0.5f + 0.5f, 0.0f), 1.0f) * 255.0f));
        long h = std::min(std::max(lroundf(heightPx * 16.0f), 0L), 65535L);
        rgba[2] = static_cast<unsigned char>(h & 0xFF);
        rgba[3] = static_cast<unsigned char>(h >> 8);
    }

} // namespace

void spriteShape(const unsigned char* pixels, int width, int height, int objectType, unsigned char* rgba, float roundStrength)
{
    size_t count = static_cast<size_t>(width) * height;
    bool shaped = objectType == OBJ_TYPE_ITEM || objectType == OBJ_TYPE_CRITTER || objectType == OBJ_TYPE_SCENERY || objectType == OBJ_TYPE_WALL || objectType == OBJ_TYPE_MISC;
    if (!shaped) {
        for (size_t i = 0; i < count; i++) {
            put(rgba + i * 4, 0.0f, 1.0f, 0.0f);
        }
        return;
    }

    std::vector<float> bottoms = columnBottoms(pixels, width, height);
    bool grounded = objectType == OBJ_TYPE_WALL || objectType == OBJ_TYPE_SCENERY;
    std::vector<float> ground = grounded ? groundLine(bottoms, height) : std::vector<float>();

    std::vector<float> nu(count, 0.0f);
    std::vector<float> nz(count, 0.0f);
    if (objectType == OBJ_TYPE_WALL) {
        std::vector<float> u = wallFacing(bottoms);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                nu[y * width + x] = u[x];
            }
        }
    } else if (roundStrength > 0.0f && objectType == OBJ_TYPE_SCENERY) {
        roundBody(pixels, width, height, roundStrength, nu, nz);
    } else {
        // Pillow billboard: the distance inside the silhouette as a bulge;
        // its gradient points outward.
        std::vector<float> dist = insideDistance(pixels, width, height);
        std::vector<float> inside;
        for (size_t i = 0; i < count; i++) {
            if (pixels[i] != 0) {
                inside.push_back(dist[i]);
            }
        }
        float radius = 2.0f;
        if (!inside.empty()) {
            size_t k = std::min(inside.size() - 1, static_cast<size_t>(inside.size() * 0.9));
            std::nth_element(inside.begin(), inside.begin() + k, inside.end());
            radius = std::max(2.0f, inside[k]);
        }
        bool bodyless = false;
        if (objectType == OBJ_TYPE_SCENERY) {
            float fill = static_cast<float>(inside.size()) / static_cast<float>(std::max(count, static_cast<size_t>(1)));
            bodyless = radius >= kPillowMaxRadius || radius < kPillowMinRadius || fill >= kPillowMaxFill;
        }
        std::vector<float> bulge(count, 0.0f);
        for (size_t i = 0; i < count; i++) {
            if (pixels[i] != 0) {
                float t = 1.0f - std::min(std::max(dist[i] / radius, 0.0f), 1.0f);
                bulge[i] = sqrtf(std::min(std::max(1.0f - t * t, 0.0f), 1.0f));
            }
        }
        auto at = [&](int x, int y) {
            x = std::min(std::max(x, 0), width - 1);
            y = std::min(std::max(y, 0), height - 1);
            return bulge[y * width + x];
        };
        for (int y = 0; y < height && !bodyless; y++) {
            for (int x = 0; x < width; x++) {
                float gx = (at(x + 1, y - 1) + 2.0f * at(x + 1, y) + at(x + 1, y + 1)) - (at(x - 1, y - 1) + 2.0f * at(x - 1, y) + at(x - 1, y + 1));
                float gy = (at(x - 1, y + 1) + 2.0f * at(x, y + 1) + at(x + 1, y + 1)) - (at(x - 1, y - 1) + 2.0f * at(x, y - 1) + at(x + 1, y - 1));
                // Facing the camera (v); screen x tilts toward u, screen up toward z.
                float u = -gx * kPillowTilt;
                float z = gy * kPillowTilt;
                float length = sqrtf(u * u + 1.0f + z * z);
                nu[y * width + x] = u / length;
                nz[y * width + x] = z / length;
            }
        }
    }

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            size_t i = static_cast<size_t>(y) * width + x;
            float h = 0.0f;
            if (pixels[i] != 0) {
                h = grounded ? ground[x] - y : static_cast<float>(height - 1 - y);
            }
            put(rgba + i * 4, nu[i], nz[i], std::max(h, 0.0f));
        }
    }
}

} // namespace fallout::hd
