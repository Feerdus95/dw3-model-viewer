/* Digital Card Battle's Digimon models (PAK files from F.DRV and G.DRV) as .dcbm files, what the Python tools under tools/dcb did. */
#pragma once
#include <array>
#include <string>
#include <vector>

#include "bytes.h"

struct DcbRef { int vi, uv; }; /* a corner: its vertex and its texture coordinates (u | v << 8) */
struct DcbAttr { int cl, tp; bool tex; int code; int col; };
struct DcbPart {
    std::vector<std::array<int, 3>> verts;
    std::vector<std::array<DcbRef, 3>> tris;
    std::vector<DcbAttr> attr; /* one a triangle */
};
struct DcbBone {
    int parent, pos[3];
    std::vector<DcbPart> parts;
};
struct DcbChunk { int id, sub; size_t off; long size; };

std::vector<DcbChunk> dcb_chunks(Span b);
std::vector<DcbBone> dcb_omd_parse(Span d); /* tmd_sort.c's sortModelObject / sortModelPrimitives; throws on a bad model */
Bytes dcb_decompress(Span b, size_t off); /* LZ + Huffman (decompress.c) */
/* PAK B -> OUTDIR/digimon/NAME/NAME.dcbm; throws when the model can't be read */
void dcb_convert(Span pak, const std::string &outdir, const std::string &name);
