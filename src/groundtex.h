// The ground's photo-scanned materials (Poly Haven, CC0): ten PBR sets loaded from
// assets/ground/*.jpg into two texture arrays for the terrain shader.
//   albedo array  RGB = base colour (sRGB, decoded by the hardware), A = height (0..1, per-material
//                 normalised, for blending)
//   normal array  RG = tangent-space normal (OpenGL convention: +y is toward the top of the image),
//                 B = roughness, A = ambient occlusion
// Layer order is fixed (the shader's table indexes it): see kGroundLayers in groundtex.cpp.
#pragma once
#include <string>

#include "gl.h"

struct GroundTextures {
    GLuint albedo = 0, normal = 0;
    int size = 0;  // texels per side of every layer (0: nothing loaded)
    bool ok() const { return albedo != 0 && size > 0; }
};

// Where the files are: assets/ground/ in the working folder, beside the executable or one or two
// folders up from it (a build/ folder). Empty if not found.
std::string findGroundDir();
// Loads (or reloads at another size) the textures at `maxSize` texels a side (1024 or 512; the
// images are 1k, so 512 is halved on the way in). Returns false and says why in `err` if any
// file is missing; the renderer then keeps the procedural look.
bool loadGroundTextures(GroundTextures& gt, int maxSize, std::string& err);
void freeGroundTextures(GroundTextures& gt);

// The terrain fragment shader's body (groundshader.cpp); it follows render.cpp's common, lit-declaration
// and procedural-terrain chunks.
extern const char* const kGroundShaderFS;
