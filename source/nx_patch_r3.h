/* nx_patch_r3.h -- engine patch offsets and feature switches for THE ROOM THREE
 *                  (Fireproof Games, Unity 2021.3.8f1, arm64 / IL2CPP)
 *
 * This header is the whole contract main.c compiles against. Every offset that
 * is filled in was DERIVED from this game's own binaries and confirmed by
 * disassembly; provenance is recorded per entry. Everything NOT derived is
 * switched OFF with its table left as a compiling placeholder, so nothing here
 * silently patches an address that was guessed.
 *
 * Runtime address = <module>.load_virtbase + offset. Both .so files link at
 * base 0, so link-time offsets are also RVAs.
 *
 * ALL PATCHES ARE APPLIED VERIFY-FIRST: main.c checks the stock word at each
 * site and skips + logs on mismatch rather than writing. That turns a stale
 * offset into a readable log line instead of a random crash.
 *
 * MIT, same as the rest of the loader.
 */
#ifndef NX_PATCH_R3_H
#define NX_PATCH_R3_H

#include <stdint.h>

typedef struct { uint32_t off, from, to; } NxPatchWord;

/* ===========================================================================
 * SECTION 0 -- feature switches
 *
 * ON  = derived for this build and confirmed.
 * OFF = not derived. Turning one on without deriving its offsets first does
 *       nothing useful -- verify-first will reject every site.
 * ======================================================================== */
#define R3_DISABLE_IL2CPP_GC     1   /* il2cpp_gc_disable() before first render  */
#define R3_HAVE_TIME_FIX         1   /* TimeManager::Update hook      -- DERIVED */

#define R3_HAVE_TIME_HOOKS       0   /* managed Time.get_* redirect    -- see §6 */
#define R3_HAVE_SCREEN_HOOKS     0   /* managed Screen.width/height    -- see §6 */
#define R3_HAVE_MANAGED_PROBE    0   /* periodic managed-state probe   -- see §6 */
#define R3_HAVE_RES_TRACE        0   /* Resources.Load tracing         -- see §6 */
#define R3_HAVE_FPS_OVERRIDE     1   /* frame-rate lock       -- DERIVED, §8 */
#define R3_HAVE_NRE_TRACE        0   /* NullReferenceException tracing -- see §6 */
#define R3_HAVE_SCENE_CENSUS     0   /* FindObjectsOfType census       -- see §6 */
#define R3_HAVE_BRANCH_FORCES    0   /* none needed for this build               */

