# AGENTS.md — 64-bit port context

This file documents the 64-bit porting work on branch `64bit_ai_fix` so an agent
(or human) with fresh context can pick up where it left off. Read this before
touching anything related to script/property layout, package loading, or the
script VM.

## What this repo is

Unreal Engine 1 **v200** source (1998) with community modifications to run on
modern systems (SDL2 windowing, GL/GLES renderers, GCC/Clang support). It was
historically 32-bit only. The `64bit_ai_fix` branch makes it build **and run**
as a native 64-bit binary. Verified end-to-end on macOS arm64: the game boots
the Intro map into playable gameplay with the Unreal v205 demo content.

**Content compatibility matters**: this engine loads package file versions up
to ~v61-67 (v200 retail / v205 demo assets). Unreal Gold / 225 / 226 content is
**v68** and fundamentally incompatible — its script packages reference
`Core.StrProperty` (dynamic strings), a class this engine generation does not
have. Version-gated v68 *summary/name-table* parsing exists (see below) so v68
packages fail with a clean error instead of a garbage-allocation crash, but
they cannot actually run.

## The core problem of the 64-bit port

UnrealScript packages (`*.u`) were compiled by a 32-bit compiler and are
treated as immutable content. They bake in three 32-bit assumptions:

1. **Bytecode object references**: script bytecode embedded raw 4-byte object
   pointers inline. Jump/skip offsets stored in the same bytecode assume those
   references stay 4 bytes.
2. **Class/struct layouts**: property offsets are *not* trusted from the
   package — they are recomputed at load time (`UStruct::LinkOffsets`) — but
   native classes' layouts must exactly match their hand-written C++ mirror
   structs, whose pointers doubled in size.
3. **Function frame metadata**: `UFunction::ParmsSize`, `ReturnValueOffset`,
   and the per-parameter size list in `EX_BeginFunction` bytecode were computed
   with 4-byte object references.

The port solves all three **at load time**; package files are never modified.

### 1. Bytecode: pointers → GObjects indices

`UStruct::SerializeExpr` (`Source/Core/Src/UnClass.cpp`, `XFER_OBJ` macro)
converts every inline object reference to a 4-byte `GObjects` index on load
(and back to a pointer via the linker on save). Exec functions resolve them
with `GObj.GetIndexedObject(Stack.ReadInt())` — see `UnCorSc.cpp`
(`execLocalVariable`, `execObjectConst`, `execBoolVariable`, etc.). In-memory
bytecode layout therefore stays byte-identical to 32-bit, keeping all baked
jump offsets valid. GC safety: `FArchiveTagUsed` traverses the same path, so
bytecode-referenced objects are tagged reachable and their indices can't be
reused while the bytecode lives.

