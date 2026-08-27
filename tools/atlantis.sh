#!/bin/sh
# Run a REAL GL app on our driver: the classic GLUT "atlantis" demo (swimming sharks/
# dolphins/whales -- lit, depth-tested, double-buffered 3D).  Installs our driver, copies
# Apple's atlantis example, patches it to fullscreen game-mode + 16-bit depth (our HW Z),
# builds it, and runs it fullscreen so it routes through our accelerated renderer.
# Quit the demo with Esc.  Run on the Lombard:  sudo sh tools/atlantis.sh
DIR=`cd \`dirname "$0"\` && pwd`; ROOT=`cd "$DIR/.." && pwd`; B=Mach64Accelerator.kext
SRC=/Developer/Examples/OpenGL/GLUT/atlantis
[ -d "$SRC" ] || { echo "no atlantis sources at $SRC"; exit 1; }

# --- our driver: bundle built -DGLD_DBUF so it ADVERTISES double-buffering.  atlantis
# requests a double-buffered context; without a DB config our renderer doesn't match it and
# GLEngine falls back to the software "Generic" renderer (the DB page-flip path works now, so
# the old "renders black" caveat behind this flag no longer applies). ---
# APPLE_PROBE=1 additionally builds the Path B probe (-DGLD_APPLE_PROBE): the bundle ALSO
# registers+shapes a surface on Apple's ATIRagePro kext and binds it via set_surface, logging the
# result -- passive (our kext still does the rendering), to confirm the bind works driven by real
# GLEngine.  Leaves the driver log ON in that mode so the [apple-probe] lines are captured.
( cd "$ROOT/gldriver" && make clean >/dev/null 2>&1; \
  make CFLAGS="-O2 -Wall -std=gnu99 -I.. -DGLD_DBUF ${APPLE_PROBE:+-DGLD_APPLE_PROBE}" bundle ) || exit 1
DST=/System/Library/Extensions/ATIRageProGLDriver.bundle
rm -rf "$DST"; cp -R "$ROOT/gldriver/ATIRageProGLDriver.bundle" "$DST"; chown -R root:wheel "$DST"
# Rebuild the kext too (the DMA-chaining perf work changed it; atlantis.sh used to copy a
# prebuilt kext).  kextunload any stale copy first so the fresh one loads.
( cd "$ROOT/kext" && make kext ) || { echo "KEXT BUILD FAILED"; exit 1; }
kextunload /tmp/$B 2>/dev/null
rm -rf /tmp/$B; cp -R "$ROOT/kext/$B" /tmp/$B; chown -R root:wheel /tmp/$B; kextload /tmp/$B
rm -f /tmp/mach64gld.log

