#pragma once

#include "raylib.h"
#include "types.h"
#include "agent.h"
#include <cmath>
#include <optional>

// Draws the table with raylib and collects actions from the player whose turn
// it is. Everything is drawn from a PlayerView, so the UI only ever shows what
// that player is allowed to see. Use it as the Agent for human seats and as
// the game's observer.
class TableUI : public Agent, public GameObserver {
    public:

        TableUI() {
            SetConfigFlags(FLAG_MSAA_4X_HINT);
            InitWindow(WIDTH, HEIGHT, "Poker");
            SetTargetFPS(60);
            const char* path = "/System/Library/Fonts/Supplemental/Arial Bold.ttf";
            if (FileExists(path)) {
                font = LoadFontEx(path, 64, nullptr, 0);
                SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
                custom_font = true;
            } else {
                font = GetFontDefault();
            }
        }

        ~TableUI() {
            if (custom_font) UnloadFont(font);
            CloseWindow();
        }

        // Shows the table to the acting player until they pick an action.
        Action act(const PlayerView& v) override {
            raise_to = v.min_raise_to;
            dragging = false;
            while (true) {
                check_quit();
                BeginDrawing();
                draw_table(v, nullptr);
                optional<Action> a = action_panel(v);
                EndDrawing();
                if (a) return *a;
            }
        }

        // Shows the end of a hand (revealed cards, winnings) until "Next hand".
        void hand_over(const PlayerView& v, const HandResult& r) override {
            while (true) {
                check_quit();
                BeginDrawing();
                draw_table(v, &r);
                bool next = result_panel(v, r);
                EndDrawing();
                if (next) return;
            }
        }

        void game_over(const PlayerView& v, int winner) override {
            while (!WindowShouldClose()) {
                BeginDrawing();
                draw_table(v, nullptr);
                DrawRectangle(0, 0, WIDTH, HEIGHT, Fade(BLACK, 0.6f));
                Rectangle p = {WIDTH / 2.0f - 220, HEIGHT / 2.0f - 90, 440, 180};
                DrawRectangleRounded(p, 0.15f, 8, PANEL);
                text_centered("Player " + to_string(winner + 1) + " wins the game!",
                    p.x + p.width / 2, p.y + 55, 30, GOLD_TEXT);
                bool quit = button({p.x + 120, p.y + 105, 200, 48}, "Quit", RED_BUTTON);
                EndDrawing();
                if (quit) break;
            }
        }

    private:

        static constexpr int WIDTH = 1280;
        static constexpr int HEIGHT = 800;
        static constexpr Vector2 CENTER = {640, 300};
        static constexpr float TABLE_RX = 430;
        static constexpr float TABLE_RY = 195;

        static constexpr Color BACKGROUND = {24, 26, 32, 255};
        static constexpr Color FELT = {30, 110, 70, 255};
        static constexpr Color FELT_LINE = {60, 145, 100, 255};
        static constexpr Color RAIL = {92, 58, 34, 255};
        static constexpr Color PANEL = {38, 41, 50, 255};
        static constexpr Color PANEL_LINE = {70, 75, 90, 255};
        static constexpr Color TEXT = {235, 235, 240, 255};
        static constexpr Color MUTED = {150, 155, 170, 255};
        static constexpr Color GOLD_TEXT = {245, 200, 80, 255};
        static constexpr Color WIN_GREEN = {90, 210, 120, 255};
        static constexpr Color RED_BUTTON = {190, 60, 60, 255};
        static constexpr Color BLUE_BUTTON = {50, 110, 200, 255};
        static constexpr Color ORANGE_BUTTON = {215, 130, 40, 255};
        static constexpr Color GREY_BUTTON = {80, 85, 100, 255};
        static constexpr Color CARD_RED = {200, 30, 40, 255};
        static constexpr Color CARD_BLACK = {25, 25, 30, 255};

        Font font;
        bool custom_font = false;
        int raise_to = 0;
        bool dragging = false;