**Trap**: any code that peeks at bytecode operands must read a 4-byte index,
never a pointer. `execBoolVariable` had exactly this bug (peeks at the next
opcode's operand).

### 2. Native class layout: pack(4) mirrors + placeholder table

Ground rules discovered during the port:

- The generated mirror headers (`EngineClasses.h`, `CoreClasses.h`,
  `FireClasses.h`) wrap classes in `#pragma pack(push,4)`. On 64-bit this means
  **pointers are 8 bytes but 4-aligned, with no alignment padding anywhere**.
- The script-side layout rules replicate exactly that: `UObjectProperty::Link`
  aligns to 4 (size `sizeof(UObject*)`), `UStructProperty::Link` aligns to 4,
  `UStruct::LinkOffsets` pads final sizes to 4.
- Hand-written native headers that mirror script classes were **not** in
  pack(4) and have been wrapped: `Engine/Inc/UnTex.h`, `Engine/Inc/UnPlayer.h`,
  `Engine/Inc/UnCamera.h`, `Engine/Src/UnCon.h`, `Fire/Src/FractalPrivate.h`.
  If you add/port another native class with script-declared properties, it must
  be `#pragma pack(push,4)` too.
- Script mirrors hide native pointer members behind `int` placeholders (e.g.
  `Object.uc`'s `int ObjectInternal[6]` covers vtable + native fields;
  `Player.vfOut` covers the `FOutputDevice` vtable; `Texture.Mips` is a
  12-byte struct standing in for a `TArray`). These are widened at link time by
  the **placeholder table** in `GetPlaceholderSize()`
  (`Source/Core/Src/UnClass.cpp`). If a new native class mismatches, it
  probably needs an entry here.

**Validation**: `UStruct::IntrinsicSize` records `sizeof(C++ class)` at
intrinsic registration. After `LinkOffsets`, any class whose script-computed
size differs from its C++ size logs
`Native class size mismatch: <class> script=<n> C++=<m>` plus a full property
offset dump, then takes the max. **A correct build logs zero mismatches** — if
you see one, script and C++ will read that class's members at different
offsets; fix it, don't ignore it. Startup `VERIFY_CLASS_OFFSET` asserts
(`UnGame.cpp`) cross-check individual members.

**Trap**: `IntrinsicSize` (like `UClass::Constructor`) must survive the
in-place reconstruction that happens when a package export replaces a
registered intrinsic class. `FObjectManager::AllocateObject` saves/restores it,
and the normal `UStruct` constructor deliberately does **not** initialize it.
Fresh objects get it zeroed by `InitProperties`. Do not "fix" the missing
initializer.

### 3. Function frames recomputed

- `UFunction::Serialize` recomputes `ParmsSize` and `ReturnValueOffset` from
  the freshly linked parameter properties after load (serialized values are
  32-bit stale).
- `UObject::CallFunction` (`UnCorSc.cpp`) takes each parameter's size and
  destination offset from its property. The `EX_BeginFunction` size list in
  bytecode is still *consumed* (to advance the code pointer) but its stale
  32-bit sizes are ignored. `ProcessEvent` only skips the list, so it needed no
  change.

## Other 64-bit / platform changes in this branch

- `UPTRINT`/`PTRINT` typedefs; pointer-safe `Align`, `FMemStack`,
  `STRUCT_OFFSET`, `CPP_PROPERTY`; window handles as `UPTRINT`; `%p` in log
  formats (earlier commit `69a34e1` plus fixes on top).
- `DLLEXT` is `.dylib` on Apple (`UnGcc.h`) — package binding fails without it.
- v68 package summary (GUID + generation history instead of heritage table) and
  length-prefixed name tables (file version ≥ 64) parse correctly —
  `Source/Core/Src/UnLinker.h`, `Source/Core/Inc/UnName.h`. Loading still stops
  at missing v68-era classes by design.
- `uuid_t` → `unreal_uuid_t` rename, `malloc.h` guard, `pthread_setname_np`
  guard for macOS.

## Known issues / next steps

- **Resolved: `URender::OccludeBsp` -O2 crash was a downstream symptom.**
  During the port, OccludeBsp crashed at -O2 on arm64 with fault addresses
  assembled from unrelated 32-bit halves (e.g. `0x707_00000010`), and a
  file-level `#pragma clang optimize off` was added to
  `Source/Render/Src/UnRender.cpp` as a workaround. The crashes predated the
  `CallFunction`/`EX_BeginFunction` stale-parameter-size fix — the script VM
  was corrupting memory, and the -O2 register allocation merely exposed it.
  After the VM fixes, the whole engine builds and runs correctly at -O2
  (verified 2026-07-11 with retail v200 and v205 demo content, ASLR enabled);
  the pragma has been removed. If a similar "impossible pointer" render crash
  ever reappears, suspect script-VM memory corruption first, not the renderer.
- **Debugging trap**: lldb disables ASLR by default, which keeps all pointers
  below 4 GB and *masks pointer-truncation bugs completely*. Always verify with
  a bare run, or `settings set target.disable-aslr false` in lldb.
- Audio is `SoundDrv.NullAudioSubsystem` (silent). `NOpenALDrv` needs OpenAL +
  libxmp (`brew install openal-soft libxmp` on macOS), then set
  `AudioDevice=NOpenALDrv.NOpenALAudioSubsystem`.
- macOS has no native GLES; use `NOpenGLDrv` (desktop GL, build with
  `-DBUILD_NOPENGLDRV=ON`) and set both `GameRenderDevice` and
  `WindowedRenderDevice` to `NOpenGLDrv.NOpenGLRenderDevice`.
- 32-bit builds must remain working (Android/PSVita targets). All layout rules
  above collapse to the original behavior when `sizeof(void*)==4`.

## Build & run (macOS arm64)

```sh
cmake -Bbuild -DBUILD_NOPENGLDRV=ON Source
cmake --build build -j8
```

Assemble a game dir from **v205 demo** assets (`System`, `Maps`, `Textures`,
`Sounds`, `Music`), then into its `System/`: copy `Engine/Config/*.ini`, all
built `*.dylib` and `Unreal.bin`, switch the renderer/audio config as above,
and run `./Unreal.bin`. `Unreal.log` in that directory is the primary
diagnostic — grep it for `Native class size mismatch` and `Critical:`.
A known-good runnable install exists at `Unreal205Demo/` (untracked).
