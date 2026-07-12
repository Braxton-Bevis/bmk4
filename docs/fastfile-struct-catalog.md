# Fastfile struct catalog — the 32→64 translation inventory (BMK4)

*Companion to docs/FASTFILE_PLAN.md §4. Derived from the KISAK_LAYOUT_ASSERT
census: 254 grep hits = 251 real assert sites (excluding the 2 macro-definition
lines in src/universal/kisak_layout.h and the include comment at
src/universal/q_shared.h:9; NEXT_SESSION's "249" additionally excluded the two
non-struct oddballs at actor_aim.cpp:1103 and r_staticmodelcache.cpp:248). Of
those, ~55 distinct structs sit on the DATABASE/fastfile serialization path and
are listed below; the remaining ~196 assert sites are runtime-only structs
never serialized to fastfiles (script VM/compiler/debugger, playerState/bgs,
aim assist, vehicles, FX runtime/editor, devgui, groupvoice, enthandle, parse
contexts, skin cache) and need no fastfile translation work.*

**Size column**: the x86-32 sizeof. "asserted" = enforced by a
KISAK_LAYOUT_ASSERT in the header (machine-checkable spec for the 32-bit
staging image); "no assert" = size stated from the decompiled layout, verify
when writing the field map.

**Pointer-bearing = needs a 32→64 field map** (offsets shift, 4-byte disk
slots become 8-byte runtime slots). Pure-data structs can be copied from the
32-bit stream unchanged.

## A. XAssetHeader union members (top-level asset types)

| Struct | x86-32 size | Ptr-bearing | Header | Notes |
|---|---|---|---|---|
| XModelPieces | 12 (asserted) | yes | src/xanim/xmodel.h:105 | name, XModelPiece* pieces. Type 0x00 has NO Load handler (legacy) |
| PhysPreset | 0x2C (no assert) | yes | src/xanim/xanim.h:843 | name, sndAliasPrefix pointers |
| XAnimParts | 88 (asserted) | yes | src/xanim/xanim.h:150 | name + 9 data pointers + XAnimIndices union + notify/deltaPart ptrs |
| XModel | 220 (asserted) | yes | src/xanim/xmodel.h:59 | 12+ pointers (boneNames, quats, trans, surfs, materialHandles, collSurfs, boneInfo, physPreset, physGeoms...) |
| Material | 80 (asserted) | yes | src/gfx_d3d/r_material.h:461 | info.name + techniqueSet/textureTable/constantTable/stateBitsTable ptrs |
| MaterialPixelShader | 0x10 (no assert) | yes | src/gfx_d3d/r_material.h:270 | name + prog.ps (D3D handle) |
| MaterialVertexShader | 0x10 (no assert) | yes | src/gfx_d3d/r_material.h:257 | name + prog.vs |
| MaterialTechniqueSet | 148 (asserted) | yes | src/gfx_d3d/r_material.h:449 | name, remappedTechniqueSet, MaterialTechnique* techniques[34] |
| GfxImage | 36 (asserted) | yes | src/gfx_d3d/r_gfx.h:215 | GfxTexture union (all pointers incl. loadDef) + name; pointer sits mid-struct so 64-bit shifts all later fields |
| snd_alias_list_t | 12 (asserted) | yes | src/sound/snd_public.h:207 | aliasName, snd_alias_t* head |
| SndCurve | 72 (asserted) | yes | src/sound/snd_public.h:146 | filename ptr only, rest pure floats |
| LoadedSound | 44 (asserted) | yes | src/sound/snd_public.h:108 | name + embedded MssSoundCOD4 (data ptrs in _AILSOUNDINFO_COD4) |
| clipMap_t | 0x11C (no assert) | yes | src/qcommon/qcommon.h:1238 | name + ~20 array pointers (planes, staticModelList, materials, brushsides, ...) |
| ComWorld | 0x10 (no assert) | yes | src/xanim/xanim.h:434 | name, ComPrimaryLight* primaryLights |
| GameWorldSp | 0x2C (no assert) | yes | src/game/g_bsp.h:5 | name + PathData (pathnode arrays) |
| GameWorldMp | 0x4 (no assert) | yes | src/game/g_bsp.h:10 | name only |
| MapEnts | 0xC (no assert) | yes | src/xanim/xanim.h:405 | name, entityString |
| GfxWorld | 0x2DC (asserted) | yes | src/gfx_d3d/r_bsp.h:145 | ~30 pointers interleaved with data; largest translation job, GfxWorldDpvsStatic embedded |
| GfxLightDef | 0x10 (no assert) | yes | src/gfx_d3d/r_gfx.h:450 | name + GfxLightImage (GfxImage*) |
| Font_s | 24 (asserted) | yes | src/gfx_d3d/r_font.h:21 | fontName, material, glowMaterial, glyphs |
| MenuList | 0xC (no assert) | yes | src/ui/ui_shared.h:615 | name, menuDef_t** menus |
| menuDef_t | 0x11C (no assert) | yes | src/ui/ui_shared.h:504 | windowDef_t + font + itemDef ptr arrays + statement/expression trees; deepest pointer graph |
| LocalizeEntry | 0x8 (no assert) | yes | src/ui/ui_shared.h:836 | value, name — both pointers |
| WeaponDef | 2168 (asserted) | yes | src/xanim/xanim.h:~400-828 | dozens of const char*/Material*/XModel*/snd_alias_list_t*/FxEffectDef* interleaved with floats |
| SndDriverGlobals | 0x4 (no assert) | yes | src/xanim/xanim.h:830 | name only; type 0x18 has NO Load handler on PC |
| FxEffectDef | 32 (asserted) | yes | src/gfx_d3d/fxprimitives.h:63 | name, FxElemDef* elemDefs (FxElemDef itself pointer-heavy) |
| FxImpactTable | 8 (asserted) | yes | src/gfx_d3d/fxprimitives.h:488 | name, FxImpactEntry* table (33 FxEffectDef* per entry) |
| RawFile | 12 (asserted) | yes | src/xanim/xanim.h:835 | name, buffer — simplest real asset; FF2's first target |
| StringTable | 16 (asserted) | yes | src/universal/q_shared.h:852 | name, const char** values |

