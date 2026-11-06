#include "app/game_icons.hpp"

#include "app/imgui_guard.hpp"
#include "common/archive/inflate.hpp"
#include "game/skill_names.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

namespace aion2::app {

namespace {

struct SkillIconRow {
    uint32_t code;
    int16_t slot;
};

#include "game_icon_sheet.inc"

constexpr size_t kSlotBytes = static_cast<size_t>(kIconPx) * kIconPx * 4;
static_assert(kIconPixelsSize == kSlotBytes * kIconSlots, "the sheet's size is its slots'");

/// The sheet, inflated on first use. Empty when the stream does not decode to
/// exactly the size the index gives: then no icon draws, and nothing else changes.
const std::vector<uint8_t>& sheet() {
    static const std::vector<uint8_t> pixels = [] {
        archive::InflateResult r = archive::inflate(kIconPixelsDeflated, kIconPixelsSize);
        if (!r.ok() || r.data.size() != kIconPixelsSize) return std::vector<uint8_t>{};
        return std::move(r.data);
    }();
    return pixels;
}

} // namespace

int class_icon(game::AionClass cls) noexcept {
    const auto i = static_cast<size_t>(cls);
    return i < std::size(kClassSlots) ? kClassSlots[i] : kNoIcon;
}

int skill_icon(uint32_t code) noexcept {
    const uint32_t key = game::skill_key(code);
    const auto* it = std::lower_bound(std::begin(kSkillIcons), std::end(kSkillIcons), key,
                                      [](const SkillIconRow& row, uint32_t c) { return row.code < c; });
    return it != std::end(kSkillIcons) && it->code == key ? it->slot : kNoIcon;
}

int effect_icon(uint32_t buff_id, uint32_t source_skill) noexcept {
    const uint32_t skill = game::effect_skill(buff_id, source_skill);
    return skill != 0 ? skill_icon(skill) : kNoIcon;
}

int icon_slot_count() noexcept { return kIconSlots; }

int icon_sheet_px() noexcept { return kIconPx; }

std::span<const uint8_t> icon_pixels(int slot) {
    const std::vector<uint8_t>& pixels = sheet();
    if (slot < 0 || slot >= kIconSlots || pixels.empty()) return {};
    return std::span<const uint8_t>(pixels).subspan(static_cast<size_t>(slot) * kSlotBytes, kSlotBytes);
}

void resample_icon(std::span<const uint8_t> src, int src_px, uint8_t* dst, int dst_px, size_t dst_stride) {
    if (src_px <= 0 || dst_px <= 0 || dst == nullptr) return;
    if (src.size() < static_cast<size_t>(src_px) * static_cast<size_t>(src_px) * 4) return;
    const double step = static_cast<double>(src_px) / dst_px;
    for (int dy = 0; dy < dst_px; ++dy) {
        const double y0 = dy * step;
        const double y1 = y0 + step;
        for (int dx = 0; dx < dst_px; ++dx) {
            const double x0 = dx * step;
            const double x1 = x0 + step;
            // Each source pixel the destination pixel covers, weighted by the part of
            // it covered, and its colour also by its alpha.
            double r = 0, g = 0, b = 0, a = 0, area = 0;
            for (int sy = static_cast<int>(y0); sy < src_px && sy < y1; ++sy) {
                const double wy = std::min<double>(sy + 1, y1) - std::max<double>(sy, y0);
                for (int sx = static_cast<int>(x0); sx < src_px && sx < x1; ++sx) {
                    const double w = wy * (std::min<double>(sx + 1, x1) - std::max<double>(sx, x0));
                    if (w <= 0) continue;
                    const uint8_t* p = &src[(static_cast<size_t>(sy) * src_px + sx) * 4];
                    const double pa = p[3] / 255.0;
                    r += p[0] * pa * w;
                    g += p[1] * pa * w;
                    b += p[2] * pa * w;
                    a += pa * w;
                    area += w;
                }
            }
            uint8_t* out = dst + static_cast<size_t>(dy) * dst_stride + static_cast<size_t>(dx) * 4;
            const auto channel = [](double v) {
                return static_cast<uint8_t>(std::clamp(std::lround(v), 0L, 255L));
            };
            out[0] = a > 0 ? channel(r / a) : 0;
            out[1] = a > 0 ? channel(g / a) : 0;
            out[2] = a > 0 ? channel(b / a) : 0;
            out[3] = area > 0 ? channel(a / area * 255.0) : 0;
        }
    }
}

#ifdef HAVE_IMGUI

namespace {

/// Where an atlas keeps the icons: its first custom rect and their size. Each
/// window has its own context and atlas, all drawn from the UI thread.
struct AtlasIcons {
    const ImFontAtlas* atlas;
    int first_rect;
    int px;
};
std::vector<AtlasIcons> g_atlases;

const AtlasIcons* icons_of(const ImFontAtlas* atlas) {
    for (const AtlasIcons& a : g_atlases) {
        if (a.atlas == atlas) return &a;
    }
    return nullptr;
}

} // namespace

void build_atlas_with_icons(ImFontAtlas& atlas, float icon_px) {
    std::erase_if(g_atlases, [&](const AtlasIcons& a) { return a.atlas == &atlas; });
    const int px = std::clamp(static_cast<int>(std::lround(icon_px)), 8, kIconPx);
    const bool have = !sheet().empty();
    const int first = atlas.CustomRects.Size;
    if (have) {
        for (int i = 0; i < kIconSlots; ++i) atlas.AddCustomRectRegular(px, px);
        // Wide enough that the icons do not stretch the atlas into a tall strip.
        const int pad = px + atlas.TexGlyphPadding;
        if (static_cast<double>(pad) * pad * kIconSlots > 512.0 * 512.0 * 0.5) {
            atlas.TexDesiredWidth = std::max(atlas.TexDesiredWidth, 1024);
        }
    }
    atlas.Build();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    atlas.GetTexDataAsRGBA32(&pixels, &width, &height);
    if (!have || pixels == nullptr) return;
    const size_t stride = static_cast<size_t>(width) * 4;
    for (int i = 0; i < kIconSlots; ++i) {
        const ImFontAtlasCustomRect* rect = atlas.GetCustomRectByIndex(first + i);
        if (!rect->IsPacked()) return;
        resample_icon(icon_pixels(i), kIconPx, pixels + rect->Y * stride + static_cast<size_t>(rect->X) * 4, px,
                      stride);
    }
    g_atlases.push_back({&atlas, first, px});
}

void draw_icon(ImDrawList* dl, int slot, float x, float y, float size, float alpha) {
    if (dl == nullptr || slot < 0 || slot >= kIconSlots) return;
    const ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    const AtlasIcons* icons = icons_of(atlas);
    if (icons == nullptr || icons->first_rect + slot >= atlas->CustomRects.Size) return;
    const ImFontAtlasCustomRect* rect = &atlas->CustomRects[icons->first_rect + slot];
    ImVec2 uv0;
    ImVec2 uv1;
    atlas->CalcCustomRectUV(rect, &uv0, &uv1);
    // Whole pixels, and the atlas's own size when it is close: one texel to a pixel
    // keeps the icon sharp, where the renderer's linear filter would blur it.
    const float side = std::fabs(size - static_cast<float>(icons->px)) < 1.5f ? static_cast<float>(icons->px) : size;
    const ImVec2 p0(std::round(x + (size - side) * 0.5f), std::round(y + (size - side) * 0.5f));
    dl->AddImage(atlas->TexID, p0, ImVec2(p0.x + side, p0.y + side), uv0, uv1,
                 ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)));
}

#else // !HAVE_IMGUI

void build_atlas_with_icons(ImFontAtlas&, float) {}

void draw_icon(ImDrawList*, int, float, float, float, float) {}

#endif

} // namespace aion2::app