        void check_quit() {
            if (WindowShouldClose()) {
                CloseWindow();
                exit(0);
            }
        }

        // ---------- text and buttons ----------

        void text(const string& s, float x, float y, float size, Color c) {
            DrawTextEx(font, s.c_str(), {x, y}, size, 1, c);
        }

        void text_centered(const string& s, float cx, float cy, float size, Color c) {
            Vector2 m = MeasureTextEx(font, s.c_str(), size, 1);
            DrawTextEx(font, s.c_str(), {cx - m.x / 2, cy - m.y / 2}, size, 1, c);
        }

        bool button(Rectangle r, const string& label, Color base, bool enabled = true) {
            bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
            Color c = !enabled ? Fade(base, 0.3f) : hover ? ColorBrightness(base, 0.15f) : base;
            DrawRectangleRounded(r, 0.3f, 8, c);
            text_centered(label, r.x + r.width / 2, r.y + r.height / 2, r.height > 36 ? 20 : 15,
                enabled ? TEXT : MUTED);
            return hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
        }

        void badge(const string& s, float x, float y, Color bg, Color fg) {
            Vector2 m = MeasureTextEx(font, s.c_str(), 13, 1);
            DrawRectangleRounded({x, y, m.x + 10, 18}, 0.5f, 6, bg);
            text(s, x + 5, y + 2, 13, fg);
        }

        // ---------- cards ----------

