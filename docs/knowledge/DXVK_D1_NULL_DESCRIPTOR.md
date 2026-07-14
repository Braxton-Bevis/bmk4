# D1 — DXVK dummy-resources patch for the MoltenVK null-descriptor gap

*Lane 5 (Sol) spike deliverable, coordinator-audited 2026-07-14. Patch:
[`scripts/platform/ios/dxvk-v2.7.1-ios-null-descriptor.patch`](../../scripts/platform/ios/dxvk-v2.7.1-ios-null-descriptor.patch).*

## Problem (M12 addendum)

MoltenVK 1.4.1 does not expose `VK_EXT_robustness2` / `nullDescriptor`.
Stock DXVK writes `VK_NULL_HANDLE` into descriptor slots for unbound
resources (`dxvk_context.cpp` ~6326-6336), which is illegal without that
feature. Real COD4 shaders leave slots unbound routinely, so this blocks
every engine-driven frame on iOS/iPadOS.

## The patch

- Size 19,517 bytes / 528 lines; SHA-256
  `5d2ac66edb35271dd8615d8ca5ef1edf0f01f6767c5f78e05dabd24495c48384`.
- Base: DXVK v2.7.1 (`c3dd74be6baec53786d4e064a572185b70347a17`), applied
  **after** `dxvk-v2.7.1-ios.patch`. Exact pristine-chain
  `git apply --check` passed (EXACT_CHAIN_GIT_APPLY_CHECK=PASS); the BMK4
  base patch's files are not modified.
- Touches only `src/dxvk/`: dxvk_adapter.cpp, dxvk_context.cpp,
  dxvk_device.{cpp,h}, dxvk_unbound.{cpp,h}.

Fallbacks when `nullDescriptor == VK_FALSE` (all gated on
`!m_device->features().extRobustness2.nullDescriptor`; platforms exposing
the feature keep the original null-handle path):

| Unbound resource | Fallback |
|---|---|
| Uniform/storage buffer | existing persistent zero-filled dummy buffer |
| Uniform/storage texel buffer | persistent `R32_UINT` views of that buffer |
| Sampled image | persistent 1×1 image view, zero component swizzles |
| Storage image | persistent 1×1 `R32_UINT` image view |
| Sampler | existing persistent default sampler |
| Vertex buffer | existing zero-filled dummy buffer |

Three dummy images cover 1D/array, 2D/array/cube/cube-array, and 3D view
types, cleared once via a startup command list before any frontend work.
Descriptor-buffer support is disabled only for the odd combination
"descriptor buffers supported but nullDescriptor absent", forcing the
corrected legacy path.

## Batching risk assessment

The patch changes no descriptor counts, binding order, dirty-set masks,
allocation, or batching: `m_legacyDescriptors.infos` is still resized
before filling; dummies are copied into the same per-binding entries; the
32-bit update-template path still resets `descriptorCount` per set; one
`updateDescriptorSets` batch as before. Persistent resources live in
`DxvkObjects`, so payloads cannot outlive their Vulkan objects.

Remaining risks:

- Writable dummy storage resources don't perfectly emulate null
  descriptors (writes are not discarded). Outside real D3D9 SM2/SM3 use.
- Dummy sampled images are single-sampled; an unbound *multisampled*
  sampled-image slot would need a sample-count-specific dummy.
- The finite dummy vertex buffer relies on core `robustBufferAccess`
  (still required) for oversized accesses.
- Startup image clears are submitted asynchronously; imported devices with
  unusual external queue callbacks deserve a Mac runtime check.

## Validation plan (first Mac/Mac-lane sitting)

1. Clone DXVK v2.7.1 shallow, apply `dxvk-v2.7.1-ios.patch` then this
   patch, build via `scripts/platform/ios/build-dxvk-ios.sh` (it skips the
   clone when the work tree exists), assert `libdxvk_d3d9.a` is arm64 and
   exports `Direct3DCreate9(Ex)`, then refresh `ios/libs/iphoneos` and
   rebuild against pinned MoltenVK 1.4.1.
2. Engine-like unbound-slot probe (disposable `D3D9Smoke.mm` variant,
   checked-in precompiled vs_2_0/ps_2_0 DWORD arrays, no runtime D3DX):
   clear to a distinct color; fullscreen triangle on stream 0; declare a
   second vertex stream and leave stream 1 null; leave texture slot 3 null
   with the pixel shader actually sampling `s3` into a marker color; draw,
   read back center pixel, Present; record all HRESULTs in the marker
   file. Run with `MVK_CONFIG_DEBUG=1 DXVK_LOG_LEVEL=debug
   DXVK_WSI_DRIVER=iOS`.
3. Acceptance gate: device log confirms MoltenVK 1.4.1 lacks
   robustness2/nullDescriptor; shader creation and DrawPrimitive return
   D3D_OK; center pixel equals marker color within one LSB per channel
   (unbound sample returned zero) and differs from the clear color;
   Present returns D3D_OK; no MoltenVK descriptor/layout errors, device
   loss, crash, or validation complaint. A validation-only debug build
   asserts at the batch boundary before `updateDescriptorSets` that every
   handle is non-null when `nullDescriptor` is false.

## Verification status

Static only so far: clean apply chain on pristine v2.7.1, `git diff
--check`, normalized-tree comparison, and a declaration/definition/
call-site walk all passed. No compiler on this host — first compile
evidence comes from the Mac lane at slice 8.