/* ===========================================================================
 * SECTION 1 -- libunity: allocator region granularity 256MB -> 64MB.  DERIVED.
 *
 * Unity's memory manager encodes a pointer as (regionIndex << 28) | offset --
 * a 256MB region granularity. Switch homebrew cannot afford reservations that
 * large, so the shift is rewritten to 26 (64MB) everywhere it appears.
 * MMAP_ARENA_ALIGN in config.h MUST equal 64MB to match; the two are one
 * decision, not two.
 *
 * HOW THIS WAS DERIVED (tools/derive_alloc_table.py + tools/scope_alloc.py)
 * ------------------------------------------------------------------------
 * Earlier ports shipped this as a literal offset list transplanted between
 * builds at a uniform delta. That does NOT work here: only 15 of the reference
 * table's 21 stock words appear in this build at all, at no common delta,
 * because Unity 2021.3 register-allocates the allocator differently from
 * 2022.3. So the offsets were re-derived from scratch, in two stages.
 *
 * 1. LEARN THE RULES, REGISTER-INDEPENDENTLY. In every edit of this family --
 *
 *      mov  wD, #0x10000000              -> mov  wD, #0x4000000
 *      mov  wD, #0xfffffff               -> mov  wD, #0x3ffffff
 *      and  xD, xN, #0xfffffffff0000000  -> and  xD, xN, #0xfffffffffc000000
 *      lsr  xD, xN, #0x1c                -> lsr  xD, xN, #0x1a
 *      ubfx xD, xN, #0x1c, #W            -> ubfx xD, xN, #0x1a, #W
 *      mov  xD, #-0x1000000000           -> mov  xD, #-0x400000000
 *      movk xD, #0x1000, lsl #16         -> movk xD, #0x400, lsl #16
 *      sub  xD, xN, xM, lsl #28          -> sub  xD, xN, xM, lsl #26
 *
 *    -- (from ^ to) touches ONLY immediate-field bits: imm16 for the mov
 *    family, N/immr/imms for UBFM and logical-immediate, the shift amount for
 *    shifted-register. None of those ranges overlap Rd, Rn or Rm. So the rule
 *    transfers even though the encoding does not: match the reference pair with
 *    that class's register fields masked out, then to = from ^ delta.
 *
 * 2. SCOPE BY NAME, NOT BY ADDRESS. The rule family is not unique to the
 *    allocator -- it also matches mbedtls_sha512 byte extraction,
 *    double_conversion::Bignum, libtess, and UNET bandwidth math, 35 sites in
 *    total across this binary. Patching any of those would corrupt something
 *    unrelated. Attributing every hit in the symbol-bearing 2021.3.8f1
 *    reference gives the functions that legitimately carry it:
 *
 *      MemoryManager::VirtualAllocator::{GetMemoryBlockFromPointer,
 *          GetBlockInfoFromPointer, MarkMemoryBlocks, ReserveMemoryBlock}
 *      MemoryManager::{GetAllocatorContainingPtr, GetRequestedPointerSize}
 *      DynamicHeapAllocator::{DynamicHeapAllocator, RequestLargeAllocMemory}
 *      LocalLowLevelAllocator::ReserveMemoryBlock
 *      BucketAllocator::BucketAllocator
 *      AtomicPageAllocator::AllocatePage
 *      TLSAllocator<0|1>::ThreadInitialize
 *
 *    (Development-build unit-test suites excluded -- they do not exist here.)
 *    Eight of those were located in this stripped Release binary by offset-free
 *    run matching; the rest are inlined away in Release, but every one of their
 *    hits still lands inside the same memory-manager translation unit, which
 *    the located anchors bound to libunity+0x11c000..0x128000. Every rule hit
 *    inside that window is listed below; every hit outside it is rejected.
 *
 * ONE SITE WAS MISSED ON THE FIRST PASS AND IT CRASHED THE BOOT.
 * +0x1240cc `and x8, x1, #0xfffffffff0000000` -- a 256MB align-down sitting
 * between two sites that WERE patched, inside GetMemoryBlockFromPointer. The
 * classifier in derive_alloc_table.py picked the register-mask by the
 * instruction's TOP BYTE, and 0x92 is shared by MOVN (64-bit) and
 * AND-immediate (64-bit). AND-immediate was therefore treated as mov-family
 * and only Rd was masked out of the match key, leaving Rn in it -- so the rule
 * learned from `and x22, x9, #mask` did not match `and x8, x1, #mask`. Same
 * instruction, different register, silently skipped.
 *
 * The result: the allocator reserved 127MB at 0x2cf8000000, then aligned that
 * pointer down with the surviving 256MB mask to 0x2cf0000000 -- 128MB BELOW its
 * own reservation, in unmapped space -- and wrote a block header there. Data
 * abort, translation fault, on the first write (esr=92000046, far=2cf0000050),
 * inside libunity's init_array at ctor ~100/416.
 *
 * The classifier now keys on bits 28..23 (100101 = mov-wide, 100100 = logical
 * immediate, 100110 = bitfield). If this table is ever re-derived, check the
 * site count: 23, not 22.
 *
 * TWO SITES WERE CHECKED BY HAND because they were not inside a located
 * function:
 *   +0x11e780/4  `lsr x8,x1,#0x1c ; mov w9,#0x10000000 ; cmp x8,#0 ;
 *                 csel x8,x1,x9,eq` -- clamping a requested size to one region.
 *                 Region math, in scope.
 *   +0x126150    inside AtomicPageAllocator::AllocatePage, on the out-of-memory
 *                 path, feeding FormatBytes for "Page Allocator out of memory.
 *                 Maximum allowed memory: %s". Confirmed against the same site
 *                 in the reference (+0x4ee378, identical surrounding shape).
 *                 COSMETIC -- it is the printed value, not an enforced cap --
 *                 but after the patch the true maximum really is 64MB, so
 *                 rewriting it keeps the message honest.
 *
 * VERIFIED: all 22 stock words read back from the shipped libunity.so, all 22
 * patched words decode to the same mnemonic, and no patched word re-matches a
 * rule (so a double application cannot shift the granularity twice).
 * ======================================================================== */
