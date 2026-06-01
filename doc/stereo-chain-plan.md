# Stereo Chaining Plan

## Goal

Allow loading two stereo plugins in series on BBA (e.g., TBDaits → Claude),
processing the output of the first as input to the second within a single
audio block.

---

## Current Constraints

| Constraint | Status |
|-----------|--------|
| SP arena (112 KB) | Already in PSRAM — no DRAM cost |
| Free PSRAM | ~3.4 MB — plenty for second plugin |
| Free DRAM | ~22 KB — tight, must not grow |
| Audio loop gate | `if (!isStereoCH0)` skips `sp[1]` when ch0 is stereo |
| Allocator | Only `CH0`, `CH1`, `STEREO` — no stereo-chain type |

---

## Key Insight

With the arena already in PSRAM and 3.4 MB free, **there is no need to split
the existing 112 KB**. The second stereo plugin should allocate its own
working memory directly from PSRAM. This avoids changing the existing arena
logic entirely.

---

## Proposed Architecture

### 1. New AllocType

Add `ctagSPAllocator::AllocationType::STEREO_CHAIN`.

- `sp[0]` gets the existing full arena (112 KB) — unchanged.
- `sp[1]` allocates from PSRAM via `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`.
- Default allocation size for `sp[1]`: 112 KB (can be adjusted per plugin).

### 2. Audio Loop Change

In `SPManager::audio_task()`, replace:

```cpp
if (!isStereoCH0) {
    if (ch01Daisy) { ... copy L→R ... }
    if (sp[1] != nullptr) sp[1]->Process(pd);
}
```

With:

```cpp
if (!isStereoCH0) {
    // existing dual-mono path — unchanged
    if (ch01Daisy) { ... }
    if (sp[1] != nullptr) sp[1]->Process(pd);
} else if (stereoChainMode && sp[1] != nullptr) {
    // stereo chain: output of sp[0] feeds sp[1]
    sp[1]->Process(pd);
}
```

Where `stereoChainMode` is set when `AllocType == STEREO_CHAIN`.

### 3. Plugin Browser UI

In `UIMenuPageHome` (plugin browser):

- When selecting a stereo plugin, after Ch0 confirmation, show:
  > "Chain another stereo plugin? [No] / [Yes]"
- If Yes, open the same plugin browser to pick `sp[1]`.
- Store as `sp[0]` + `sp[1]` IDs in config JSON.

### 4. Memory Impact

| Resource | Before | After | Delta |
|----------|--------|-------|-------|
| SP arena (PSRAM) | 112 KB | 112 KB (sp[0]) | 0 |
| Chain block (PSRAM) | 0 | ~112 KB (sp[1]) | +112 KB |
| Free PSRAM | ~3.4 MB | ~3.3 MB | Still plentiful |
| DRAM | ~22 KB free | ~22 KB free | Unchanged |

### 5. Constraint — Claude / GDVerb Arena Needs

Plugins that need >112 KB from arena:

| Plugin | Arena need | Issue |
|--------|-----------|-------|
| Claude | ~67 KB | Fits in 112 KB — no change needed |
| GDVerb | ~100 KB | Fits in 112 KB — no change needed |
| GDVerb2 | ~111 KB | Fits in 112 KB — no change needed |

These fit within the single 112 KB arena, so no restructuring is required
for chain mode. If both plugins in a chain are heavy arena users, the
combined PSRAM usage would be ~200-224 KB — still only ~7% of the 3.4 MB
available.

---

## Implementation Steps

1. **`ctagSPAllocator`**: Add `STEREO_CHAIN` enum value. When STEREO_CHAIN,
   set `buffer1` = arena, `buffer2` = nullptr (sp[1] allocates separately).

2. **`SPManager`**:
   - Add `bool stereoChain` flag.
   - In `SetSoundProcessorChannel()`: if loading stereo on ch1 and ch0 is
     already stereo, set `stereoChain = true`, allocate sp[1] from PSRAM.
   - In `audio_task()`: skip the `isStereoCH0` gate when `stereoChain`.

3. **`UIMenuPageHome`**: Add chain-selection flow after picking a stereo
   plugin.

4. **Config persistence**: Save `sp[1]` plugin ID to `spm-config.jsn`.

---

## Open Questions

- Should chain mode be indicated in the UI (e.g., "TBDaits → Claude" in
  ROOT panel indicator)?
- What happens when user navigates to PARAMS panel in chain mode — show
  params for sp[0] or sp[1]?
- Should `toStereoCH0/toStereoCH1` matrix apply before or after the chain?
  (Probably after — chain output → stereo matrix → DAC.)
- What about `ch01Daisy` interaction with stereo chain (should be ignored).
