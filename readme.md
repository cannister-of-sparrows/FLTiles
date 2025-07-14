# FLTK Tiles Demo (OpenGL 1.1 + FLTK)

This is a demonstration application for rendering large 2D tilemaps using OpenGL 1.1 and the FLTK GUI toolkit. It is intended as a performance-focused foundation for building tile-based editors, especially on systems that do not support modern OpenGL.

The viewer renders tilemaps using immediate-mode OpenGL, optimized with basic visibility culling and adaptive rendering based on zoom level.

## Features

- Renders arbitrarily large tilemaps efficiently using OpenGL 1.1
- Uses FLTK for windowing and input handling
- Loads a PNG tileset using `stb_image.h`
- Smooth panning via mouse drag
- Zooming centered on mouse position using mouse wheel
- FPS display in window title
- Tile under mouse is highlighted with an outline
- View frustum culling to avoid rendering off-screen tiles
- Adaptive downsampling: when zoomed out, tiles are drawn as grouped blocks
- Horizontal and vertical scrollbars for panning
- Scrollbars sync with pan and zoom and clamp to map bounds

## Usage

- Left-click and drag to pan
- Use mouse wheel to zoom in or out (centered on cursor)
- Scrollbars can also be used to pan
- Hover the mouse to highlight a tile
- Tile rendering adapts based on zoom level for performance

## Notes

- Tilemaps are rendered using OpenGL immediate mode (`glBegin`/`glEnd`)
- This approach is compatible with systems limited to OpenGL 1.1

## License

### Code

The code in this project is licensed under the GNU General Public License v3.0.
See the [LICENSE](LICENSE) file for details.

### Tileset

This project includes graphical assets from the **Sprout Lands Basic Pack** by **Cup Nooble**, which is licensed separately.
See the [TILESETLICENSE](TILESETLICENSE) file for details.