__attribute__((unused))
static const NxPatchWord R3_PATCH_WORDS[] = {
  /*  0 */ { 0x11ded4, 0x12be0009, 0x12bf8009 },   /* mov w9, #0xfffffff                */
  /*  1 */ { 0x11dedc, 0x92648d36, 0x92669536 },   /* and x22, x9, #0xfffffffff0000000  */
  /*  2 */ { 0x11e780, 0xd35cfc28, 0xd35afc28 },   /* lsr x8, x1, #0x1c                 */
  /*  3 */ { 0x11e784, 0x52a20009, 0x52a08009 },   /* mov w9, #0x10000000               */
  /*  4 */ { 0x11fac8, 0x52a20009, 0x52a08009 },   /* mov w9, #0x10000000               */
  /*  5 */ { 0x121e18, 0xd35cfd6b, 0xd35afd6b },   /* lsr x11, x11, #0x1c               */
  /*  6 */ { 0x121e1c, 0x52a20009, 0x52a08009 },   /* mov w9, #0x10000000               */
  /*  7 */ { 0x1222cc, 0x12be000a, 0x12bf800a },   /* mov w10, #0xfffffff               */
  /*  8 */ { 0x1222d4, 0x92648d36, 0x92669536 },   /* and x22, x9, #0xfffffffff0000000  */
  /*  9 */ { 0x123ae4, 0xd35cfc33, 0xd35afc33 },   /* lsr x19, x1, #0x1c                */
  /* 10 */ { 0x123ae8, 0xd35cfd15, 0xd35afd15 },   /* lsr x21, x8, #0x1c                */
  /* 11 */ { 0x123be0, 0x52a20008, 0x52a08008 },   /* mov w8, #0x10000000               */
  /* 12 */ { 0x1240bc, 0xd35cfc2c, 0xd35afc2c },   /* lsr x12, x1, #0x1c                */
  /* 13 */ { 0x1240cc, 0x92648c28, 0x92669428 },   /* and x8, x1, #0xfffffffff0000000   */
  /* 14 */ { 0x1240d4, 0xd35c9c2a, 0xd35a942a },   /* ubfx x10, x1, #0x1c, #0xc         */
  /* 15 */ { 0x1240e8, 0xb25c6feb, 0xb25e77eb },   /* mov x11, #-0x1000000000           */
  /* 16 */ { 0x1240ec, 0xf2a2000b, 0xf2a0800b },   /* movk x11, #0x1000, lsl #16        */
  /* 17 */ { 0x124128, 0xcb0a7108, 0xcb0a6908 },   /* sub x8, x8, x10, lsl #28          */
  /* 18 */ { 0x124140, 0xd35cfc28, 0xd35afc28 },   /* lsr x8, x1, #0x1c                 */
  /* 19 */ { 0x124154, 0xd35c9c29, 0xd35a9429 },   /* ubfx x9, x1, #0x1c, #0xc          */
  /* 20 */ { 0x125d20, 0xd35cfc28, 0xd35afc28 },   /* lsr x8, x1, #0x1c                 */
  /* 21 */ { 0x125d3c, 0xd35c9e89, 0xd35a9689 },   /* ubfx x9, x20, #0x1c, #0xc         */
  /* 22 */ { 0x126150, 0x52a20000, 0x52a08000 },   /* mov w0, #0x10000000               */
};
#define R3_PATCH_WORDS_N ((int)(sizeof(R3_PATCH_WORDS)/sizeof(R3_PATCH_WORDS[0])))

/* ---- branch/word force sites -------------------------------------------
 * None needed for this build. The earlier lineage's single entry was a
 * BufferGLES BeginWrite caps gate, a workaround for a mesa bug fixed upstream;
 * it is not carried over. */
__attribute__((unused))
static const NxPatchWord R3_BRANCH_FORCES[] = {
  { 0, 0, 0 },  /* placeholder; unused while R3_HAVE_BRANCH_FORCES == 0 */
};
#define R3_BRANCH_FORCES_N (R3_HAVE_BRANCH_FORCES ? \
  ((int)(sizeof(R3_BRANCH_FORCES)/sizeof(R3_BRANCH_FORCES[0]))) : 0)

