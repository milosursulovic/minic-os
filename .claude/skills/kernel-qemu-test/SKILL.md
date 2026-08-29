---
name: kernel-qemu-test
description: Boot minic-os's kernel.elf in QEMU and drive its interactive shell non-interactively (headless, via QEMU monitor sendkey) to verify a milestone with a real, checkable assertion - not just "it didn't crash". Use before writing any ad hoc QEMU command for this kernel, and before declaring any kernel change verified.
---

# minic-os QEMU test recipe

This kernel's shell only accepts real PS/2 keyboard input - there is no
serial-input path (`grep`ping for a serial-read/getc function in
`drivers/keyboard.c`/`shell/shell.c` confirms this). That means the serial
port is output-only, and driving the shell non-interactively requires QEMU
to actually deliver key events, not just receiving piped text on a pty.

## 1. Build

```bash
bash build.sh          # or: make
```

Confirm the rebuild actually happened before trusting a test result - a
stale `kernel.elf` from an earlier, unrelated build silently reproduces old
behavior and looks exactly like a real regression. Check the build output
for the `ld -m elf_i386 ... -o kernel.elf` line and make sure it lists the
`.o` for whatever you just changed.

## 2. Boot headless with a monitor socket

```bash
rm -f serial.log qemu-mon.sock
qemu-system-x86_64 -kernel kernel.elf -display none -serial file:serial.log \
  -monitor unix:qemu-mon.sock,server,nowait -drive file=disk.img,format=raw,if=ide \
  -no-reboot -daemonize
```

`disk.img` is only needed for `disk`/`diskwrite`/VFS-over-disk commands -
build it first with `make disk` if it doesn't exist yet. Every other command
works identically without `-drive` at all.

Wait ~2s, then confirm it's actually up:
```bash
ps aux | grep qemu-system | grep -v grep
tail -5 serial.log   # should already show boot output ending at the shell "> " prompt
```

## 3. Send shell commands via QEMU monitor `sendkey`

Send each keystroke as its own short-lived monitor connection - this
sidesteps keystroke word-splitting entirely (a real gotcha: piping a
literal multi-word string into an interactive session can silently eat
spaces or reorder characters under load). Letters/digits are their own key
name; `ret` is Enter.

```bash
for k in t e x t ret; do echo "sendkey $k" | socat - unix-connect:qemu-mon.sock >/dev/null; sleep 0.1; done
sleep 1
tail -10 serial.log
```

For a command containing a space (rare - e.g. `echo <text>`), send `spc` as
its own key in the sequence rather than assuming a literal space character
survives being typed elsewhere.

## 4. Read the assertion, don't just check "it printed something"

Every shell test command in `shell/shell.c` (`cmd_fb`, `cmd_win`,
`cmd_text`, `cmd_wincontent`, `cmd_textcontent`, etc.) prints named
hex-labeled values specifically so they can be compared against a
hand-computed expected value - e.g. a specific framebuffer pixel that must
equal a known foreground color, or must equal the untouched background.
Compute what each printed value *should* be before running the command, and
treat a mismatch as a real bug, not a rounding/formatting difference.

## 5. Known gotchas (each cost real debugging time to find)

- **`pkill` (or any command) that actually kills a running background
  process, when combined with further commands in the same shell
  invocation, has produced an opaque exit code (144) with zero output from
  the whole invocation** - the cause wasn't confirmed, but the workaround
  is reliable: run the kill as its own isolated command, never chained with
  build/launch/inspect steps in the same call.
- **QEMU/TCG's timer runs far faster than the nominal 100Hz PIT rate.**
  Comparing tick counts or wall-clock-style timing across *separate* shell
  commands (each with its own process-spawn latency) does not correspond to
  real elapsed guest time - don't use it to validate a `sleep_ticks()`-style
  duration.
