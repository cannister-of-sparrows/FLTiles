/*
 * FLTK Tiles Demo
 * Copyright (C) 2025 Cannister of Sparrows <cannister_of_sparrows@proton.me>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
 
#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include <FL/gl.h>
#include <FL/glu.h>
#include <FL/Fl_Window.H>
#include <FL/Fl_Scrollbar.H>

#include <chrono>
#include <cstdio>
#include <cmath>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Settings
const int TILE_SIZE = 16;
const int MAP_WIDTH = 10000;
const int MAP_HEIGHT = 10000;
const int TILES_PER_ROW = 8;
const float MIN_VISIBLE_PIXELS = 4.0f;

class TilemapScrollView; // Forward declare

class TilemapWindow : public Fl_Gl_Window {
public:
    TilemapWindow(int x, int y, int w, int h);
    ~TilemapWindow();

    void draw() override;
    int handle(int event) override;

    void loadTileset(const char* filename);
    void drawTile(int tileIndex, int x, int y, int size);
    void updateHoveredTile(int mouseX, int mouseY);

    // Shared view state
    float offsetX = 0.0f, offsetY = 0.0f;
    float zoom = 1.0f;
    TilemapScrollView* parentView = nullptr;

private:
    GLuint tilesetTexture = 0;
    int tilesetWidth = 0, tilesetHeight = 0;
    int tileMap[MAP_HEIGHT][MAP_WIDTH];

    int lastMouseX = 0, lastMouseY = 0;
    bool dragging = false;
    int hoveredX = -1, hoveredY = -1;

    std::chrono::steady_clock::time_point lastFpsTime;
    int frames = 0;
};

class TilemapScrollView : public Fl_Group {
public:
    TilemapWindow* canvas;
    Fl_Scrollbar* hscroll;
    Fl_Scrollbar* vscroll;

    TilemapScrollView(int X, int Y, int W, int H);
    void updateScrollbars();
    void resize(int X, int Y, int W, int H) override;
};

// =============== TilemapWindow Implementation ==================

TilemapWindow::TilemapWindow(int x, int y, int w, int h)
    : Fl_Gl_Window(x, y, w, h) {
    mode(FL_RGB | FL_DOUBLE | FL_DEPTH); // Enable double-buffering for smooth drawing

    // Fill the tilemap with random tile indices
    for (int y = 0; y < MAP_HEIGHT; ++y)
        for (int x = 0; x < MAP_WIDTH; ++x)
            tileMap[y][x] = rand() % (TILES_PER_ROW * TILES_PER_ROW);

    Fl::add_idle([](void* userdata) {
        auto* self = static_cast<TilemapWindow*>(userdata);
        self->redraw(); // Continuous redraw for smooth FPS updates
    }, this);
}

TilemapWindow::~TilemapWindow() {
    if (tilesetTexture)
        glDeleteTextures(1, &tilesetTexture);
}

void TilemapWindow::loadTileset(const char* filename) {
    int n;
    unsigned char* data = stbi_load(filename, &tilesetWidth, &tilesetHeight, &n, 4);
    if (!data) {
        fprintf(stderr, "Failed to load image: %s\n", filename);
        exit(1);
    }

    // Upload the tileset as a texture to the GPU
    glGenTextures(1, &tilesetTexture);
    glBindTexture(GL_TEXTURE_2D, tilesetTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); // Crisp pixel edges
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tilesetWidth, tilesetHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
}

void TilemapWindow::drawTile(int tileIndex, int x, int y, int size) {
    // Calculate texture coordinates for a specific tile index in the atlas
    float u = (tileIndex % TILES_PER_ROW) * (float)TILE_SIZE / tilesetWidth;
    float v = (tileIndex / TILES_PER_ROW) * (float)TILE_SIZE / tilesetHeight;
    float du = (float)TILE_SIZE / tilesetWidth;
    float dv = (float)TILE_SIZE / tilesetHeight;

    // Basic OpenGL immediate mode rendering for a single textured quad
    glBegin(GL_QUADS);
    glTexCoord2f(u, v);             glVertex2f(x, y);
    glTexCoord2f(u + du, v);        glVertex2f(x + size, y);
    glTexCoord2f(u + du, v + dv);   glVertex2f(x + size, y + size);
    glTexCoord2f(u, v + dv);        glVertex2f(x, y + size);
    glEnd();
}

void TilemapWindow::draw() {
    if (!valid()) {
        // Only initialize OpenGL context and projection once
        glLoadIdentity();
        glOrtho(0, w(), h(), 0, -1, 1); // Set up orthographic 2D projection
        loadTileset("tileset.png");
        glEnable(GL_TEXTURE_2D);
        lastFpsTime = std::chrono::steady_clock::now();
    }

    glClearColor(0.1f, 0.1f, 0.1f, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glPushMatrix();
    glTranslatef(offsetX, offsetY, 0); // Apply panning
    glScalef(zoom, zoom, 1.0f);        // Apply zoom scaling

    // Compute visible world bounds in tile space
    float invZoom = 1.0f / zoom;
    float viewLeft = -offsetX * invZoom;
    float viewTop = -offsetY * invZoom;
    float viewRight = (w() - offsetX) * invZoom;
    float viewBottom = (h() - offsetY) * invZoom;

    // Determine the visible tile range in the current viewport.
    // This acts as a form of *view frustum culling* in tile space.
    //
    // The camera's visible rectangle in world space (computed earlier) is
    // divided by TILE_SIZE to find the tile indices that intersect it.
    //
    // floor(): ensures we start drawing from the first partially visible tile
    // ceil(): ensures we include the last partially visible tile
    //
    // The result is clamped to the tilemap bounds (0 to MAP_WIDTH/HEIGHT)
    // to avoid accessing out-of-bounds tile data.
    int tileX0 = std::max(0, (int)std::floor(viewLeft / TILE_SIZE));
    int tileY0 = std::max(0, (int)std::floor(viewTop / TILE_SIZE));
    int tileX1 = std::min(MAP_WIDTH,  (int)std::ceil(viewRight / TILE_SIZE));
    int tileY1 = std::min(MAP_HEIGHT, (int)std::ceil(viewBottom / TILE_SIZE));

    glBindTexture(GL_TEXTURE_2D, tilesetTexture);

    // Skip over tiles when zoomed out too far to reduce draw calls.
    // Instead of drawing 1000x1000 tiles at 1px each, draw representative tiles at larger size.
    float pixelsPerTile = TILE_SIZE * zoom;
    int step = std::max(1, (int)std::ceil(MIN_VISIBLE_PIXELS / pixelsPerTile));

    for (int y = tileY0; y < tileY1; y += step) {
        for (int x = tileX0; x < tileX1; x += step) {
            int tile = tileMap[y][x];
            drawTile(tile, x * TILE_SIZE, y * TILE_SIZE, TILE_SIZE * step);
        }
    }

    // Draw an outline around the currently hovered tile
    if (hoveredX >= 0 && hoveredY >= 0) {
        glDisable(GL_TEXTURE_2D);
        glColor3f(1.0, 0.0, 0.0);
        int tx = hoveredX * TILE_SIZE;
        int ty = hoveredY * TILE_SIZE;
        glBegin(GL_LINE_LOOP);
        glVertex2f(tx, ty);
        glVertex2f(tx + TILE_SIZE, ty);
        glVertex2f(tx + TILE_SIZE, ty + TILE_SIZE);
        glVertex2f(tx, ty + TILE_SIZE);
        glEnd();
        glEnable(GL_TEXTURE_2D);
    }

    glPopMatrix();
    glColor3f(1, 1, 1); // reset state

    // FPS
    frames++;
    auto now = std::chrono::steady_clock::now();
    float seconds = std::chrono::duration<float>(now - lastFpsTime).count();
    if (seconds >= 1.0f) {
        char title[128];
        snprintf(title, sizeof(title), "Tilemap Viewer - FPS: %d", frames);
        window()->label(title);
        frames = 0;
        lastFpsTime = now;
    }
}

void TilemapWindow::updateHoveredTile(int mouseX, int mouseY) {
    float fx = (mouseX - offsetX) / zoom;
    float fy = (mouseY - offsetY) / zoom;
    int tileX = (int)(fx / TILE_SIZE);
    int tileY = (int)(fy / TILE_SIZE);
    if (tileX >= 0 && tileY >= 0 && tileX < MAP_WIDTH && tileY < MAP_HEIGHT) {
        hoveredX = tileX;
        hoveredY = tileY;
    } else {
        hoveredX = hoveredY = -1;
    }
}

int TilemapWindow::handle(int event) {
    switch (event) {
    case FL_PUSH:
        if (Fl::event_button() == FL_LEFT_MOUSE) {
            dragging = true;
            lastMouseX = Fl::event_x();
            lastMouseY = Fl::event_y();
        }
        return 1;
    case FL_DRAG:
        if (dragging) {
            int dx = Fl::event_x() - lastMouseX;
            int dy = Fl::event_y() - lastMouseY;
            offsetX += dx;
            offsetY += dy;
            lastMouseX = Fl::event_x();
            lastMouseY = Fl::event_y();
            if (parentView) parentView->updateScrollbars();
        }
        return 1;
    case FL_RELEASE:
        dragging = false;
        return 1;
    case FL_MOUSEWHEEL: {
        int mx = Fl::event_x();
        int my = Fl::event_y();
        float worldX = (mx - offsetX) / zoom;
        float worldY = (my - offsetY) / zoom;
        float zoomFactor = (Fl::event_dy() > 0) ? 0.9f : 1.1f;
        zoom *= zoomFactor;
        offsetX = mx - worldX * zoom;
        offsetY = my - worldY * zoom;
        if (parentView) parentView->updateScrollbars();
        return 1;
    }
    case FL_MOVE:
        updateHoveredTile(Fl::event_x(), Fl::event_y());
        return 1;
    }
    return Fl_Gl_Window::handle(event);
}

// =============== TilemapScrollView Implementation ==================

TilemapScrollView::TilemapScrollView(int X, int Y, int W, int H)
    : Fl_Group(X, Y, W, H) {

    canvas = new TilemapWindow(X, Y, W - 16, H - 16);
    canvas->parentView = this;

    hscroll = new Fl_Scrollbar(X, Y + H - 16, W - 16, 16);
    hscroll->type(FL_HORIZONTAL);
    hscroll->callback([](Fl_Widget* w, void* data) {
        auto* view = static_cast<TilemapScrollView*>(data);
        view->canvas->offsetX = -view->hscroll->value();
        view->canvas->redraw();
    }, this);

    vscroll = new Fl_Scrollbar(X + W - 16, Y, 16, H - 16);
    vscroll->type(FL_VERTICAL);
    vscroll->callback([](Fl_Widget* w, void* data) {
        auto* view = static_cast<TilemapScrollView*>(data);
        view->canvas->offsetY = -view->vscroll->value();
        view->canvas->redraw();
    }, this);

    end();
    resizable(canvas);
    updateScrollbars();
}

void TilemapScrollView::updateScrollbars() {
    int contentW = (int)(MAP_WIDTH * TILE_SIZE * canvas->zoom);
    int contentH = (int)(MAP_HEIGHT * TILE_SIZE * canvas->zoom);
    int viewW = canvas->w();
    int viewH = canvas->h();

    hscroll->value((int)-canvas->offsetX, viewW, 0, std::max(viewW, contentW));
    vscroll->value((int)-canvas->offsetY, viewH, 0, std::max(viewH, contentH));
}

void TilemapScrollView::resize(int X, int Y, int W, int H) {
    Fl_Group::resize(X, Y, W, H);
    canvas->resize(X, Y, W - 16, H - 16);
    hscroll->resize(X, Y + H - 16, W - 16, 16);
    vscroll->resize(X + W - 16, Y, 16, H - 16);
    updateScrollbars();
}

// =============== Main ==================

int main(int argc, char** argv) {
    Fl_Window win(800, 600, "Tilemap Viewer");
    TilemapScrollView viewer(0, 0, 800, 600);
    win.end();
    win.show(argc, argv);
    return Fl::run();
}