/* ===========================================================================
 * SECTION 2 -- libunity: frame pacing (Swappy) force-disable.  DERIVED.
 *
 * Swappy::IsEnabledAndActive() -- the cached "is frame pacing on?" getter.
 * Located by exact-matching its offset-free tail (cmp/cset/cmp/cset/and/ldp/ret)
 * from the version-matched 2021.3.8f1 reference: exactly ONE site in .text. The
 * body then verified instruction-for-instruction against that reference; only
 * the global page offsets differ (cached flag +0x701, value +0x700, second flag
 * +0x704).
 *
 * 13 call sites, 12 immediately followed by a tbz/cbz on w0 -- the same shape
 * the reference ports found. Forcing a 0 return makes every site take the
 * disabled path: plain eglSwapBuffers, no Choreographer-driven pacing threads,
 * no frame-0 join.
 * ======================================================================== */
#define R3_OFF_PACING_GETTER   0x30aa1c
#define R3_WORD_PACING_STOCK   0xA9BF7BF3u  /* stp x19,x30,[sp,#-0x10]! */
/* NOTE the register order. The reference port's constant is 0xA9BF4FFE
 * (stp x30,x19) -- the SAME instruction with the two registers swapped, so
 * it disassembles almost identically and reads as correct at a glance. This
 * build emits 0xA9BF7BF3. Copying the reference value here made verify-first
 * reject the patch every boot, silently leaving Swappy frame pacing ENABLED.
 * Re-read every stock word from the shipped libunity.so; never copy one. */

/* ===========================================================================
 * SECTION 3 -- libunity: audio.  DERIVED.
 *
 * (a) FMOD output type. Unity's internal FMOD defaults to the Java AudioTrack
 *     output, which needs a JVM run loop this loader does not have. The
 *     selection site is AudioManager::InitNormal:
 *
 *         bl   AndroidAudio::GetAndroidAudioOutputType
 *         cmp  w0, #2
 *         mov  w8, #0x15                 ; 21 = AudioTrack
 *         cinc w21, w8, eq               ; -> 21, or 22 = OpenSL
 *         ldr  x0, [x19, #0x158]
 *         mov  w1, w21                   ; <-- patch site
 *         bl   FMOD::System::setOutput
 *
 *     Forcing w1 to 22 makes FMOD drive its own self-scheduling OpenSL output,
 *     which opensles.c backs. NOTE: this build's selection is a two-way cinc,
 *     not the three-way csel the reference port's 2022.3 build had, so the
 *     constant was derived here rather than copied.
 *
 * (b) OpenSL buffer geometry. FMOD::OutputOpenSL::init validates the requested
 *     buffer against (bufferCount-1)*bufferSize, halves it once, then rechecks:
 *
 *         sub  w10, w20, #1 ; mul w10, w10, w21 ; cmp w9, w10
 *         b.ls +0xc                      ; first check
 *         lsr  w9, w9, #1                ; halve it
 *         str  w9, [x19, #0x3f8]
 *         cmp  w9, w10
 *         b.ls +0x10                     ; <-- TERMINAL check, patch site
 *         mov  x0, x19 ; bl close()      ; still too big -> bail out
 *
 *     Forcing the terminal b.ls to an unconditional b always takes the success
 *     path. The full 8-word signature matched the reference exactly, including
 *     the struct field offset -- FMOD is a vendored library, compiled
 *     identically in Development and Release builds, unlike Unity's own code.
 * ======================================================================== */
#define R3_OFF_FMOD_OUTPUT     0x3fdd08
#define R3_WORD_FMOD_STOCK     0x2A1503E1u  /* mov  w1, w21   */
#define R3_WORD_FMOD_OPENSL    0x528002C1u  /* movz w1, #22   */

#define R3_OFF_FMOD_BUFGEO     0x9aa1f0
#define R3_WORD_BUFGEO_STOCK   0x54000089u  /* b.ls +0x10 */
#define R3_WORD_BUFGEO_FORCE   0x14000004u  /* b    +0x10 */

/* ===========================================================================
 * SECTION 4 -- libil2cpp globals.  DERIVED.
 *
 * We do not call libil2cpp's JNI_OnLoad: its first action is an
 * __android_log_print through a GOT slot that is not safely bound at that
 * point. Its only essential effects are two global stores, which main.c
 * replicates.
 *
 * Recovered by disassembling THIS libil2cpp's JNI_OnLoad @ 0x45c3e0:
 *     adrp x8,#0x1b7d000 ; str x19,[x8,#0x610]   -> VM global  +0x1b7d610
 *     adrp x0,#0x45c000  ; add x0,x0,#0x424      -> handler fn +0x45c424
 *     bl 0x4bfaf8, whose entire body is
 *       { adrp x8,#0x1b7e000 ; str x0,[x8,#0x9a8] ; ret }
 *                                                -> handler ptr +0x1b7e9a8
 * Both globals land in .bss; the handler fn is in .text. Same three-instruction
 * registration shape as every other port in this lineage.
 * ======================================================================== */
