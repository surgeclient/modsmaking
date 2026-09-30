// HUD, menus, crafting / taming / brewing panels. Text uses a built-in 5x7 pixel font
// drawn as instanced quads, so there are no font files or textures.
#include "game.h"
#include <GLFW/glfw3.h>
#include <cstdio>

namespace {

// Classic 5x7 font, ASCII 32..95, column-major, bit 0 = top row.
const uint8_t FONT[64][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x5F, 0x00, 0x00}, {0x00, 0x07, 0x00, 0x07, 0x00}, {0x14, 0x7F, 0x14, 0x7F, 0x14},
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, {0x23, 0x13, 0x08, 0x64, 0x62}, {0x36, 0x49, 0x55, 0x22, 0x50}, {0x00, 0x05, 0x03, 0x00, 0x00},
    {0x00, 0x1C, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x1C, 0x00}, {0x08, 0x2A, 0x1C, 0x2A, 0x08}, {0x08, 0x08, 0x3E, 0x08, 0x08},
    {0x00, 0x50, 0x30, 0x00, 0x00}, {0x08, 0x08, 0x08, 0x08, 0x08}, {0x00, 0x60, 0x60, 0x00, 0x00}, {0x20, 0x10, 0x08, 0x04, 0x02},
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4B, 0x31},
    {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39}, {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x00, 0x36, 0x36, 0x00, 0x00}, {0x00, 0x56, 0x36, 0x00, 0x00},
    {0x00, 0x08, 0x14, 0x22, 0x41}, {0x14, 0x14, 0x14, 0x14, 0x14}, {0x41, 0x22, 0x14, 0x08, 0x00}, {0x02, 0x01, 0x51, 0x09, 0x06},
    {0x32, 0x49, 0x79, 0x41, 0x3E}, {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36}, {0x3E, 0x41, 0x41, 0x41, 0x22},
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, {0x7F, 0x49, 0x49, 0x49, 0x41}, {0x7F, 0x09, 0x09, 0x01, 0x01}, {0x3E, 0x41, 0x41, 0x51, 0x32},
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x08, 0x14, 0x22, 0x41},
    {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x04, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F}, {0x3E, 0x41, 0x41, 0x41, 0x3E},
    {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x3E, 0x41, 0x51, 0x21, 0x5E}, {0x7F, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31},
    {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x1F, 0x20, 0x40, 0x20, 0x1F}, {0x7F, 0x20, 0x18, 0x20, 0x7F},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x03, 0x04, 0x78, 0x04, 0x03}, {0x61, 0x51, 0x49, 0x45, 0x43}, {0x00, 0x00, 0x7F, 0x41, 0x41},
    {0x02, 0x04, 0x08, 0x10, 0x20}, {0x41, 0x41, 0x7F, 0x00, 0x00}, {0x04, 0x02, 0x01, 0x02, 0x04}, {0x40, 0x40, 0x40, 0x40, 0x40},
};

inline void rect(FrameScene& s, float x, float y, float w, float h, vec4 c) { s.ui.push_back({vec4(x, y, w, h), c}); }

inline float textW(const std::string& t, float px) { return t.size() * 6 * px - px; }

void textRaw(FrameScene& s, float x, float y, const std::string& t, float px, vec4 col) {
    float cx = x;
    for (char ch : t) {
        int c = (unsigned char)ch;
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c < 32 || c > 95) c = '?';
        const uint8_t* g = FONT[c - 32];
        for (int colI = 0; colI < 5; colI++) {
            uint8_t bits = g[colI];
            int r = 0;
            while (r < 7) {
                if (bits & (1 << r)) {
                    int start = r;
                    while (r < 7 && (bits & (1 << r))) r++;
                    rect(s, cx + colI * px, y + start * px, px, (r - start) * px, col);
                } else {
                    r++;
                }
            }
        }
        cx += 6 * px;
    }
}

void text(FrameScene& s, float x, float y, const std::string& t, float px, vec4 col) {
    textRaw(s, x + px * 0.6f, y + px * 0.6f, t, px, vec4(0, 0, 0, col.w * 0.7f));
    textRaw(s, x, y, t, px, col);
}

void textC(FrameScene& s, float cx, float y, const std::string& t, float px, vec4 col) { text(s, cx - textW(t, px) * 0.5f, y, t, px, col); }

void bar(FrameScene& s, float x, float y, float w, float h, float frac, vec3 col, const std::string& label, float px) {
    rect(s, x - 2, y - 2, w + 4, h + 4, vec4(0, 0, 0, 0.55f));
    rect(s, x, y, w, h, vec4(col * 0.25f, 0.8f));
    rect(s, x, y, w * saturate(frac), h, vec4(col, 0.95f));
    if (!label.empty()) text(s, x + 5, y + (h - 7 * px) * 0.5f, label, px, vec4(1, 1, 1, 0.95f));
}