        // raylib only fills triangles given in counter-clockwise order.
        static void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
            float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
            if (cross > 0) swap(b, c);
            DrawTriangle(a, b, c, col);
        }

        static void draw_suit(Suit s, float x, float y, float r, Color c) {
            switch (s) {
                case HEART:
                    DrawCircleV({x - 0.5f * r, y - 0.25f * r}, 0.55f * r, c);
                    DrawCircleV({x + 0.5f * r, y - 0.25f * r}, 0.55f * r, c);
                    tri({x - 1.02f * r, y - 0.05f * r}, {x + 1.02f * r, y - 0.05f * r}, {x, y + 0.95f * r}, c);
                    break;
                case DIAMOND:
                    tri({x, y - r}, {x - 0.72f * r, y}, {x + 0.72f * r, y}, c);
                    tri({x, y + r}, {x - 0.72f * r, y}, {x + 0.72f * r, y}, c);
                    break;
                case SPADE:
                    DrawCircleV({x - 0.5f * r, y + 0.2f * r}, 0.5f * r, c);
                    DrawCircleV({x + 0.5f * r, y + 0.2f * r}, 0.5f * r, c);
                    tri({x - 0.98f * r, y + 0.12f * r}, {x + 0.98f * r, y + 0.12f * r}, {x, y - r}, c);
                    tri({x, y + 0.1f * r}, {x - 0.38f * r, y + r}, {x + 0.38f * r, y + r}, c);
                    break;
                case CLUB:
                    DrawCircleV({x, y - 0.45f * r}, 0.42f * r, c);
                    DrawCircleV({x - 0.48f * r, y + 0.12f * r}, 0.42f * r, c);
                    DrawCircleV({x + 0.48f * r, y + 0.12f * r}, 0.42f * r, c);
                    DrawCircleV({x, y}, 0.3f * r, c);
                    tri({x, y + 0.1f * r}, {x - 0.38f * r, y + r}, {x + 0.38f * r, y + r}, c);
                    break;
            }
        }

        void draw_card(const Card& c, float x, float y, float w, float h) {
            static const char* labels[] = {"2", "3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A"};
            Rectangle r = {x, y, w, h};
            DrawRectangleRounded(r, 0.15f, 6, WHITE);
            DrawRectangleRoundedLinesEx(r, 0.15f, 6, 1.5f, {170, 170, 175, 255});
            Color col = (c.suit == HEART || c.suit == DIAMOND) ? CARD_RED : CARD_BLACK;
            text(labels[c.rank], x + w * 0.1f, y + h * 0.04f, h * 0.34f, col);
            draw_suit(c.suit, x + w * 0.5f, y + h * 0.68f, h * 0.19f, col);
        }

        void draw_card_back(float x, float y, float w, float h) {
            Rectangle r = {x, y, w, h};
            DrawRectangleRounded(r, 0.15f, 6, {40, 60, 140, 255});
            DrawRectangleRoundedLinesEx({x + 4, y + 4, w - 8, h - 8}, 0.15f, 6, 1.5f, {120, 140, 220, 255});
            DrawRectangleRoundedLinesEx(r, 0.15f, 6, 1.5f, {20, 25, 60, 255});
        }

        // ---------- table ----------

        // A point on the table ellipse at `angle`, scaled by fx/fy.
        static Vector2 on_table(float angle, float rx, float ry) {
            return {CENTER.x + rx * cosf(angle), CENTER.y + ry * sinf(angle)};
        }

        // Seats go clockwise starting from the bottom middle.
        static float seat_angle(int seat, int n) {
            return (90.0f + seat * 360.0f / n) * DEG2RAD;
        }

        void draw_table(const PlayerView& v, const HandResult* res) {
            ClearBackground(BACKGROUND);
            text("Hand #" + to_string(v.hand_number), 16, 12, 22, TEXT);
            text("Blinds " + to_string(SMALL_BLIND) + "/" + to_string(BIG_BLIND), 16, 40, 16, MUTED);

            DrawEllipse(CENTER.x, CENTER.y, TABLE_RX + 22, TABLE_RY + 22, RAIL);
            DrawEllipse(CENTER.x, CENTER.y, TABLE_RX, TABLE_RY, FELT);
            DrawEllipseLines(CENTER.x, CENTER.y, TABLE_RX - 14, TABLE_RY - 14, FELT_LINE);

            // community cards
            const float cw = 56, ch = 80, gap = 8;
            float x0 = CENTER.x - (5 * cw + 4 * gap) / 2;
            for (int i = 0; i < 5; i++) {
                float x = x0 + i * (cw + gap), y = CENTER.y - ch / 2;
                if (i < (int)v.community.size()) draw_card(v.community[i], x, y, cw, ch);
                else DrawRectangleRoundedLinesEx({x, y, cw, ch}, 0.15f, 6, 1.5f, FELT_LINE);
            }

            int pot = v.pot;
            if (res) {
                pot = 0;
                for (int w : res->won) pot += w;
            }
            text_centered("Pot: " + to_string(pot), CENTER.x, CENTER.y - ch / 2 - 22, 22, GOLD_TEXT);

            int n = v.stacks.size();
            for (int i = 0; i < n; i++) draw_seat(v, res, i, n);

            draw_log(v);
        }

        void draw_seat(const PlayerView& v, const HandResult* res, int i, int n) {
            float a = seat_angle(i, n);
            Vector2 s = on_table(a, TABLE_RX + 70, TABLE_RY + 75);
            bool out = v.folded[i] && v.stacks[i] == 0;
            bool acting = !res && i == v.id;
            bool winner = res && res->won[i] > 0;
            bool shown = res && !res->shown[i].empty();
            float fade = (out || (v.folded[i] && !winner)) ? 0.45f : 1.0f;

            // hole cards, between the seat and the middle of the table
            Vector2 k = on_table(a, TABLE_RX * 0.85f, TABLE_RY * 0.85f);
            const float hw = 40, hh = 56;
            if (!out && (!v.folded[i] || shown)) {
                const vector<Card>* face_up = shown ? &res->shown[i] : (i == v.id ? &v.hole : nullptr);
                for (int c = 0; c < 2; c++) {
                    float x = k.x - hw - 2 + c * (hw + 4), y = k.y - hh / 2;
                    if (face_up && c < (int)face_up->size()) draw_card((*face_up)[c], x, y, hw, hh);
                    else draw_card_back(x, y, hw, hh);
                }
            }

            // chips bet this street
            if (!res && v.street_bets[i] > 0) {
                Vector2 b = on_table(a, TABLE_RX * 0.6f, TABLE_RY * 0.55f);
                DrawCircleV(b, 10, {200, 50, 50, 255});
                DrawCircleLinesV(b, 7, WHITE);
                text(to_string(v.street_bets[i]), b.x + 14, b.y - 9, 18, TEXT);
            }

            // seat box
            Rectangle box = {s.x - 75, s.y - 29, 150, 58};
            DrawRectangleRounded(box, 0.25f, 8, Fade(PANEL, fade < 1 ? 0.7f : 1.0f));
            Color border = acting ? GOLD_TEXT : winner ? WIN_GREEN : PANEL_LINE;
            DrawRectangleRoundedLinesEx(box, 0.25f, 8, acting || winner ? 3.0f : 1.0f, border);

            text("Player " + to_string(i + 1), box.x + 10, box.y + 7, 16, Fade(TEXT, fade));
            float bx = box.x + box.width - 8;
            auto right_badge = [&](const string& t, Color bg, Color fg) {
                bx -= MeasureTextEx(font, t.c_str(), 13, 1).x + 14;
                badge(t, bx, box.y + 6, bg, fg);
            };
            if (!out) {
                if (i == v.big_blind) right_badge("BB", {70, 90, 150, 255}, TEXT);
                if (i == v.small_blind) right_badge("SB", {70, 90, 150, 255}, TEXT);
                if (i == v.button) right_badge("D", WHITE, CARD_BLACK);
            }
            text(to_string(v.stacks[i]), box.x + 10, box.y + 29, 22, Fade(GOLD_TEXT, fade));

            if (winner) {
                text_centered("+" + to_string(res->won[i]), box.x + box.width - 30, box.y + 41, 20, WIN_GREEN);
            } else if (!out && !v.folded[i] && v.stacks[i] == 0 && !res) {
                badge("ALL-IN", box.x + box.width - 62, box.y + 32, RED_BUTTON, TEXT);
            }

            // status under the box: hand name at showdown, otherwise last action
            string status = out ? "OUT"
                : shown ? res->hand_name[i]
                : v.folded[i] ? "FOLDED"
                : res ? "" : v.last_action[i];
            if (!status.empty()) {
                Color c = shown ? (winner ? WIN_GREEN : TEXT) : MUTED;
                text_centered(status, s.x, box.y + box.height + 12, 15, c);
            }
        }

        void draw_log(const PlayerView& v) {
            Rectangle p = {12, 615, 380, 173};
            DrawRectangleRounded(p, 0.08f, 8, PANEL);
            text("Action log", p.x + 12, p.y + 8, 16, MUTED);
            float y = p.y + 32;
            for (const string& line : v.log) {
                text(line, p.x + 12, y, 15, TEXT);
                y += 19;
            }
        }

        // ---------- bottom-right panels ----------

        optional<Action> action_panel(const PlayerView& v) {
            Rectangle p = {898, 575, 370, 213};
            DrawRectangleRounded(p, 0.08f, 8, PANEL);
            DrawRectangleRoundedLinesEx(p, 0.08f, 8, 1.5f, GOLD_TEXT);
            text("Player " + to_string(v.id + 1) + " to act", p.x + 12, p.y + 10, 20, TEXT);
            string info = v.to_call > 0 ? "To call: " + to_string(v.to_call) : "Stack: " + to_string(v.stacks[v.id]);
            Vector2 m = MeasureTextEx(font, info.c_str(), 16, 1);
            text(info, p.x + p.width - 12 - m.x, p.y + 13, 16, MUTED);

            float y = p.y + 44;
            if (button({p.x + 10, y, 170, 44}, "Fold", RED_BUTTON) || IsKeyPressed(KEY_F)) {
                return Action{FOLD};
            }
            int call_amount = min(v.to_call, v.stacks[v.id]);
            string call_label = v.to_call == 0 ? "Check"
                : "Call " + to_string(call_amount) + (call_amount == v.stacks[v.id] ? " (all-in)" : "");
            if (button({p.x + 190, y, 170, 44}, call_label, BLUE_BUTTON) ||
                IsKeyPressed(KEY_C) || IsKeyPressed(KEY_K)) {
                return Action{v.to_call == 0 ? CHECK : CALL};
            }

            if (!v.can_raise) {
                text_centered("Raising isn't allowed here", p.x + p.width / 2, p.y + 150, 16, MUTED);
                return nullopt;
            }

            auto clamp_raise = [&](int x) { return max(v.min_raise_to, min(v.max_raise_to, x)); };
            int pot_after_call = v.pot + v.to_call;

            // presets
            y += 54;
            const char* names[] = {"Min", "1/2 Pot", "Pot", "All-in"};
            int values[] = {v.min_raise_to, v.current_bet + pot_after_call / 2,
                            v.current_bet + pot_after_call, v.max_raise_to};
            for (int i = 0; i < 4; i++) {
                if (button({p.x + 10 + i * 89.0f, y, 83, 28}, names[i], GREY_BUTTON)) {
                    raise_to = clamp_raise(values[i]);
                }
            }

            // slider
            y += 42;
            Rectangle bar = {p.x + 20, y, 330, 8};
            Rectangle hit = {bar.x - 10, bar.y - 12, bar.width + 20, bar.height + 24};
            Vector2 mouse = GetMousePosition();
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, hit)) dragging = true;
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) dragging = false;
            int range = v.max_raise_to - v.min_raise_to;
            if (dragging && range > 0) {
                float t = (mouse.x - bar.x) / bar.width;
                raise_to = clamp_raise(v.min_raise_to + (int)lroundf(t * range));
            }
            float wheel = GetMouseWheelMove();
            if (wheel != 0) raise_to = clamp_raise(raise_to + (wheel > 0 ? BIG_BLIND : -BIG_BLIND));
            if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_RIGHT)) raise_to = clamp_raise(raise_to + BIG_BLIND);
            if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_LEFT)) raise_to = clamp_raise(raise_to - BIG_BLIND);

            float t = range > 0 ? (float)(raise_to - v.min_raise_to) / range : 1.0f;
            DrawRectangleRounded(bar, 1.0f, 6, PANEL_LINE);
            DrawRectangleRounded({bar.x, bar.y, bar.width * t, bar.height}, 1.0f, 6, ORANGE_BUTTON);
            DrawCircleV({bar.x + bar.width * t, bar.y + bar.height / 2}, 10, TEXT);

            // confirm
            y += 24;
            string verb = raise_to == v.max_raise_to ? "All-in " : v.current_bet == 0 ? "Bet " : "Raise to ";
            if (button({p.x + 10, y, 350, 44}, verb + to_string(raise_to), ORANGE_BUTTON) ||
                IsKeyPressed(KEY_R) || IsKeyPressed(KEY_ENTER)) {
                return Action{RAISE, raise_to};
            }
            return nullopt;
        }

        bool result_panel(const PlayerView& v, const HandResult& r) {
            Rectangle p = {898, 575, 370, 213};
            DrawRectangleRounded(p, 0.08f, 8, PANEL);
            DrawRectangleRoundedLinesEx(p, 0.08f, 8, 1.5f, WIN_GREEN);
            text("Hand #" + to_string(v.hand_number) + " result", p.x + 12, p.y + 10, 20, TEXT);
            float y = p.y + 44;
            for (int i = 0; i < (int)r.won.size() && y < p.y + 140; i++) {
                if (r.won[i] == 0) continue;
                string line = "Player " + to_string(i + 1) + " wins " + to_string(r.won[i]);
                if (!r.hand_name[i].empty()) line += " - " + r.hand_name[i];
                text(line, p.x + 12, y, 16, WIN_GREEN);
                y += 22;
            }
            return button({p.x + 10, p.y + 157, 350, 44}, "Next hand", BLUE_BUTTON) ||
                IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE);
        }
};