#define R3_IL2CPP_VM_GLOBAL     0x1b7d610
#define R3_IL2CPP_HANDLER_PTR   0x1b7e9a8
#define R3_IL2CPP_HANDLER_FN    0x45c424

/* ---- il2cpp object layout (64-bit, version-independent) ------------------
 * Il2CppString : Il2CppObject{klass,monitor} = 0x10, then int32 length, chars.
 * Il2CppArray  : Il2CppObject + bounds ptr + uintptr max_length, then vector. */
#define R3_STR_LEN_OFF          0x10
#define R3_STR_CHARS_OFF        0x14
#define R3_IL2CPP_ARRAY_LEN     0x18
#define R3_IL2CPP_ARRAY_DATA    0x20

/* ===========================================================================
 * SECTION 5 -- libunity: engine clock.  DERIVED.
 *
 * On Switch the player loop parks UnityMain inside a synchronous scene load, so
 * TimeManager::Update stops being driven: newTime freezes, deltaTime collapses
 * to its 1e-5 floor, and Loading.PreloadManager -- which integrates load
 * progress over deltaTime -- starves. That is the frame-0 async-load hang. It
 * is a property of this loader architecture rather than anything
 * title-specific, and every port in this lineage carries the fix.
 *
 * main.c installs an entry hook that replays the prologue and re-drives the
 * body, plus a background clock thread that re-drives the body with a
 * wall-clock newTime whenever the main thread has been silent for >100ms.
 *
 * Derived twice independently: once by constant-scanning for the 1e-5 deltaTime
 * floor (0x3727c5ac), once by name against the version-matched 2021.3.8f1
 * reference. Both land on the same entry:
 *
 *   0x1bba28  ldr  x8,[x0,#0xc8] / ldr w9,[x0,#0xd0] / ldrb w10,[x0,#0xf8]
 *             add  x8,x8,#1      / add w9,w9,#1
 *             str  x8,[x0,#0xc8] / str w9,[x0,#0xd0]
 *             cbz  w10,+0x24     / ret          <- paused early-out
 *
 * The body at entry+0x24 opens `ldr d2,[x0,#0xe8]` / `fsub d2,d0,d2`,
 * confirming +0xe8 is the startup-reference double and that the body takes
 * newTime in d0, i.e. body(void *tm, double newTime).
 *
 * GetTimeManager() is reached from every UnityEngine.Time binding. Its body is
 * `str x30 / mov w0,#7 / bl GetSubsystem / ldr x30 / ret` -- subsystem index 7,
 * matching every reference build. The patch site is the `mov w0,#7` at entry+4,
 * so the function entry itself is 0x1bc0b8.
 * ======================================================================== */
#define R3_OFF_TIMEMGR_UPDATE_ENTRY  0x1bba28
#define R3_OFF_TIMEMGR_UPDATE_BODY   0x1bba4c   /* entry + 0x24               */
#define R3_OFF_GET_TIME_MANAGER      0x1bc0b8   /* FUNCTION ENTRY -- see below */
#define R3_WORD_TIMEMGR_STOCK        0xF9406408u /* ldr x8,[x0,#0xc8] */
#define R3_WORD_GETTIMEMGR_STOCK     0xF81F0FFEu /* str x30,[sp,#-0x10]! */
/* R3_OFF_GET_TIME_MANAGER MUST be the function entry, because main.c uses it
 * BOTH as the verify site AND as a call target:
 *     g_get_time_manager = (void *(*)(void))(ub + R3_OFF_GET_TIME_MANAGER);
 *
 * In the reference port those were the same address: its GetTimeManager was a
 * tail call, so the entry instruction WAS `mov w0,#7` (0x528000E0). This build
 * is not a tail call --
 *     0x1bc0b8  str x30,[sp,#-0x10]!   <- entry
 *     0x1bc0bc  mov w0,#7              <- the reference port's landmark
 *     0x1bc0c0  bl  GetSubsystem
 *     0x1bc0c4  ldr x30,[sp],#0x10
 *     0x1bc0c8  ret
 * -- so pointing the macro at the `mov` (which is what the reference's landmark
 * matches) would enter at +4, skip the push, then pop a garbage x30 off the
 * caller's frame and `ret` into it, with SP left 0x10 high. A wild jump on the
 * clock thread the moment the hook first ticks.
 *
 * The verify word therefore has to be the PROLOGUE, not the `mov`. */