void panelBox(FrameScene& s, float x, float y, float w, float h) {
    vec4 edge(0.9f, 0.7f, 0.35f, 0.8f);
    rect(s, x, y, w, h, vec4(0.04f, 0.04f, 0.06f, 0.96f));
    rect(s, x - 3, y - 3, w + 6, 3, edge);
    rect(s, x - 3, y + h, w + 6, 3, edge);
    rect(s, x - 3, y, 3, h, edge);
    rect(s, x + w, y, 3, h, edge);
}

std::string fmtTime(float t) {
    int minutes = (int)(t * 24 * 60);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", (minutes / 60) % 24, minutes % 60);
    return buf;
}

}  // namespace

void uiTextCentered(FrameScene& s, float cx, float y, const std::string& t, float px, vec4 col) { textC(s, cx, y, t, px, col); }

// ---------------------------------------------------------------------------
void Game::updatePanels(float dt) {
    auto up = keyPressed(GLFW_KEY_UP) || keyPressed(GLFW_KEY_W);
    auto down = keyPressed(GLFW_KEY_DOWN) || keyPressed(GLFW_KEY_S);
    auto confirm = keyPressed(GLFW_KEY_ENTER) || keyPressed(GLFW_KEY_KP_ENTER) || keyPressed(GLFW_KEY_SPACE);

    switch (panel_) {
        case PANEL_HELP:
            if (keyPressed(GLFW_KEY_F1)) panel_ = PANEL_NONE;
            break;
        case PANEL_CRAFT: {
            if (keyPressed(GLFW_KEY_TAB)) {
                panel_ = PANEL_NONE;
                break;
            }
            std::vector<int> list;
            for (int i = 0; i < RECIPE_COUNT; i++)
                if (RECIPES[i].station == ST_HAND) list.push_back(i);
            if (up) craftSel_ = (craftSel_ + (int)list.size() - 1) % (int)list.size();
            if (down) craftSel_ = (craftSel_ + 1) % (int)list.size();
            craftSel_ = std::min(craftSel_, (int)list.size() - 1);
            if (confirm) {
                const Recipe& r = RECIPES[list[craftSel_]];
                if (canCraft(r)) {
                    craft(r, 1);
                    addMessage(std::string("Crafted ") + ITEMS[r.out].name, vec3(0.6f, 1.0f, 0.6f));
                } else {
                    addMessage("Missing ingredients.", vec3(1.0f, 0.5f, 0.4f));
                }
            }
            break;
        }
        case PANEL_TAME: {
            if (panelTarget_ < 0 || panelTarget_ >= (int)creatures_.size()) {
                panel_ = PANEL_NONE;
                break;
            }
            Creature& c = creatures_[panelTarget_];
            const Species& sp = SPECIES[c.species];
            if (!c.alive || c.state != CS_UNCONSCIOUS || c.tamed || distXZ(c.pos, player_.pos) > creatureRadius(c) + 7) {
                panel_ = PANEL_NONE;
                break;
            }
            auto give = [&](Item it, int n) {
                n = std::min(n, player_.inv[it]);
                if (n <= 0) {
                    addMessage(std::string("You have no ") + ITEMS[it].name + ".", vec3(1.0f, 0.6f, 0.4f));
                    return;
                }
                if (it != I_DREAMBERRY && foodValue(c.species, it) <= 0) {
                    addMessage(std::string("The ") + sp.name + " won't eat " + ITEMS[it].name +
                                   (sp.rarity != R_COMMON ? ". It only eats MYTHIC BAIT!" : "."),
                               vec3(1.0f, 0.6f, 0.4f));
                    return;
                }
                player_.inv[it] -= n;
                c.food[it] += n;
            };
            if (keyPressed(GLFW_KEY_1)) give(I_MYTHBAIT, 1);
            if (keyPressed(GLFW_KEY_2)) give(I_WILDBAIT, 1);
            if (keyPressed(GLFW_KEY_3)) {
                if (sp.diet == D_CARNIVORE) give(player_.inv[I_COOKEDMEAT] > 0 ? I_COOKEDMEAT : I_RAWMEAT, 5);
                else give(I_EMBERBERRY, 5);
            }
            if (keyPressed(GLFW_KEY_4)) give(I_DREAMBERRY, 5);
            break;
        }
        case PANEL_CAULDRON: {
            std::vector<int> list;
            for (int i = 0; i < RECIPE_COUNT; i++)
                if (RECIPES[i].station == ST_CAULDRON) list.push_back(i);
            if (up) craftSel_ = (craftSel_ + (int)list.size() - 1) % (int)list.size();
            if (down) craftSel_ = (craftSel_ + 1) % (int)list.size();
            craftSel_ = std::min(craftSel_, (int)list.size() - 1);
            if (keyPressed(GLFW_KEY_ENTER) || keyPressed(GLFW_KEY_KP_ENTER)) {
                const Recipe& r = RECIPES[list[craftSel_]];
                if (!canCraft(r)) {
                    addMessage("Missing ingredients for " + std::string(ITEMS[r.out].name) + ".", vec3(1.0f, 0.5f, 0.4f));
                } else {
                    for (auto& in : r.in)
                        if (in.item != I_NONE) player_.inv[in.item] -= in.count;
                    brew_ = Brew{};
                    brew_.recipe = list[craftSel_];
                    brew_.speed = 0.9f;
                    brew_.zoneWidth = 0.22f;
                    brew_.zoneStart = rng_.range(0.1f, 0.68f);
                    panel_ = PANEL_BREW;
                }
            }
            break;
        }
        case PANEL_BREW: {
            if (brew_.done) {
                brew_.resultTimer -= dt;
                if (brew_.resultTimer <= 0) panel_ = PANEL_CAULDRON;
                break;
            }
            brew_.needle += brew_.dir * brew_.speed * dt;
            if (brew_.needle > 1) {
                brew_.needle = 1;
                brew_.dir = -1;
            }
            if (brew_.needle < 0) {
                brew_.needle = 0;
                brew_.dir = 1;
            }
            if (keyPressed(GLFW_KEY_SPACE)) {
                bool hit = brew_.needle >= brew_.zoneStart && brew_.needle <= brew_.zoneStart + brew_.zoneWidth;
                if (hit) brew_.hits++;
                brew_.stage++;
                shake_ = hit ? 0.0f : 0.15f;
                if (brew_.stage >= 3) {
                    brew_.done = true;
                    brew_.resultTimer = 2.5f;
                    const Recipe& r = RECIPES[brew_.recipe];
                    if (brew_.hits == 0) {
                        addMessage("The brew curdles and burns... ruined!", vec3(1.0f, 0.4f, 0.3f));
                    } else {
                        int n = r.outCount * brew_.hits;
                        player_.inv[r.out] += n;
                        const char* q = brew_.hits == 3 ? "PERFECT" : (brew_.hits == 2 ? "Good" : "Weak");
                        addMessage(std::string(q) + " brew! +" + std::to_string(n) + " " + ITEMS[r.out].name, vec3(0.5f, 1.0f, 0.6f));
                    }
                } else {
                    brew_.speed *= 1.45f;
                    brew_.zoneWidth *= 0.72f;
                    brew_.zoneStart = rng_.range(0.05f, 0.95f - brew_.zoneWidth);
                }
            }
            break;
        }
        default: break;
    }
}

