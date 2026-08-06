/* imports_r3_extra.c -- the three dynamic symbols The Room Three's libraries
 * import that the reference loader's tables do not yet resolve.
 *
 * Derived by diffing all 409 UND dynsyms across libmain/libunity/libil2cpp
 * against the loader's existing DynLibFunction tables: 406 were already covered.
 *
 * The Makefile globs every .c under source/, and imports.c concatenates
 * `game_extra_functions` into the combined table before so_resolve(), so this
 * file needs no wiring beyond existing.
 *
 * Note what is NOT here: the reference port's game-extra table carried
 * cosh/sinh/tanh/nearbyintf/environ. The Room Three imports none of those --
 * verified against its undefined-symbol table -- so they are gone.
 *
 * MIT, same as the rest of the loader.
 */

#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include "so_util.h"

/* diag.h doesn't declare it; jni_unimpl.h does, but pulling that in here would
 * drag the whole unimplemented-JNI slot table along. Declare it locally. */
int debugPrintf(char *text, ...);

/* ---------------------------------------------------------------------------
 * ALooper_pollAll  <- libunity.so
 *
 * Deprecated NDK alias for ALooper_pollOnce with "keep draining until something
 * that isn't a callback happens" semantics. android_native.c already implements
 * ALooper_pollOnce, so loop it.
 * ------------------------------------------------------------------------- */
extern int ALooper_pollOnce(int timeoutMillis, int *outFd, int *outEvents,
                            void **outData);

static int ALooper_pollAll_fake(int timeoutMillis, int *outFd, int *outEvents,
                                void **outData) {
  for (;;) {
    int r = ALooper_pollOnce(timeoutMillis, outFd, outEvents, outData);
    /* ALOOPER_POLL_CALLBACK == -2: a callback fired, nothing for the caller. */
    if (r != -2)
      return r;
  }
}

/* ---------------------------------------------------------------------------
 * socketpair  <- libunity.so
 *
 * Both call sites are inside LIBCURL, confirmed by name against the
 * version-matched reference: Curl_resolver_getaddrinfo (the async DNS
 * resolver's notification pair) and Curl_multi_handle (the multi-handle wakeup
 * socket). Neither is thread signalling and neither is the profiler, so nothing
 * in the engine's own machinery depends on this working.
 *
 * Fail cleanly. libcurl treats both as optional -- the wakeup socket is a
 * documented best-effort feature and the resolver falls back -- and both check
 * the return value, so -1 puts curl on its well-tested no-wakeup path.
 *
 * Deliberately NOT backed by fakefd_pipe. A pipe is half-duplex: curl expects
 * to write on either end, so a fake pair would accept the call and then fail
 * (or block) on the wrong direction, which is a hang instead of a clean
 * fallback. A working-looking socket that cannot carry a wakeup is worse than
 * no socket. The game has no network path on this platform anyway -- Play Games
 * and cloud save are both gated off in playgames_stub.c.
 * ------------------------------------------------------------------------- */
static int socketpair_fake(int domain, int type, int protocol, int sv[2]) {
  (void)domain; (void)type; (void)protocol;
  if (sv) { sv[0] = -1; sv[1] = -1; }
  static int once = 0;
  if (!once) { once = 1; debugPrintf("[shim] socketpair() -> EAFNOSUPPORT "
                                     "(libcurl wakeup/resolver; expected)\n"); }
  errno = EAFNOSUPPORT;
  return -1;
}

/* ---------------------------------------------------------------------------
 * tcflush  <- libil2cpp.so
 *
 * Pulled in by the BCL's terminal handling (Console / System.IO). There is no
 * tty here. The loader already provides tcgetattr/tcsetattr as no-ops; match
 * them.
 * ------------------------------------------------------------------------- */
static int tcflush_fake(int fd, int queue_selector) {
  (void)fd; (void)queue_selector;
  return 0;
}

/* ------------------------------------------------------------------------- */

DynLibFunction game_extra_functions[] = {
  { "ALooper_pollAll", (uintptr_t)&ALooper_pollAll_fake },
  { "socketpair",      (uintptr_t)&socketpair_fake      },
  { "tcflush",         (uintptr_t)&tcflush_fake         },
};

size_t game_extra_numfunctions =
    sizeof(game_extra_functions) / sizeof(*game_extra_functions);