/* ---- TimeManager field offsets (Release layout, this build) --------------
 * Read directly off the binding stubs in libunity. Informational -- main.c
 * drives Update rather than poking fields -- but useful when reading a dump.
 * NOTE a Development-build reference carries 0x18 extra bytes at the front of
 * TimeManager, so every offset there is +0x18 from these. Use THESE. */
#define R3_TM_FIXED_TIME          0x30   /* double */
#define R3_TM_FIXED_DELTA_TIME    0x48   /* float  */
#define R3_TM_TIME                0x90   /* double */
#define R3_TM_UNSCALED_TIME       0xa0   /* double */
#define R3_TM_DELTA_TIME          0xa8   /* float  */
#define R3_TM_UNSCALED_DELTA_TIME 0xac   /* float  */
#define R3_TM_SMOOTH_DELTA_TIME   0xb0   /* float  */
#define R3_TM_FRAME_COUNT         0xc8   /* int64, read as int32 by frameCount */
#define R3_TM_RENDERED_FRAMES     0xd0   /* uint32 */
#define R3_TM_PAUSE               0xf8   /* bool   */
#define R3_TM_TIME_SCALE          0xfc   /* float  */
#define R3_TM_MAXIMUM_DELTA_TIME  0x100  /* float  */

/* The libunity binding stubs themselves, for reference. Each is the same
 * 5-instruction Release form:
 *     str x30,[sp,#-0x10]! ; bl GetTimeManager ; ldr <reg>,[x0,#FIELD] ;
 *     ldr x30,[sp],#0x10 ; ret
 * Enumerated by finding all 129 callers of GetTimeManager and matching each
 * one's field load against the reference's named binding. These are NOT the
 * managed hook targets -- see §6.
 *   time 0x0e529c  timeAsDouble 0x0e52b4  deltaTime 0x0e52d4  fixedTime 0x0e52e8
 *   unscaledTime 0x0e5300  unscaledDeltaTime 0x0e5318  fixedDeltaTime 0x0e532c
 *   maximumDeltaTime 0x0e5360  smoothDeltaTime 0x0e5394  timeScale 0x0e53a8
 *   frameCount 0x0e53dc
 * (Unlike the reference port's build, IL2CPP did not strip smoothDeltaTime.) */

/* ===========================================================================
 * SECTION 6 -- managed-side hooks.  NOT DERIVED, ALL OFF.
 *
 * Everything in this section targets libil2cpp RVAs -- GAME code -- so no
 * reference port's values mean anything here; they must be derived per build.
 *
 * The reference derived them from a per-getter shape where each managed icall
 * stub embeds its own signature string:
 *     stp x30,x19,[sp,#-0x10]! ; adrp/ldr cache ; cbnz done
 *     adrp/add x0,"UnityEngine.Time::get_deltaTime()" ; bl resolve ; str cache
 *   done: ldp ; br x0
 *
 * THIS BUILD DOES NOT USE THAT SHAPE. The signature strings are present in
 * libil2cpp's .rodata -- get_deltaTime at 0x14ffe41, get_time at 0x14ffdf9,
 * get_frameCount at 0x1500007, get_realtimeSinceStartup at 0x150002a,
 * Screen::get_width at 0x14fa7b1 -- but nothing in .text reaches them by an
 * adrp/add pair, and no relocated data pointer targets them either. There are
 * only 8,187 ADRP instructions across 469,325 .text instructions, and zero ADDs
 * carrying the right imm12. Icall binding here is metadata-driven, so
 * recovering the stubs needs a different route than the reference used.
 *
 * None of this blocks booting. The TimeManager::Update fix in §5 is the
 * fundamental one and is independent of these -- the reference port's own notes
 * say so explicitly. What is off:
 *   TIME_HOOKS     managed Time.get_* agreeing exactly with the native clock
 *   SCREEN_HOOKS   managed Screen.width/height following dock state
 *   FPS_OVERRIDE   ignoring the game's Application.targetFrameRate
 *   RES_TRACE      logging Resources.Load calls
 *   MANAGED_PROBE  periodic camera/scene/isPlaying probe
 *   NRE_TRACE      managed stack trace on NullReferenceException
 *   SCENE_CENSUS   FindObjectsOfType census at chosen frames
 *
 * The placeholders below exist only so the file compiles with the switches off.
 * Do not turn a switch on until its RVAs are real.
 * ======================================================================== */