- **A `screendump` taken only a couple seconds after boot can catch a
  newly-spawned ring3 window's `_start()` still mid-execution, or catch one
  redraw function's several *separate* `compositor_redraw()`-triggering
  syscalls half-applied** (each `window_fill_rect`/`window_draw_text` call
  independently blits, so a function that clears-then-redraws several text
  rows can be screendumped between two of those steps) - as the boot
  sequence has grown (more auto-spawned GUI apps, more round-robin tasks),
  this has produced apparent "this button's label never renders" or "this
  whole block is blank" bugs that were actually just "give it more real
  wall-clock time" (10-15s, not 2-5s) under QEMU/TCG's software emulation
  with no `-enable-kvm`. Before spending real effort disassembling code to
  chase a rendering bug, re-screendump after a longer wait first - and
  separately, try a full `make clean && make kernel.elf` rebuild, since a
  stale ring3-program build (its multi-step Makefile sub-pipeline appears
  less airtight against staleness than the main `C_OBJS` `.d`-tracked path)
  has also produced this exact symptom at least once.
- **QEMU monitor `mouse_button` (pressing a virtual PS/2 mouse button)
  permanently breaks `sendkey` keyboard delivery for the rest of that QEMU
  session** - confirmed by reproducing it twice: after `mouse_button 1`,
  every subsequent `sendkey` (even a single unrelated key) produces zero
  effect and zero echo in `serial.log`, while the timer/idle heartbeat
  keeps running (not a full VM hang - the CPU and other IRQs stay alive,
  just keyboard IRQ1 delivery specifically). `mouse_move` alone does NOT
  cause this. Root cause not found (candidate: mouse.c's IRQ handler not
  correctly EOI'ing the slave PIC, wedging IRQ1 behind it on the shared
  cascade) - real enough to be worth investigating as its own bug, but out
  of scope for whatever feature you're testing when you hit it. Workaround
  for testing a click: type the triggering command first, THEN fire
  `mouse_button` - do not plan to type anything else in that QEMU session
  afterward. **Instead of a follow-up shell command, use `screendump`
  (below) to verify the result** - it needs no guest keyboard input at all,
  so it sidesteps this bug entirely.
- **Command batching (many `sendkey`/monitor round trips issued too fast)
  can occasionally hang** - keep the `sleep 0.1` between keystrokes and a
  `sleep 1`+ pause after the final `ret` before reading `serial.log`.

## 6. Visual verification via `screendump` (no guest keyboard needed)

For GUI features (window/compositor/font/mouse work), a pixel-value
readback via a shell command is the primary assertion (see §4), but a real
screenshot is a strong secondary check - and the *only* way to verify
anything after a `mouse_button` press, since that kills further keyboard
input for the rest of the boot (§5). QEMU's monitor `screendump` captures
the display straight from its internal framebuffer state, entirely
independent of the guest's keyboard - it works even headless (`-display
none`) and even after the mouse_button bug has fired:

```bash
echo "screendump /absolute/path/out.ppm" | socat - unix-connect:qemu-mon.sock
```

`screendump` only writes PPM. Convert to something the Read tool displays
well:
```bash
python3 -c "from PIL import Image; Image.open('/absolute/path/out.ppm').save('/absolute/path/out.png')"
```
Then use the Read tool on the `.png` directly - it's a real screenshot, so
this catches things a handful of hand-picked pixel checks might miss (e.g.
confirms a whole window's layout looks right, not just 2-3 sampled points).
Delete the `.ppm`/`.png` when done - they're scratch verification output,
not deliverables.

## 7. Cleanup

```bash
pkill -f "qemu-system-x86_64.*kernel.elf"   # run this alone, not chained (see gotcha above)
```
```bash
rm -f serial.log qemu-mon.sock
```

Leftover QEMU processes or a stale monitor socket from a prior run will
make the next test's `ps aux`/`socat` connections ambiguous or fail
outright - always check for and clear stragglers before starting a new run.

## Delegating this

For a confirmation run (not active debugging), hand this off to the
`kernel-qemu-tester` agent rather than running it inline - give it the
exact shell command(s) to run and the expected hand-computed value(s) to
check against.