29 union members listed. Asset-type enum: src/xanim/xanim.h:905-944 (33 slots,
ASSET_TYPE_COUNT=0x21); 26 types have Load handlers (25 switch cases at
db_load.cpp:6746-6852; CLIPMAP/CLIPMAP_PVS share one), 7 are dead on PC
(0x00 xmodelpieces, 0x12 ui_map, 0x18 snddriverglobals, 0x1B aitype,
0x1C mptype, 0x1D character, 0x1E xmodelalias). Pool sizes
db_registry.cpp:44-113; names db_registry.cpp:269-304.

## B. Fastfile container structs

| Struct | x86-32 size | Ptr-bearing | Header | Notes |
|---|---|---|---|---|
| XAsset | 8 (asserted) | yes | src/xanim/xanim.h:996 | XAssetType + XAssetHeader (pointer union) |
| XAssetList | 16 (asserted) | yes | src/xanim/xanim.h:1114 | ScriptStringList + XAsset* assets; first thing deserialized from zone |
| ScriptStringList | 8 (asserted) | yes | src/xanim/xanim.h:1107 | const char** strings |
| XFile | 44 (asserted) | **no** | src/xanim/xanim.h:1122 | On-disk header: pure uint32 (size, externalSize, blockSize[9]); format-compatible on 64-bit as-is, but the 32-bit block sizes are the allocation source of truth |

## C. Transitive serialized structs (assert-covered)