// ---------------------------------------------------------------------------
void Game::buildUI(FrameScene& s) {
    if (mode_ == GM_TRAILER) {
        drawTrailerUI(s);
        return;
    }
    if (mode_ == GM_TITLE) {
        drawTitle(s);
        return;
    }
    drawHUD(s);
    drawPanels(s);
}

void Game::drawTitle(FrameScene& s) {
    float W = (float)renderer_->width(), H = (float)renderer_->height();
    float ui = std::max(1.0f, H / 720.0f);
    float fadeIn = saturate(titleTime_ / 1.5f);
    rect(s, 0, 0, W, H, vec4(0, 0, 0, 0.25f + (1 - fadeIn) * 0.75f));
    float big = 12 * ui;
    textC(s, W * 0.5f + 4 * ui, H * 0.22f + 4 * ui, "MYTHBOUND", big, vec4(0.5f, 0.05f, 0.02f, fadeIn));
    textC(s, W * 0.5f, H * 0.22f, "MYTHBOUND", big, vec4(1.0f, 0.82f, 0.45f, fadeIn));
    textC(s, W * 0.5f, H * 0.22f + big * 9, "SURVIVE THE ISLAND.  TAME THE LEGENDS.  ENDURE THE NIGHT.", 2 * ui, vec4(0.9f, 0.9f, 0.95f, fadeIn));
    float pulse = 0.6f + 0.4f * std::sin(titleTime_ * 3);
    textC(s, W * 0.5f, H * 0.6f, "PRESS ENTER TO BEGIN", 3 * ui, vec4(1, 1, 1, pulse * fadeIn));
    textC(s, W * 0.5f, H * 0.6f + 40 * ui, "T - WATCH TRAILER      ESC - QUIT", 2 * ui, vec4(0.8f, 0.8f, 0.85f, fadeIn));
    textC(s, W * 0.5f, H - 40 * ui, "21 MYTHICAL CREATURES  -  THE HOLLOW TRIBE  -  VULKAN ENGINE", 1.5f * ui, vec4(0.7f, 0.7f, 0.75f, fadeIn));
}

