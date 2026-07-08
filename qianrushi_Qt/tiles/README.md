# Tile cache

Put real map tile PNG files here using the standard Web Mercator layout:

```text
tiles/
  16/
    x/
      y.png
```

The app copies this folder to the build output after each build. At runtime
`TileMapProvider` loads `tiles/{z}/{x}/{y}.png` from the executable directory.
If a tile is missing, the map falls back to a generated light-color offline tile.