| Struct | x86-32 size | Ptr-bearing | Header | Notes |
|---|---|---|---|---|
| XModelPiece | 16 (asserted) | yes | src/xanim/xmodel.h:98 | XModel* model (via XModelPieces) |
| XModelSurfs | 20 (asserted) | yes | src/xanim/xmodel.h:142 | XSurface* surfs (via XModel) |
| XBoneInfo | 40 (asserted) | **no** | src/xanim/xmodel.h:36 | pure floats — byte-identical on 64-bit |
| XModelCollTri_s | 48 (asserted) | **no** | src/xanim/xmodel.h:28 | pure float[4]x3 |
| XModelPartsLoad | 28 (asserted) | yes | src/xanim/xmodel.h:166 | DB-load staging struct, 6 pointers |
| XSurface | 56 (asserted) | yes | src/xanim/xanim.h:1174 | triIndices, vertInfo.vertsBlend, verts0, vertList pointers |
| XRigidVertList | 12 (asserted) | yes | src/xanim/xanim.h:1157 | XSurfaceCollisionTree* collisionTree |
| XSurfaceVertexInfo | 12 (asserted) | yes | src/xanim/xanim.h:1167 | uint16_t* vertsBlend |
| XAnimIndices | 4 (asserted) | yes | src/xanim/xanim.h:31 | union of pointers — doubles to 8 on arm64; live arm depends on runtime data (flag in field map) |
| GfxPackedVertex | 32 (asserted) | **no** | src/gfx_d3d/r_gfx.h:48 | pure packed data — bulk-copyable unchanged (XSurface verts0 / GfxWorld vd) |
| MaterialPixelShaderProgram | 12 (asserted) | yes | src/gfx_d3d/r_material.h:263 | IDirect3DPixelShader9* ps + GfxPixelShaderLoadDef |
| GfxPixelShaderLoadDef | 8 (asserted) | yes | src/gfx_d3d/r_gfx.h:657 | void* program + 2 uint16 |
| GfxWorldDpvsStatic | 0x68 (asserted) | yes | src/gfx_d3d/r_gfx.h:361 | 10 array pointers after 11 uint32 counters; embedded in GfxWorld |
| DObjAnimMat | 32 (asserted) | **no** | src/xanim/dobj.h:36 | pure floats (XModel baseMat, Load_DObjAnimMatArray) |
| snd_alias_t | 92 (asserted) | yes | src/sound/snd_public.h:179 | 4 string ptrs + soundFile/volumeFalloffCurve/speakerMap ptrs interleaved with floats |
| pathlink_s | 12 (asserted) | **no** | src/game/pathnode.h:97 | pure data (GameWorldSp PathData) |
| pathbasenode_t | 16 (asserted) | **no** | src/game/pathnode.h:176 | pure data (PathData) |
| pathnode_tree_nodes_t | 8 (asserted) | yes | src/game/pathnode.h:183 | uint16_t* nodes; unioned with pathnode_tree_t* child[2] which doubles on arm64 (union-arm hazard) |
| DynEntityDef | 0x60 (asserted) | yes | src/DynEntity/DynEntity_client.h:33 | xModel/destroyFx/destroyPieces/physPreset ptrs (via clipMap_t) |
| DynEntityPose | 0x20 (asserted) | **no** | src/DynEntity/DynEntity_client.h:49 | pure GfxPlacement + radius |
| DynEntityClient | 0xC (asserted) | **no** | src/DynEntity/DynEntity_client.h:56 | pure data |
| DynEntityColl | 0x14 (asserted) | **no** | src/DynEntity/DynEntity_client.h:65 | pure data |

## Totals and scope caveat

| Metric | Count |
|---|---|
| Structs cataloged above (fastfile path, assert-informed) | 55 |
| Pointer-bearing → need explicit 32→64 field maps | **45** |
| Pure-data → memcpy from 32-bit stream unchanged | **10** (XFile, XBoneInfo, XModelCollTri_s, GfxPackedVertex, DObjAnimMat, pathlink_s, pathbasenode_t, DynEntityPose, DynEntityClient, DynEntityColl) |

**Caveat**: this catalog covers assert-informed structs only — the
machine-checkable core. The full transitive serialized closure visible in
src/database/database.h's Load_* roster adds roughly 100+ unasserted types:
the FxElemDef tree, menu/itemDef/statement graph, clipMap_t internals
(cplane_s, cbrush_t, cLeaf_t, CollisionPartition), GfxWorld internals
(GfxCell, GfxLightGrid, GfxSurface, GfxAabbTree), the XAnimDeltaPart family,
SoundFile/SpeakerMap, pathnode_constant_t, XModelCollSurf_s,
PhysGeomList/BrushWrapper. A complete 64-bit fastfile loader touches an
estimated **~150-165 distinct struct layouts, of which ~110-120 are
pointer-bearing**. As each unasserted type's field map is written, its 32-bit
size should be pinned (in the field-map table or a new assert) so CI can check
it the same way the asserted core is checked.