void Game::drawHUD(FrameScene& s) {
    float W = (float)renderer_->width(), H = (float)renderer_->height();
    float ui = std::max(1.0f, H / 720.0f);
    float px = 2 * ui;
    const Player& p = player_;

    if (p.hurtFlash > 0) {
        float a = p.hurtFlash * 0.35f;
        rect(s, 0, 0, W, 40 * ui, vec4(0.7f, 0, 0, a));
        rect(s, 0, H - 40 * ui, W, 40 * ui, vec4(0.7f, 0, 0, a));
        rect(s, 0, 0, 40 * ui, H, vec4(0.7f, 0, 0, a));
        rect(s, W - 40 * ui, 0, 40 * ui, H, vec4(0.7f, 0, 0, a));
    }

    // Time of day.
    bool night = time_ >= 0.75f || time_ < 0.25f;
    std::string clock = "DAY " + std::to_string(day_) + "   " + fmtTime(time_);
    textC(s, W * 0.5f, 14 * ui, clock, px, vec4(1, 0.95f, 0.85f, 0.95f));
    int out = 0;
    for (const auto& e : infected_)
        if (e.alive && e.state != IS_DEAD && !e.trailer) out++;
    if (nightActive_ && night) {
        float pulse = 0.7f + 0.3f * std::sin(totalTime_ * 4);
        textC(s, W * 0.5f, 34 * ui, "NIGHT " + std::to_string(night_) + " - THE HOLLOW IS OPEN", px, vec4(1.0f, 0.25f, 0.2f, pulse));
        textC(s, W * 0.5f, 52 * ui, std::to_string(out) + " OF THE TRIBE ARE ROAMING", 1.5f * ui, vec4(1.0f, 0.55f, 0.5f, 0.9f));
    } else if (time_ > 0.68f && time_ < 0.77f) {
        textC(s, W * 0.5f, 34 * ui, "NIGHT IS COMING...", px, vec4(1.0f, 0.6f, 0.35f, 0.9f));
    } else if (out > 0) {
        textC(s, W * 0.5f, 34 * ui, std::to_string(out) + " STRAGGLERS BURNING IN THE SUN", 1.5f * ui, vec4(1.0f, 0.7f, 0.4f, 0.9f));
    }
    std::string biome = BIOME_NAMES[terrain_.biomeAt(p.pos.x, p.pos.z)];
    text(s, W - textW(biome, 1.5f * ui) - 16 * ui, 14 * ui, biome, 1.5f * ui, vec4(0.85f, 0.9f, 0.8f, 0.8f));
    int tamed = 0;
    for (const auto& c : creatures_)
        if (c.alive && c.tamed && c.state != CS_DEAD) tamed++;
    std::string stats = "TAMED " + std::to_string(tamed) + "   KILLS " + std::to_string(p.kills);
    text(s, W - textW(stats, 1.5f * ui) - 16 * ui, 30 * ui, stats, 1.5f * ui, vec4(0.85f, 0.9f, 0.8f, 0.8f));

    // Messages.
    float my = 14 * ui;
    for (const auto& m : messages_) {
        float a = saturate(m.time);
        text(s, 16 * ui, my, m.text, 1.5f * ui, vec4(m.color, a));
        my += 14 * ui;
    }

    if (p.dead) {
        rect(s, 0, 0, W, H, vec4(0.15f, 0, 0, 0.55f));
        textC(s, W * 0.5f, H * 0.4f, "YOU DIED", 8 * ui, vec4(1, 0.2f, 0.2f, 1));
        textC(s, W * 0.5f, H * 0.4f + 80 * ui, "Respawning in " + std::to_string((int)p.respawnTimer + 1) + "...", px, vec4(1, 1, 1, 0.9f));
        return;
    }

    // Survival bars.
    float bx = 18 * ui, bw = 220 * ui, bh = 16 * ui;
    float by = H - 30 * ui - 4 * (bh + 8 * ui);
    bar(s, bx, by, bw, bh, p.hp / p.maxHp, vec3(0.85f, 0.2f, 0.2f), "HEALTH " + std::to_string((int)p.hp), 1.5f * ui);
    bar(s, bx, by + (bh + 8 * ui), bw, bh, p.stamina / 100, vec3(0.9f, 0.8f, 0.2f), "STAMINA", 1.5f * ui);
    bar(s, bx, by + 2 * (bh + 8 * ui), bw, bh, p.food / 100, vec3(0.85f, 0.5f, 0.15f), "FOOD", 1.5f * ui);
    bar(s, bx, by + 3 * (bh + 8 * ui), bw, bh, p.water / 100, vec3(0.2f, 0.55f, 0.95f), "WATER", 1.5f * ui);

    // Mount.
    if (p.riding >= 0) {
        const Creature& c = creatures_[p.riding];
        float rx = W - bw - 18 * ui;
        text(s, rx, by - 18 * ui, c.name + "  LV " + std::to_string(c.level), 1.5f * ui, vec4(0.9f, 0.95f, 1.0f, 1));
        bar(s, rx, by, bw, bh, c.hp / c.maxHp, vec3(0.85f, 0.2f, 0.2f), "MOUNT HEALTH", 1.5f * ui);
        bar(s, rx, by + bh + 8 * ui, bw, bh, c.stamina / c.maxStamina, vec3(0.9f, 0.8f, 0.2f), "MOUNT STAMINA", 1.5f * ui);
    }

    // Crosshair.
    rect(s, W * 0.5f - 1 * ui, H * 0.5f - 7 * ui, 2 * ui, 14 * ui, vec4(1, 1, 1, 0.7f));
    rect(s, W * 0.5f - 7 * ui, H * 0.5f - 1 * ui, 14 * ui, 2 * ui, vec4(1, 1, 1, 0.7f));

    // Interaction prompt.
    if (!iaPrompt_.empty() && panel_ == PANEL_NONE) {
        float tw = textW(iaPrompt_, px);
        rect(s, W * 0.5f - tw * 0.5f - 10 * ui, H * 0.62f - 8 * ui, tw + 20 * ui, 7 * px + 16 * ui, vec4(0, 0, 0, 0.5f));
        textC(s, W * 0.5f, H * 0.62f, iaPrompt_, px, vec4(1, 0.95f, 0.7f, 1));
    }

    // Hotbar.
    auto items = hotbarItems();
    float slot = 62 * ui, gap = 6 * ui;
    float hx = W * 0.5f - (8 * slot + 7 * gap) * 0.5f, hy = H - slot - 16 * ui;
    for (int i = 0; i < 8; i++) {
        float x = hx + i * (slot + gap);
        bool sel = i == p.hotSel;
        rect(s, x, hy, slot, slot, sel ? vec4(0.95f, 0.75f, 0.35f, 0.85f) : vec4(0, 0, 0, 0.5f));
        rect(s, x + 3 * ui, hy + 3 * ui, slot - 6 * ui, slot - 6 * ui, vec4(0.08f, 0.08f, 0.1f, 0.85f));
        text(s, x + 5 * ui, hy + 5 * ui, std::to_string(i + 1), 1.2f * ui, vec4(0.7f, 0.7f, 0.7f, 0.8f));
        if (i < (int)items.size()) {
            Item it = items[i];
            rect(s, x + slot * 0.3f, hy + slot * 0.22f, slot * 0.4f, slot * 0.4f, vec4(ITEMS[it].color, 1));
            std::string name = ITEMS[it].name;
            size_t sp = name.find(' ');
            std::string shortName = sp != std::string::npos ? name.substr(sp + 1) : name;
            if (shortName.size() > 8) shortName = shortName.substr(0, 8);
            textC(s, x + slot * 0.5f, hy + slot - 12 * ui, shortName, 1.1f * ui, vec4(1, 1, 1, 0.9f));
            if (p.inv[it] > 1) text(s, x + slot - 16 * ui, hy + 5 * ui, std::to_string(p.inv[it]), 1.1f * ui, vec4(1, 1, 0.8f, 0.9f));
        } else if (i == 0 && items.empty()) {
            textC(s, x + slot * 0.5f, hy + slot * 0.4f, "FISTS", 1.1f * ui, vec4(1, 1, 1, 0.8f));
        }
    }
    Item sel = selectedItem();
    std::string selName = sel == I_NONE ? "Fists" : ITEMS[sel].name;
    if (sel == I_BOW) selName += p.ammoDarts ? "  [SLEEP DARTS " + std::to_string(p.inv[I_SLEEPDART]) + "]  R: switch"
                                             : "  [ARROWS " + std::to_string(p.inv[I_ARROW]) + "]  R: switch";
    if (sel != I_NONE && ITEMS[sel].kind == IK_PLACEABLE) selName += "   LMB place   R rotate";
    textC(s, W * 0.5f, hy - 16 * ui, selName, 1.5f * ui, vec4(1, 1, 1, 0.85f));
    text(s, W - 200 * ui, H - 20 * ui, "F1 HELP   TAB CRAFT", 1.2f * ui, vec4(0.8f, 0.8f, 0.8f, 0.6f));

    if (paused_) {
        rect(s, 0, 0, W, H, vec4(0, 0, 0, 0.6f));
        textC(s, W * 0.5f, H * 0.4f, "PAUSED", 6 * ui, vec4(1, 0.85f, 0.5f, 1));
        textC(s, W * 0.5f, H * 0.4f + 70 * ui, "ESC RESUME     T TRAILER     Q QUIT", px, vec4(1, 1, 1, 0.9f));
    }
}

