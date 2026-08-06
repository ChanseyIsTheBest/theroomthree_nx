/* r3_slot_probe.c -- read-only probe of the game's SlotManager.
 *
 * WHY
 * ---
 * On SELECT PROFILE the slot rows and the Google buttons never appear, while the
 * title and the caption render normally. Tracing that screen in libil2cpp:
 *
 *   SelectSlotScreen.AreSlotsEnabled()            libil2cpp + 0x5A78C0
 *       -> returns the BYTE at SlotManager.Instance + 0x35
 *          which dump.cs names `private bool SlotDataLoaded`
 *
 *   SlotManager.LoadAllSlotData()                 libil2cpp + 0x65FCB8
 *       -> writes that byte in exactly ONE place, +0x65FF8C, at the very end,
 *          immediately before storing LastTimeSlotsLoaded.
 *
 * The log proves LoadAllSlotData starts (it prints "SlotManager.LoadAllSlotData()"
 * and then five "DeserialisePlayerSlot: file does not exist" lines, which is the
 * correct first-run result). What the log CANNOT show is whether it reached that
 * final store -- and that single byte is the whole difference between a usable
 * profile screen and the one on screen now.
 *
 * So read it directly. Nothing here writes to game memory.
 *
 * HOW THE ADDRESS IS REACHED
 * --------------------------
 * Straight out of the AreSlotsEnabled disassembly:
 *
 *   adrp x20, #0x1a5e000 ; ldr x20, [x20, #0x908]   -> Il2CppClass* SlotManager
 *   ldr  x8, [x20]                                   -> (class ptr indirection)
 *   ldr  x8, [x8, #0xb8]                             -> klass->static_fields
 *   ldr  x8, [x8]                                    -> static field #0 = Instance
 *   ldrb w0, [x8, #0x35]                             -> SlotDataLoaded
 *
 * Field offsets below are dump.cs's, cross-checked against that code path.
 *
 * This file is diagnostic and title-specific. Delete it once the profile screen
 * works; it is not part of the loader.
 */

#include <stdint.h>
#include <string.h>

#include "so_util.h"

int debugPrintf(char *text, ...);

extern so_module il2cpp_mod;

/* --- addresses derived from THIS build's libil2cpp.so ------------------- */
#define SLOTMGR_CLASS_PTR_SLOT   0x1a5e908   /* global holding Il2CppClass*    */
#define SOCIAL_CLASS_PTR_SLOT    0x1a3e9f8   /* SocialPlatformManager           */
#define SPM_IS_SIGNING_IN        0x18        /* bool IsSigningIn                */
#define IL2CPP_KLASS_STATICS     0xb8        /* klass->static_fields           */

/* SlotManager instance layout (dump.cs, TypeDefIndex 1269) */
#define SM_INSTANCE              0x00        /* static SlotManager Instance    */
#define SM_SLOTS                 0x20        /* PlayerSlot[] Slots             */
#define SM_SLOT_ERROR_STATUS     0x28        /* eSlotErrorStatus[]             */
#define SM_ACTIVE_SLOT_INDEX     0x30        /* int                            */
#define SM_LOAD_REQUESTED        0x34        /* bool SlotDataLoadRequested     */
#define SM_LOADED                0x35        /* bool SlotDataLoaded  <-- THIS  */
#define SM_LAST_TIME_LOADED      0x38        /* float LastTimeSlotsLoaded      */
#define SM_CLOUD_JUST_ENABLED    0x3c        /* bool                           */

/* il2cpp array header: {klass, monitor, bounds, max_length, vector[]} */
#define IL2CPP_ARRAY_MAX_LENGTH  0x18
#define IL2CPP_ARRAY_VECTOR      0x20

/* A pointer we are willing to dereference. The heap and the module both live
 * well above this; anything smaller is a null or a small integer that has been
 * mistaken for a pointer, and dereferencing it would turn a diagnostic into a
 * crash. */
static int sane_ptr(const void *p) {
  uintptr_t v = (uintptr_t)p;
  return v > 0x10000u && (v & 7u) == 0u;
}

/* Call once per frame; it rate-limits itself and goes quiet once the answer
 * stops changing, so it costs nothing after the first couple of seconds. */