# --- atlantis: copy + patch (fullscreen game mode, 16-bit depth, print GL_RENDERER) ---
rm -rf /tmp/atlantis; cp -R "$SRC" /tmp/atlantis; cd /tmp/atlantis
sed -i.bak 's/fullscreen = GL_FALSE/fullscreen = GL_TRUE/' atlantis.c
sed -i.bak 's/depth=24/depth=16/' atlantis.c
sed -i.bak 's#glutEnterGameMode();#glutEnterGameMode(); fprintf(stderr,"GL_RENDERER=%s\\n",(const char*)glGetString(GL_RENDERER));#' atlantis.c
# GLUT game-mode size does NOT propagate as 1024x768 on our driver (type 0x50 / garbage
# descriptor), so atlantis's Reshape gets a tiny size -> tiny, wrong-aspect viewport in the
# center.  Print the size GLUT actually handed us, then force the real fullscreen 1024x768
# viewport + aspect so the scene fills the screen.  (Proper size propagation is part of the
# upcoming windowed/video-mode work.)
sed -i.bak 's#glViewport(0, 0, width, height);#fprintf(stderr,"RESHAPE %dx%d\\n",width,height); width=1024; height=768; glViewport(0, 0, width, height);#' atlantis.c
# FRAME PACING: the scene renders correctly but whips by at full HW framerate (no more 3.9Hz
# software cap).  Inject frame_pace(): caps to 60fps by DEFAULT (watchable) and prints the
# achieved FPS once/sec to stderr (so we can read the max rate).  Press 'f' in the demo to
# toggle the cap OFF (uncapped = max fps, prints "UNCAPPED") / ON (60fps).  (usleep is used
# without <unistd.h> -> an implicit-declaration warning on Panther gcc, harmless.  BSD sed on
# the Lombard does NOT treat \n as a newline, so the helper is one line, added after the last
# #include via a backslash-real-newline; \\n -> C's \n and \& -> literal &.)
sed -i.bak 's|#include "atlantis.h"|#include "atlantis.h"\
static int g_cap=1;static void frame_pace(void){static struct timeval pv={0,0},f0={0,0};static int fr=0;struct timeval nw;if(g_cap){gettimeofday(\&nw,0);if(pv.tv_sec){long u=(nw.tv_sec-pv.tv_sec)*1000000L+(nw.tv_usec-pv.tv_usec);if(u<16667)usleep(16667-u);}gettimeofday(\&pv,0);}gettimeofday(\&nw,0);if(!f0.tv_sec)f0=nw;fr++;{long u=(nw.tv_sec-f0.tv_sec)*1000000L+(nw.tv_usec-f0.tv_usec);if(u>=1000000){fprintf(stderr,"FPS %.1f (%s)\\n",fr*1e6/u,g_cap?"capped60":"UNCAPPED");fr=0;f0=nw;}}}|' atlantis.c
sed -i.bak 's|glutPostRedisplay();|frame_pace(); glutPostRedisplay();|' atlantis.c
sed -i.bak "s|case 't':|case 'f': g_cap=!g_cap; break; case 't':|" atlantis.c
# CLEAN EXIT: atlantis's Esc handler calls exit(1) directly, which kills the process before
# GLUT tears down the game-mode context -> our driver never gets the detach that flips the
# CRTC back to the desktop, so the last frame stays frozen on screen.  Leave game mode first
# so the context is destroyed (our gldAttachDrawable type-0 detach restores the scanout) and
# the WindowServer redraws the desktop.
sed -i.bak 's|exit(1);|glutLeaveGameMode(); exit(0);|' atlantis.c
cc -O2 -o /tmp/atlantis/atlantis atlantis.c dolphin.c shark.c whale.c swim.c \
    -framework GLUT -framework OpenGL -framework Foundation 2>&1 || { echo "BUILD FAILED"; kextunload /tmp/$B; exit 1; }
# In-session desktop-restore tool: capture/release forces the WindowServer to reprogram the
# CRTC our driver page-flipped and repaint the desktop.  Built + invoked HERE (this script
# runs in the console GUI session -- that's why game mode works), so it actually reaches the
# live display, unlike an ssh-launched process.  This is the RELIABLE teardown: the in-app
# glutLeaveGameMode restore is deferred by GLUT past exit() and never runs.
cc -O2 -o /tmp/restore_desktop "$ROOT/tools/restore_desktop.c" -framework ApplicationServices 2>&1 || \
    echo "WARN: restore_desktop build failed"

echo "=== running atlantis fullscreen (Esc to quit) ==="
# MACH64_GLD_NOLOG=1 disables the driver's per-line debug log so we measure the TRUE render fps.
# In APPLE_PROBE mode we leave the log ON so the [apple-probe] Path B lines are recorded.
if [ -n "$APPLE_PROBE" ]; then
    /tmp/atlantis/atlantis 2>&1 | tee "$ROOT/atlantis-out.txt"
else
    MACH64_GLD_NOLOG=1 /tmp/atlantis/atlantis 2>&1 | tee "$ROOT/atlantis-out.txt"
fi
[ -x /tmp/restore_desktop ] && /tmp/restore_desktop 2>&1   # restore desktop however the app exited
kextunload /tmp/$B
echo "=== FPS seen ==="; grep -E "FPS" "$ROOT/atlantis-out.txt" | tail -6
cp -f /tmp/mach64gld.log "$ROOT/atlantis-log.txt" 2>/dev/null
echo "=== Path B [apple-probe] result (APPLE_PROBE mode) ==="
grep -E "apple-probe" "$ROOT/atlantis-log.txt" 2>/dev/null | head -20
echo "--- done ---"