void Game::drawPanels(FrameScene& s) {
    float W = (float)renderer_->width(), H = (float)renderer_->height();
    float ui = std::max(1.0f, H / 720.0f);
    float px = 2 * ui, sm = 1.5f * ui;
    const Player& p = player_;

    if (panel_ == PANEL_HELP) {
        float w = 640 * ui, h = 470 * ui, x = (W - w) * 0.5f, y = (H - h) * 0.5f;
        panelBox(s, x, y, w, h);
        textC(s, W * 0.5f, y + 16 * ui, "HOW TO SURVIVE", 3 * ui, vec4(1, 0.85f, 0.45f, 1));
        const char* lines[] = {
            "WASD move   SHIFT sprint   SPACE jump   CTRL crouch (harder to spot)",
            "MOUSE look   WHEEL zoom   1-8 hotbar   LMB attack / gather / place",
            "E interact   F ride / dismount   Q drop egg or baby   X eat   R ammo/rotate",
            "TAB crafting   F12 screenshot   ESC pause",
            "",
            "KNOCKOUT TAMING: hit creatures with a CLUB or SLEEP DARTS until they",
            "  drop. Press E and feed them. Rare ones ONLY eat MYTHIC BAIT.",
            "  Dreamberries keep them asleep. Hitting them lowers taming bonus.",
            "EGG TAMING: steal an egg from a dragon-kin nest and set it by a",
            "  CAMPFIRE (Q). Dragon eggs also need DRAGON BAIT to hatch.",
            "BABY TAMING: grab a baby Unicorn, Kitsune or Jackalope (E), outrun",
            "  its parents, drop it by your campfire and feed it.",
            "BAIT: build a CAULDRON and brew - hit SPACE in the green zone!",
            "",
            "THE TRIBE: every night war bands climb out of the Hollow in the",
            "  centre of the island. They don't know where you are - but they",
            "  kill anything they see. Hide, crouch, swim (they can't), or fight.",
            "  They grow stronger every night and burn at sunrise.",
        };
        float ly = y + 60 * ui;
        for (const char* l : lines) {
            text(s, x + 20 * ui, ly, l, sm, vec4(0.9f, 0.92f, 0.95f, 1));
            ly += 20 * ui;
        }
        textC(s, W * 0.5f, y + h - 24 * ui, "F1 / ESC CLOSE", sm, vec4(0.7f, 0.7f, 0.7f, 1));
    }

    if (panel_ == PANEL_CRAFT) {
        float w = 900 * ui, h = 540 * ui, x = (W - w) * 0.5f, y = (H - h) * 0.5f;
        panelBox(s, x, y, w, h);
        text(s, x + 20 * ui, y + 16 * ui, "INVENTORY", px, vec4(1, 0.85f, 0.45f, 1));
        float iy = y + 44 * ui;
        int col = 0;
        for (int it = 1; it < I_COUNT; it++) {
            if (p.inv[it] <= 0) continue;
            float ix = x + 20 * ui + col * 190 * ui;
            rect(s, ix, iy + 1 * ui, 10 * ui, 10 * ui, vec4(ITEMS[it].color, 1));
            text(s, ix + 16 * ui, iy, std::string(ITEMS[it].name) + " x" + std::to_string(p.inv[it]), sm, vec4(0.9f, 0.9f, 0.9f, 1));
            col++;
            if (col == 2) {
                col = 0;
                iy += 18 * ui;
            }
        }
        float rx = x + 410 * ui;
        text(s, rx, y + 16 * ui, "CRAFTING  (UP/DOWN + ENTER)", px, vec4(1, 0.85f, 0.45f, 1));
        std::vector<int> list;
        for (int i = 0; i < RECIPE_COUNT; i++)
            if (RECIPES[i].station == ST_HAND) list.push_back(i);
        float ry = y + 44 * ui;
        for (int k = 0; k < (int)list.size(); k++) {
            const Recipe& r = RECIPES[list[k]];
            bool sel = k == craftSel_;
            bool ok = canCraft(r);
            if (sel) rect(s, rx - 6 * ui, ry - 3 * ui, w - (rx - x) - 14 * ui, 16 * ui, vec4(0.9f, 0.7f, 0.35f, 0.25f));
            std::string name = std::string(ITEMS[r.out].name) + (r.outCount > 1 ? " x" + std::to_string(r.outCount) : "");
            text(s, rx, ry, name, sm, ok ? vec4(0.6f, 1.0f, 0.6f, 1) : vec4(0.65f, 0.65f, 0.65f, 1));
            ry += 20 * ui;
        }
        const Recipe& r = RECIPES[list[std::min(craftSel_, (int)list.size() - 1)]];
        float dy = y + h - 110 * ui;
        rect(s, x + 14 * ui, dy - 10 * ui, w - 28 * ui, 1 * ui, vec4(0.9f, 0.7f, 0.35f, 0.5f));
        text(s, x + 20 * ui, dy, ITEMS[r.out].name, px, vec4(1, 1, 1, 1));
        text(s, x + 20 * ui, dy + 22 * ui, r.hint, sm, vec4(0.8f, 0.85f, 0.9f, 1));
        float cx = x + 20 * ui;
        for (auto& in : r.in) {
            if (in.item == I_NONE) continue;
            bool have = p.inv[in.item] >= in.count;
            std::string t = std::string(ITEMS[in.item].name) + " " + std::to_string(p.inv[in.item]) + "/" + std::to_string(in.count);
            text(s, cx, dy + 46 * ui, t, sm, have ? vec4(0.6f, 1.0f, 0.6f, 1) : vec4(1.0f, 0.45f, 0.4f, 1));
            cx += textW(t, sm) + 24 * ui;
        }
        textC(s, W * 0.5f, y + h - 22 * ui, "TAB / ESC CLOSE      Baits are brewed at a CAULDRON", sm, vec4(0.7f, 0.7f, 0.7f, 1));
    }

    if (panel_ == PANEL_TAME && panelTarget_ >= 0) {
        const Creature& c = creatures_[panelTarget_];
        const Species& sp = SPECIES[c.species];
        float w = 560 * ui, h = 330 * ui, x = (W - w) * 0.5f, y = H * 0.18f;
        panelBox(s, x, y, w, h);
        text(s, x + 20 * ui, y + 16 * ui, std::string(sp.name) + "  -  LV " + std::to_string(c.level), 3 * ui, vec4(1, 0.85f, 0.45f, 1));
        text(s, x + 20 * ui, y + 48 * ui, sp.lore, 1.2f * ui, vec4(0.8f, 0.85f, 0.9f, 1));
        bar(s, x + 20 * ui, y + 72 * ui, w - 40 * ui, 18 * ui, c.tameProgress / std::max(1.0f, sp.tameDifficulty), vec3(0.3f, 0.85f, 0.4f),
            "TAMING " + std::to_string((int)(100 * c.tameProgress / std::max(1.0f, sp.tameDifficulty))) + "%", sm);
        bar(s, x + 20 * ui, y + 100 * ui, w - 40 * ui, 18 * ui, c.torpor / c.maxTorpor, vec3(0.55f, 0.35f, 0.9f), "TORPOR (keep it asleep!)", sm);
        text(s, x + 20 * ui, y + 128 * ui, "TAMING EFFECTIVENESS " + std::to_string((int)(c.tameEff * 100)) + "%", sm,
             vec4(0.9f, 0.9f, 0.6f, 1));
        std::string eats = sp.rarity != R_COMMON ? "EATS: MYTHIC BAIT ONLY"
                                                  : (sp.diet == D_CARNIVORE ? "EATS: Mythic/Wild Bait, cooked or raw meat" : "EATS: Mythic/Wild Bait, Emberberries");
        text(s, x + 20 * ui, y + 148 * ui, eats, sm, vec4(0.8f, 0.9f, 1.0f, 1));
        std::string inside = "IN ITS INVENTORY: ";
        bool any = false;
        for (Item it : {I_MYTHBAIT, I_WILDBAIT, I_COOKEDMEAT, I_RAWMEAT, I_EMBERBERRY, I_DREAMBERRY})
            if (c.food[it] > 0) {
                inside += std::string(ITEMS[it].name) + " x" + std::to_string(c.food[it]) + "  ";
                any = true;
            }
        if (!any) inside += "nothing - it's starving!";
        text(s, x + 20 * ui, y + 172 * ui, inside, sm, vec4(1, 1, 1, 1));
        std::string diet = sp.diet == D_CARNIVORE ? "Meat x5" : "Emberberries x5";
        const char* opts[] = {"1  Mythic Bait x1", "2  Wild Bait x1", nullptr, "4  Dreamberries x5 (sleep)"};
        std::string o3 = "3  " + diet;
        float oy = y + 204 * ui;
        for (int k = 0; k < 4; k++) {
            std::string t = k == 2 ? o3 : opts[k];
            Item it = k == 0 ? I_MYTHBAIT : k == 1 ? I_WILDBAIT : k == 3 ? I_DREAMBERRY
                    : (sp.diet == D_CARNIVORE ? (p.inv[I_COOKEDMEAT] > 0 ? I_COOKEDMEAT : I_RAWMEAT) : I_EMBERBERRY);
            t += "   (have " + std::to_string(p.inv[it]) + ")";
            text(s, x + 20 * ui + (k % 2) * 270 * ui, oy + (k / 2) * 22 * ui, t, sm, p.inv[it] > 0 ? vec4(0.9f, 1, 0.9f, 1) : vec4(0.6f, 0.6f, 0.6f, 1));
        }
        textC(s, W * 0.5f, y + h - 24 * ui, "ESC CLOSE  -  guard it while it eats: the tribe hunts at night", sm, vec4(0.7f, 0.7f, 0.7f, 1));
    }

    if (panel_ == PANEL_CAULDRON || panel_ == PANEL_BREW) {
        float w = 620 * ui, h = 330 * ui, x = (W - w) * 0.5f, y = H * 0.2f;
        panelBox(s, x, y, w, h);
        text(s, x + 20 * ui, y + 16 * ui, "ALCHEMY CAULDRON", 3 * ui, vec4(0.5f, 1.0f, 0.6f, 1));
        std::vector<int> list;
        for (int i = 0; i < RECIPE_COUNT; i++)
            if (RECIPES[i].station == ST_CAULDRON) list.push_back(i);
        if (panel_ == PANEL_CAULDRON) {
            float ry = y + 56 * ui;
            for (int k = 0; k < (int)list.size(); k++) {
                const Recipe& r = RECIPES[list[k]];
                bool sel = k == craftSel_;
                if (sel) rect(s, x + 14 * ui, ry - 4 * ui, w - 28 * ui, 50 * ui, vec4(0.4f, 0.9f, 0.5f, 0.15f));
                text(s, x + 20 * ui, ry, ITEMS[r.out].name, px, canCraft(r) ? vec4(0.6f, 1.0f, 0.6f, 1) : vec4(0.7f, 0.7f, 0.7f, 1));
                text(s, x + 220 * ui, ry + 2 * ui, r.hint, 1.2f * ui, vec4(0.8f, 0.85f, 0.9f, 1));
                float cx = x + 20 * ui;
                for (auto& in : r.in) {
                    if (in.item == I_NONE) continue;
                    bool have = p.inv[in.item] >= in.count;
                    std::string t = std::string(ITEMS[in.item].name) + " " + std::to_string(p.inv[in.item]) + "/" + std::to_string(in.count);
                    text(s, cx, ry + 22 * ui, t, 1.2f * ui, have ? vec4(0.6f, 1.0f, 0.6f, 1) : vec4(1.0f, 0.45f, 0.4f, 1));
                    cx += textW(t, 1.2f * ui) + 18 * ui;
                }
                ry += 64 * ui;
            }
            textC(s, W * 0.5f, y + h - 24 * ui, "UP/DOWN select   ENTER brew   ESC close", sm, vec4(0.7f, 0.7f, 0.7f, 1));
        } else {
            const Recipe& r = RECIPES[brew_.recipe];
            textC(s, W * 0.5f, y + 60 * ui, "BREWING " + std::string(ITEMS[r.out].name), px, vec4(1, 1, 1, 1));
            if (!brew_.done) {
                textC(s, W * 0.5f, y + 90 * ui, "STAGE " + std::to_string(brew_.stage + 1) + " OF 3  -  PRESS SPACE IN THE GREEN!", sm,
                      vec4(0.9f, 0.9f, 0.6f, 1));
                float bx = x + 40 * ui, bw = w - 80 * ui, by = y + 140 * ui, bh = 40 * ui;
                rect(s, bx - 3, by - 3, bw + 6, bh + 6, vec4(0, 0, 0, 0.8f));
                rect(s, bx, by, bw, bh, vec4(0.35f, 0.12f, 0.1f, 1));
                rect(s, bx + bw * brew_.zoneStart, by, bw * brew_.zoneWidth, bh, vec4(0.25f, 0.9f, 0.35f, 1));
                rect(s, bx + bw * brew_.needle - 3 * ui, by - 10 * ui, 6 * ui, bh + 20 * ui, vec4(1, 1, 1, 1));
            }
            std::string res;
            for (int k = 0; k < 3; k++) res += k < brew_.stage ? (k < brew_.hits ? "[*] " : "[ ] ") : "[.] ";
            textC(s, W * 0.5f, y + 210 * ui, res, px, vec4(0.9f, 0.9f, 0.9f, 1));
            if (brew_.done)
                textC(s, W * 0.5f, y + 250 * ui,
                      brew_.hits == 3 ? "PERFECT BREW - TRIPLE YIELD!" : (brew_.hits == 0 ? "RUINED..." : "BREW COMPLETE"), px,
                      brew_.hits == 0 ? vec4(1, 0.4f, 0.3f, 1) : vec4(0.5f, 1, 0.6f, 1));
        }
    }
}