void r3_probe_slots(int frame) {
  if (!il2cpp_mod.load_virtbase) return;
  if (frame < 60 || (frame % 60) != 0) return;

  static int last_loaded = -1, last_requested = -1, quiet = 0;
  if (quiet) return;

  uintptr_t base = (uintptr_t)il2cpp_mod.load_virtbase;

  void *klass = *(void **)(base + SLOTMGR_CLASS_PTR_SLOT);
  if (!sane_ptr(klass)) {
    debugPrintf("[slot] frame %d: SlotManager class not yet initialised\n", frame);
    return;
  }
  void *inner = *(void **)klass;
  if (!sane_ptr(inner)) return;
  void *statics = *(void **)((char *)inner + IL2CPP_KLASS_STATICS);
  if (!sane_ptr(statics)) return;
  void *inst = *(void **)((char *)statics + SM_INSTANCE);
  if (!sane_ptr(inst)) {
    debugPrintf("[slot] frame %d: SlotManager.Instance is null\n", frame);
    return;
  }

  /* SocialPlatformManager.IsSigningIn -- the OTHER gate, and the one that was
   * actually stuck. SelectSlotScreen.UpdateButtons (libil2cpp+0x5A6EF0) reads
   * this bool and, while it is true, calls SetActive(false) on every entry of
   * SlotButtons[]. Confirmed two ways: GetIsSigningIn() at +0xD037A8 is literally
   * `ldrb w0,[x0,#0x18]; ret`, and Awake() at +0xD033BC resolves its class
   * through the same global (0x1a3e9f8) that UpdateButtons uses. */
  int signing = -1;
  {
    void *sk = *(void **)(base + SOCIAL_CLASS_PTR_SLOT);
    if (sane_ptr(sk)) {
      void *si = *(void **)sk;
      if (sane_ptr(si)) {
        void *ss = *(void **)((char *)si + IL2CPP_KLASS_STATICS);
        if (sane_ptr(ss)) {
          void *spm = *(void **)ss;
          if (sane_ptr(spm)) signing = ((const unsigned char *)spm)[SPM_IS_SIGNING_IN];
        }
      }
    }
  }

  const unsigned char *o = (const unsigned char *)inst;
  int loaded    = o[SM_LOADED];
  int requested = o[SM_LOAD_REQUESTED];
  int active    = *(const int32_t *)(o + SM_ACTIVE_SLOT_INDEX);
  float lastt   = *(const float *)(o + SM_LAST_TIME_LOADED);

  void *slots = *(void **)(o + SM_SLOTS);
  long nslots = -1;
  int  nonnull = 0;
  if (sane_ptr(slots)) {
    nslots = (long)*(const uintptr_t *)((char *)slots + IL2CPP_ARRAY_MAX_LENGTH);
    if (nslots < 0 || nslots > 64) nslots = -1;       /* not an array after all */
    for (long i = 0; i < nslots; i++) {
      void *e = *(void **)((char *)slots + IL2CPP_ARRAY_VECTOR + i * 8);
      if (e) nonnull++;
    }
  }

  static int last_signing = -2;
  if (loaded == last_loaded && requested == last_requested && signing == last_signing) {
    /* Same answer three checks running: say so once, then stop. */
    static int same = 0;
    if (++same >= 3) {
      quiet = 1;
      debugPrintf("[slot] settled: SlotDataLoaded=%d IsSigningIn=%d -- %s\n",
                  loaded, signing,
                  (loaded && signing == 0) ? "buttons SHOULD be visible"
                  : signing ? "UpdateButtons is hiding every button because "
                              "SocialPlatformManager.IsSigningIn is still true "
                              "(the sign-in callback never reached the game)"
                            : "SlotDataLoaded is false; LoadAllSlotData did not finish");
    }
    return;
  }
  last_loaded = loaded; last_requested = requested; last_signing = signing;

  debugPrintf("[slot] frame %d: SlotDataLoaded=%d LoadRequested=%d IsSigningIn=%d "
              "ActiveIndex=%d LastTimeLoaded=%.2f Slots[%ld] non-null=%d\n",
              frame, loaded, requested, signing, active, (double)lastt, nslots, nonnull);
}