/* UNVERIFIED -- every constant in this section was copied from the reference
 * port and belongs to a feature that is OFF. They have NOT been read out of this
 * build (the pacing bug above is exactly what that costs). Re-read each one from
 * the shipped binary before enabling its feature.
 *
 * For reference, this build's libunity Time binding stubs open with
 * `str x30,[sp,#-0x10]!` = 0xF81F0FFE, not the value below -- but the hook list
 * targets libil2cpp stubs that this build does not emit in that shape at all
 * (see the note under SECTION 6), so neither value is usable as-is. */
#define R3_WORD_TIMEGET_STOCK   0xA9BF4FFEu  /* UNVERIFIED (copied) */
/* X(rva, clock_fn, name) -- clock_fn is bound in main.c's translation unit. */
#define R3_TIME_HOOK_LIST(X)     /* empty -- R3_HAVE_TIME_HOOKS == 0 */

/* X(rva, fn, name, verify_word) */
#define R3_SCREEN_HOOK_LIST(X)   /* empty -- R3_HAVE_SCREEN_HOOKS == 0 */

#define R3_PROBE_VERIFY_WORD    0xA9BF4FFEu
#define R3_PROBE_PERIOD_FRAMES  60
#define R3_PROBE_MAX_QUIET      600
/* X(rva, label, kind) */
#define R3_MANAGED_PROBE_LIST(X) /* empty -- R3_HAVE_MANAGED_PROBE == 0 */

#define R3_RES_LOAD_THUNK       0x0         /* UNDERIVED */
#define R3_RES_LOAD_CACHE       0x0         /* UNDERIVED */
#define R3_RES_LOAD_SIG         "UnityEngine.ResourcesAPIInternal::Load"
#define R3_WORD_RES_THUNK_STOCK 0xa9be57feu /* stp x30,x21,[sp,#-0x20]! */
#define R3_RES_TRACE_MAX        200

/* (The reference port's targetFrameRate icall-cache hook is not applicable here
 * -- see SECTION 8. These remain only so nothing references a missing macro.) */
#define R3_SET_TFR_THUNK        0x0         /* N/A for this build */
#define R3_SET_TFR_CACHE        0x0         /* N/A for this build */
#define R3_SET_TFR_SIG          "UnityEngine.Application::set_targetFrameRate"
#define R3_WORD_TFR_THUNK_STOCK 0xf81e0ffeu /* N/A for this build */

#define R3_NRE_RAISER           0x0         /* UNDERIVED */
#define R3_NRE_PATCH_LEN        0x10
#define R3_NRE_WORD_0           0xd10083ffu /* sub sp, sp, #0x20      */
#define R3_NRE_WORD_1           0xa900fbffu /* stp xzr, x30, [sp, #8] */
#define R3_NRE_WORD_2           0x910003e0u /* mov x0, sp             */
#define R3_NRE_WORD_3           0xf90003ffu /* str xzr, [sp]          */
#define R3_NRE_TRACE_MAX        12
#define R3_NRE_MAX_FRAMES       10

#define R3_FINDOBJECTS_RVA      0x0         /* UNDERIVED */
#define R3_WORD_FINDOBJ_STOCK   0xa9be57feu /* stp x30,x21,[sp,#-0x20]! */
#define R3_OBJ_GET_NAME_RVA     0x0         /* UNDERIVED */
#define R3_WORD_GETNAME_STOCK   0xa9be57feu /* stp x30,x21,[sp,#-0x20]! */
#define R3_CENSUS_FRAMES        { 2, 6, 120, 360 }
/* X(namespace, class, label) */
#define R3_SCENE_CENSUS_LIST(X)  /* empty -- R3_HAVE_SCENE_CENSUS == 0 */

