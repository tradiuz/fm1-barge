# Barge's AC79 engineering rules

The FM-1 runs a JieLi AC791N with JieLi's own clang (pi32v2). It is fast enough for a lot of synth, but it
has habits. These are the ones that cost the SLOOP / sloopDX / zp12 people time; the build catches most of them.

## Single precision is available; `double` is not

- The FM-1's JieLi AC791N has a **single-precision FPU**. JieLi pi32v2 clang needs `-mcpu=r3 -mfprev1`; without
  these options it emits software helpers such as `__mulsf3` / `__divsf3`, which are unavailable in Barge's link.
  Barge adds these flags to the target application build. This correction is documented in
  [Dinghy issue #1](https://github.com/zvenson/dinghy/issues/1), including a clang 4.0.1 compile/link check.
- **Use `float` where it helps**, especially when an engine or effect naturally uses coefficients, filter state,
  or algorithms that become clearer and safer in single precision. It is not a blanket recommendation: keep the
  audio callback bounded and measure worst-case target time. Use fixed point where it is faster, bit-exact output
  matters, or integer state is a better fit (Q15 levels, Q12 gains, 32-bit oscillator phases).
- **No hardware `double`.** It pulls in software helpers; do not use `double` in target DSP or code reachable from it.
- **No 64-bit integer division.** `int64_t` multiplication is fine; division/modulo needs `__udivdi3`, which is
  unavailable. Shift, use bounded 32-bit arithmetic, or precompute constants instead.
- Use `-ffp-contract=off` when comparing host and target floating-point renders if reproducible non-fused
  multiply/add behavior is wanted. The FPU/toolchain may otherwise contract operations differently from a host.
- **A division by zero does not crash.** It gives a wrong value (the trap is off, as SLOOP has it, because the
  compiler may compute a division ahead of the test that guards it). Test your divisors.
- Compute constants on your computer and write them into the source as numbers (see `INC_C9` in barge.c).

## The build checks

`./build.sh` fails, with the reason, when:

- the image, the RAM or the pool is too big;
- code calls or loads an address in the chip ROM (0xFFC00000..0xFFCFFFFF). A constant that happens to look
  like one counts too: `-32767 << 7` is 0xFFC00080, whatever you meant by it. Change the arithmetic so that value
  never appears as a constant (clamp to another range, or shift after the clamp instead of before);
- code outside `hal/` touches a hardware register (a raw address, a `volatile` pointer cast, inline asm): put
  a helper in `hal/` and call it;
- code that runs while the flash is busy (`.ram_text`, the flash driver) calls anything.

## The audio interrupt

`app_audio()` has 5.8 ms for 256 frames, and less in practice: the USB and the key scan interrupt it. When it
runs late the sound crackles; when it never ends the watchdog resets the FM-1.

- Do the work per frame that has to be per frame. Envelopes, filters' coefficients, LFOs: once per block of
  16 or 32 frames is plenty.
- Measure: `fm1_ticks()` before and after, divided by `FM1_TICKS_PER_US`. Under half the budget (2.9 ms) leaves
  room.
- No flash, no screen, no `platform_poll()`, no waiting in it.
- Share with the main loop through `volatile` variables, one writer each. A 32-bit store is atomic; a struct
  is not: write the parts in an order that is safe to see half-done (barge.c sets the pitch before the gate).

## The main loop

- Call `platform_poll()` at least every few ms (USB) and never less than every 8 s (watchdog).
- Draw only what changed: a full screen is 115 KB over SPI.
- A flash erase stops everything for tens of milliseconds, the sound too. Save when silent.

## When it crashes

A fault shows a red screen with three numbers: the vector, the **PC** (where it happened) and the return
address. Look the PC up in `build/barge.dis` (the disassembly the build writes). Then it restarts.

If it crashes during boot, twice, the FM-1 starts in the USB rescue by itself, where the updater still works:
install a fixed build. You can always force the rescue: hold **OCT-** while switching on.

If even that is gone (the app is broken before it gets to its boot guard), the FM-1's own boot ROM still
updates over USB: [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) gets it back.
