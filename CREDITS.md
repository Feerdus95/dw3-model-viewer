# Credits

## dw3_decomp

`src/model.cpp`, `src/game.cpp` and `src/effect.cpp` port what the battle does with
its models, fighters and 2D effects from dw3_decomp's C decompilation of the game, under its license: the models'
reading and posing (`src/fightstg/fightstg.c`, `src/main/gfx/tim_loader.c`,
`src/main/file/decompressor.c`), and the fighters' file, faces, action scripts and
stage lights (`src/fightstg/fighter_info.c`, `face.c`, `battle_script.c`, `lights.c`,
`models.c`, and their headers), and the 2D effects (`src/fightstg/sprite_effect.c`,
`sprite_anim.c`, `src/main/gfx/sprite_drawer.c`, `src/wfightmn/wfightmn.c`).

```
MIT License

Copyright (c) 2026 juandav

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Dear ImGui

`third_party/imgui`, unmodified: MIT, see `third_party/imgui/LICENSE.txt`.

## SDL3

Linked as `SDL3.dll`, from SDL3's MinGW development package: zlib license.