/* ===========================================================================
 * SECTION 8 -- libil2cpp: frame-rate lock.  DERIVED.
 *
 * The game does not use Application.targetFrameRate. It picks
 * QualitySettings.vSyncCount from one bool in its own settings model,
 * RecommendedPlatformSettings.runAt60 (+0x29), inside
 * PlatformSettings.ApplyRecommendedSettings (+0x5BA040):
 *
 *   +0x5ba230  mov  w9,#0x1e ; str w9,[sp,#0x28]   default 30 (the logged value)
 *   +0x5ba258  ldrb w8,[x8,#0x29]                  runAt60
 *   +0x5ba25c  cbz  w8,#0x5ba270                   <-- PATCH SITE
 *   +0x5ba260  mov  w8,#0x3c / mov w0,#1           60, vSyncCount = 1
 *   +0x5ba270  mov  w0,#2                          vSyncCount = 2  -> 30 fps
 *
 * That ldrb is the ONLY read of runAt60 in the entire binary (checked across
 * ApplyRecommendedSettings and GenerateRecommendedSettings), so rewriting the
 * branch is equivalent to setting the field -- nothing else can disagree.
 *
 * Both directions are single words, so config.framerate works either way:
 *   nop            always fall through to the 60 path
 *   b #+0x14       always jump to the 30 path (needed if a device map or a
 *                  PlatformSettings.xml ever sets runAt60 = true)
 *
 * The 30 branch distance is (0x5ba270 - 0x5ba25c) / 4 = 5.
 * ======================================================================== */
#define R3_OFF_FPS_RUNAT60_BRANCH 0x5ba25c
#define R3_WORD_FPS_BRANCH_STOCK  0x340000A8u  /* cbz w8, #+0x14 */
#define R3_WORD_FPS_FORCE60       0xD503201Fu  /* nop            */
#define R3_WORD_FPS_FORCE30       0x14000005u  /* b   #+0x14     */

/* ===========================================================================
 * SECTION 7 -- libil2cpp: Boehm GC stop-the-world bridge.  DERIVED.
 *
 * The collector stops the world by pthread_kill-ing every other thread with a
 * suspend signal; each target's handler sem_posts an ack and parks. Switch
 * delivers no POSIX signals, so without help the collector waits forever.
 *
 * TWO MUTUALLY EXCLUSIVE OPTIONS:
 *
 *  (a) R3_DISABLE_IL2CPP_GC -- call il2cpp_gc_disable() after surface setup and
 *      BEFORE the first nativeRender. It is an exported dynsym here, so resolve
 *      it BY NAME (so_find_addr); the offset below is for reference only. Cost:
 *      managed memory is never reclaimed, acceptable for a session-length
 *      puzzle game.
 *
 *      Ordering matters. Calling it during GC INIT deadlocks -- it blocks on a
 *      lock held by a helper thread that has not started yet. After surface
 *      setup and before the first render is the safe window.
 *
 *  (b) the pthread_kill bridge in libc_shim.c, using the globals below.
 *
 * DO NOT DO BOTH. Disabling the collector behind a live bridge leaves the
 * bridge waiting on an ack that never comes.
 *
 * Derivation: libil2cpp has exactly TWO call sites to pthread_kill --
 * GC_suspend_all (0x506da4) and GC_start_world (0x507008). Resolving the
 * adrp/ldr chain feeding the second argument at each gives the two signal
 * globals; they are adjacent, and the word immediately below them is the
 * restart ack gate, read or written at five sites inside the same
 * stop-the-world code. The ack semaphore is whatever sem_init is handed -- one
 * global, also posted at two sites. Sections check out: three ints in .data,
 * the semaphore in .bss.
 * ======================================================================== */
#define R3_IL2CPP_GC_DISABLE_SYM  "il2cpp_gc_disable"  /* resolve by NAME */
#define R3_OFF_IL2CPP_GC_DISABLE  0x4e9b88             /* reference only  */

#define R3_GC_START_ACK_OFF   0x1b75858  /* .data -- restart ack gate      */
#define R3_GC_SUSPEND_SIG_OFF 0x1b7585c  /* .data -- GC_suspend_all  sig   */
#define R3_GC_RESTART_SIG_OFF 0x1b75860  /* .data -- GC_start_world  sig   */
#define R3_GC_ACK_SEM_OFF     0x1b82380  /* .bss  -- suspend-ack semaphore */

/* call sites, for logging / breakpoints */
#define R3_GC_SUSPEND_ALL_KILL 0x506da4
#define R3_GC_START_WORLD_KILL 0x507008

#endif /* NX_PATCH_R3_H */
