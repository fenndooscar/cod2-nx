# Measuring performance without fooling yourself

This page explains how to measure performance on the Switch in a way you can trust. Read it before you optimise
anything, and again whenever a result surprises you. None of it is specific to Need for Speed.

Most of the time lost during this port was not spent writing code, but acting on wrong conclusions. Around 190 builds
were measured on a Switch at stock clocks (its normal speeds, without overclocking). A good share of the early
analysis had to be thrown away, because a number did not mean what it seemed to mean. This page collects the methods
that held up and the mistakes to avoid.

## In short
- Measure on the console, at stock clocks. Numbers from the PC or from an overclocked console mislead.
- To compare two versions, alternate between them every few seconds inside the same session, and check the noise
  with a setting that does nothing.
- On the Switch, multiply the raw GPU timestamps of NVK by 1.627.
- Before you trust a counter, read the code that increments it.
- Look at the share of slow frames and at the range of [frame times](glossary.md#frame-time), not only at the
  average: the player feels the variation.
- A frame that is cut into many render passes is the first thing to suspect on a tile-based GPU, but "the passes"
  is not one thing: separate what each pass cost from why it ended and from what it was for, and then run the A/B
  that says whether the passes are the wall at all (see "Splitting a frame that is cut into render passes").

## Ground rules

- **Measure on the console, at stock clocks.** The PC build is useful to check that something works and to estimate
  CPU cost, but its numbers do not carry over. A draw (one drawing command sent to the GPU) that cost 1.9 µs of CPU on
  a desktop PC cost around 16 µs on the Switch, and the GPU bottleneck of the console did not exist on the PC.
  Overclocking hides the real bottlenecks: a build that ran at 34 FPS with an overclock ran at 15 without one, and the
  part that limited it changed from the CPU to the GPU.
- **Report every number with its machine and its log.** Numbers used in one calculation must come from the same log.
  An average from one session multiplied by a counter from another session gives a number that means nothing.
- **Divide by what really causes the cost.** A stage that is paid per index (each index says which vertex to use
  next) looks eight times more expensive "per draw" in one part of a race than in another, without any code change.
  Use nanoseconds per index, per triangle or per fragment (a pixel being shaded): whatever drives the work.
- **Weight samples by the CPU of the thread they came from, not by the number of samples.** The console profiler
  pauses and reads every thread, sleeping ones included, so an idle thread collects as many samples as a busy one. On
  the first Call of Duty 2 session all fifteen threads had 407 samples each, while their CPU ranged from 0.0 % to
  96.0 %: pooling the samples put `svcWaitProcessWideKeyAtomic`, `condvarWaitTimeout` and `errno_from_result` — the
  inside of a sleeping thread — at the top of the list, and the function that really held a core came out third. The
  per-thread header of each report carries its CPU, and that is the weight. `tools/calientes.py` does this.
- **A thread that is busy is not necessarily working.** Main XThread of Call of Duty 2 spends 92 % of its time on a
  single three-instruction loop inside `sub_823800D8`, spinning on a guest flag. That is 100 % of a core and none of
  it is progress. Before calling the CPU the bottleneck, read what the busy thread is executing.
- **Only compare the same kind of load.** A menu costs a fraction of what a level costs. Averaging a whole session
  inflated the frame rate by almost two frames per second. Use only the intervals of the level where the player is
  actually moving, and weight them by frames. When two sessions ran over different parts of the level, re-weight them
  by load (for example, by groups of draws per frame). `tools/perfil_consola.py` splits the reports into cutscene, menu
  and game for this reason: on the Call of Duty 2 sessions the same build measured 26.2 FPS in the menu, 3.3 in a level
  and 0.5 in a cutscene, and the three numbers describe three different programs.

## A/B tests that can be trusted

An A/B test compares two versions of the same thing: A and B. Two runs of a quick race are not comparable: the
track, the car and the AI change every time. Two captures taken at the same second of two runs differ in up to 92 %
of their pixels, without any code change. Removing the AI does not help either, because the physics and the attract
demo (the demo the game plays by itself) still diverge.

What worked:

1. **Alternate A and B every N seconds inside the same session.** Put the change under test behind a setting that
   can be changed while the game runs, and change it every 30 s for timing or every 2 s for image checks. Write the
   current value of the setting to the log at every change, so the analysis can sort every report by mode.
2. **Pause the race** for GPU timing and for checking that two images are the same. The scene freezes, half of the
   screen stops changing, and the noise drops enough to see differences of a few percent. Compare only the pixels
   that do not change within each mode.
3. **Measure the noise with a control that cannot change anything.** Run the same alternation with a setting that
   has no effect. In one test, the "no effect" build showed a 9.9 % median difference, because the start of the race
   fell into one of the modes. Skip the start and compare interval by interval.
4. **Change one thing at a time.** When three optimisations alternate at once, the slow changes of the session leak
   into categories that none of them touch.
5. **Never run two measured processes at once.** Two overlapping runs made everything look 60 % slower.

## GPU timestamps on NVK under Horizon

A GPU timestamp is a reading of the GPU's clock, written between two commands. The difference between two of them
tells how long the GPU work in between took.

- **Multiply by 1.627.** [NVK](glossary.md#nvk) reports `timestampPeriod = 1 ns`, but on the Switch one timestamp
  unit is **1.627 ns**. This was measured by comparing the time the timestamps covered with real (wall clock) time.
  Every raw GPU time has to be multiplied by 1.627. A frame that looked like 14.7 ms of GPU work was really 23.9 ms,
  and a whole theory about the CPU and the GPU waiting for each other came from reading the raw value.
- **Check every GPU measurement against the wall clock.** The sum of the categories plus the idle gap, times the
  scale, must equal the frame time. If it does not, the scale or the categories are wrong. But once the scale is
  calibrated from wall time, this check is true by construction and stops proving anything.
- **`TOP_OF_PIPE` blurs the boundaries between passes.** `TOP_OF_PIPE` and `BOTTOM_OF_PIPE` are two of the points of
  the GPU's work where a timestamp can be written. On NVK, `TOP_OF_PIPE` is written when the GPU reads the command,
  not when earlier work finishes. So times per pass taken with it give part of each pass to the next one.
  `BOTTOM_OF_PIPE` gives exact boundaries. With `BOTTOM_OF_PIPE` on both sides, a counter of "overlapping jobs" can
  never be above zero, which leads to the next section. **This does not spoil a whole-submission measurement**: on a
  single queue, submission N+1's commands are not read until N has finished, so a `TOP_OF_PIPE` at the very start of
  one and a `BOTTOM_OF_PIPE` at the very end of the same one bracket that submission's execution and nothing else.
  That is what the port does now, and it is why the pair is written into the submission itself rather than around it.
- **Where the port uses them.** `VulkanCommandProcessor::Cod2MarcarFinalDeEnvio` and `Cod2RecogerMarcas`
  (`sdk/src/graphics/vulkan/command_processor.cpp`) write the pair into every submission, into a rotating set of
  eight query slots, and read the finished ones back without ever blocking
  (`VK_QUERY_RESULT_WITH_AVAILABILITY_BIT`). The report prints the result as
  `gpu: N ms por fotograma ocupada, M % del tiempo de reloj`. `M` is the self-check: it is the GPU's busy time as a
  share of the wall clock, so it must come out below 100 %. A couple of points above it does not mean the scale is
  too large, though: the figure is one submission's span attributed to a presented frame, and a game running more
  than one submission per frame puts a frame boundary inside a span. The stronger check is the one on the same line,
  `si fuera el limite, el juego iria a N fps`: with the right scale it lands on the measured frame rate, and with a
  wrong one it is off by the same factor as the error. The scale is a cvar and not a constant because it is a
  property of the driver, not of the port.

## Counters that lie

1. **A wall-clock counter is not a cost.** "The ring thread is busy 97 % of the time" was really 76 % of CPU time
   plus 21 points of waiting. (The [ring thread](glossary.md#ring-thread) is the renderer thread that reads the
   game's GPU commands.) Always compare a thread's wall-clock time with its CPU time from a sampling profiler (a tool
   that looks many times per second at what each thread is doing). If they differ, the difference is waiting or
   preemption (the thread was paused so that another one could run), not work.
2. **One interval is not a series.** Quote the median of all intervals of the log, never one value that happens to
   support the argument.
3. **A "cost" that shrinks when frames get longer is slack.** A presentation stage that gets shorter when frames get
   longer measures the margin left before the vblank (the moment the screen starts its next refresh). It is spare
   time, not work.
4. **Check whether a counter is cumulative or per interval** before comparing it with another value. Cumulative
   means a total or average since the start. A cumulative average compared with a changing value gave a correlation
   of r = −0.22; done correctly it was +0.617.
5. **Counters with a limit hide data.** A stutter log limited to 200 entries filled up after 135 s of a 234 s
   session. So the second half of every race was invisible, and totals could not be compared.
6. **A [guard](glossary.md#guard) that counts zero because its code never ran proves nothing.** A safety counter read
   "0 conflicts" for five builds, because the feature was switched off in the settings file, so there was nothing to
   count. The first session where the code really ran showed one conflict per frame. Put an "attempts" counter next
   to every "failures" counter.
7. **A counter that cannot take any other value is not data.** If a counter reads the same value in every report,
   read the condition that increments it and ask whether it can ever be false.
8. **Comments that explain counters can be wrong.** A comment said a stage was divided by the draws that enter the
   ring. The increment was right after `vkCmdDraw`, so it counted recorded draws only, and every stage was inflated by
   40 %. Check against the line that increments the counter, and against the arithmetic of the log itself.
9. **A counter measures what its code does, not what its label says.** A report flagged two render targets as
   "nobody reads this". The counter only tracked copies back to the game's memory, and both targets were read as
   textures later in the frame. Removing them would have broken the image. A warning in a log is a hypothesis, not a
   conclusion.
10. **Count real actions, not calls.** A [hook](glossary.md#hook) ran on every call, but an unverified offset made
    it skip almost all of them. It looked active in the log and changed nothing. Pair "times called" with "times it
    acted".
11. **Stopwatches cost time.** Timing every ring packet and every draw stage meant about 50,000 clock reads per frame:
    close to a quarter of the ring thread's CPU on the PC. On busy code, time only a sample (today one in 128 packets,
    and one in 8 or 64 draws depending on the timer) and scale the result.

## Splitting a frame that is cut into render passes

This port ends about 140 render passes per frame for about 330 draws, and on a tile-based GPU every one of those
stores the attachments to memory and loads them back when the next one begins. That makes the passes the first
thing to suspect, but "the passes" is not one thing, and the counters have to separate them before a change is
chosen:

1. **What a pass cost.** Counter 35 (`area MB por fotograma`) is the attachment pixels the passes cover, four bytes
   each, colour only. Times the passes per frame it is the frame's floor of attachment traffic, which no change in
   shading can remove. The setting `cod2_area_de_dibujo_ajustada` is what moves it, because a render target image
   is as tall as the EDRAM allocation (2048 rows for a 1280-wide one) and not as tall as the image the guest draws
   into (720). A pass at the full allocation costs 10 MB of colour and 10 MB of depth, one at the guest's area 3.5
   and 3.5.
2. **Why a pass ended.** Counters 36-39, printed as `pases por motivo`. Two of the four reasons are the port's own
   work and not the guest's, and those are the ones a change in the port can remove without changing what the game
   draws. Counter 40 counts the guest's occlusion queries, each of which ends the pass twice for reasons that have
   nothing to do with the attachments.
3. **What a pass was for.** Counters 42-45, printed as `pases por clase`. This is the split that decides *who* can
   remove them: the `dibujados` half is the guest's drawing and only drawing less removes it, the `transferencias`
   half is the port copying render target contents out so they can be sampled as textures, which the Xbox 360's
   EDRAM made free. Summing the two and comparing with `pases/fot` also shows whether the frame leaves passes open
   at the end.
   **Why the count cannot simply be brought down.** With dynamic rendering, `vkCmdPipelineBarrier` must not be
   recorded inside a render pass instance unless `dynamicRenderingLocalRead` and the tile-image-read features are
   enabled (`VUID-vkCmdPipelineBarrier-None-09553` and `-09554`), so every batch of pending barriers has to be
   recorded between two pass instances. `VK_RENDERING_SUSPENDING_BIT`/`VK_RENDERING_RESUMING_BIT` would let the
   pass stay open across them and NVK implements it faithfully (it drops the linear-tile store and the resolve on
   suspend and skips the whole load and clear setup on resume:
   `mesa/mesa-switch/src/nouveau/vulkan/nvk_cmd_draw.c:1877`, `:1999`, `:2086`), but the flags are part of the
   *begin* info, so a pass would have to be suspended before the port knows whether the next begin can resume it;
   an abandoned suspension silently drops the store and the MSAA resolve. Counters 46-48 (`barrera por contenido`)
   say whether the barrier that ended each pass was the whole-buffer shared memory barrier, an image-only batch, or
   a scratch buffer, because only the first two are the port's own.
4. **The A/B that decides whether any of it is the wall.** Play the same spot twice in one session, once with
   `cod2_area_de_dibujo_ajustada` at its default and once with it `false`, and compare `area MB/f` and the frame
   rate. If turning the area off costs the predicted 2.8x of attachment traffic and the frame rate barely moves,
   the frame is not bandwidth-bound by the passes and the next change belongs somewhere else — which is the answer
   that matters, because it is the one that stops the next round from being another blind guess. Do not read the
   frame rate of one run alone: the game's own scene changes more than the change under test does.
5. **The same question, answered by removing the work instead of by counting it.** Holding **L+R+ZL** in game
   starts a rotation that removes one part of the GPU work per report, in this order: `normal`, `sin nada` (all of
   it), `sin dibujados` (no draws), `sin transferencias`, `sin resolves`, `sin cargas de texturas`, `sin efecto de
   presentacion`, `sin volcado de baldosas`, `sin barrera de memoria compartida`, `sin pixels (vista de 1 pixel)`,
   and then back to `normal`. Each report says which mode it was in, so the modes are measurements of the same spot,
   and the difference between `normal` and `sin nada` is the whole cost of the GPU work. The image is wrong while a
   mode lasts, which is expected. If `sin nada` is just as slow as `normal`, the cost is not in the work at all but
   in something fixed (submissions, the driver, the fence waits), and no amount of cutting draws or passes will help.
   **The last two modes are not work removal but hypothesis tests**, and they are the ones that decide whether a
   target frame time is reachable at all:
   - `sin barrera de memoria compartida` (bit 64) keeps every draw and removes only the `VK_WHOLE_SIZE` barrier that
     `VulkanSharedMemory::Use` pushes when the guest's use of the shared memory buffer changes between reading and
     writing — which the memexport-capable draws do on almost every draw, about 150 times a frame. Those barriers
     cannot be recorded inside a render pass begun with `vkCmdBeginRendering`
     (VUID-vkCmdPipelineBarrier2-None-09553/-09554), so each one ends the pass, stores the tile buffer, loads it back
     and drains the pipeline. If the frame time collapses in this mode, the port's own synchronisation is the wall;
     if it does not move, the draws cost what they cost.
   - `sin pixels (vista de 1 pixel)` (bit 128) keeps every draw with the same pipeline, the same barriers and the same
     render area, and only shrinks the viewport to one pixel, so the vertex shaders still run for every vertex and
     the rasteriser shades almost nothing. This separates a frame that costs what its pixels cost from one that costs
     what its draw calls cost — and therefore whether rendering at a lower resolution, the only lever that scales the
     guest's own shading, could ever reach the target.
   **The reports are five seconds long while the rotation is on** (ten when it is not), so a full cycle is 50 s of
   holding the buttons, and `sin nada` — the one report the whole measurement exists for — comes second rather than
   last. A five-second report is still tens of frames at these frame rates. The rotation always restarts at `normal`,
   so an interrupted session does not shift the sequence.
   What the modes do and do not remove: `RexSwitchPerfSkip` guards the *Vulkan call* at each site, not the work the
   port does to record it (`sdk/src/ui/switch_perf.cpp`, `RexSwitchPerfSkip`). `sin dibujados` therefore still
   reports ~164 draws per frame and `sin resolves` still runs the whole transfer and render-pass machinery. Each
   mode measures GPU work removed, not CPU work removed, which is what makes the comparison against `normal` clean.
6. **What is still missing: per-pass GPU time.** Nothing here says whether the frame is bound by the passes'
   bandwidth or by the pixels the draws shade; the scissor counter (counter 6) is the sum of every draw's scissor
   rectangle without clipping, so it is an upper bound of an upper bound. The *whole frame's* GPU busy time is now
   measured (item 8), which is enough to say whether the GPU is the wall at all, but not which pass is. That needs
   `vkCmdWriteTimestamp` at the pass boundaries, read back a frame later, with the scale of the section above — the
   parked native renderer (`app/src/aparcado/cod2_nativo_destinos.cpp`) has a working implementation to copy the
   plumbing from.
7. **The GPU half of the frame, timed directly, and the trap in reading it.** The `libnx por segundo` line separates
   `fences consultadas` from `fences esperadas`, and that is not a style choice: `nvFenceWait(fence, 0)` and
   `nvFenceWait(fence, timeout)` behave differently (`sdk/src/ui/switch_perf.cpp`, the `nvFenceWait` wrapper), and
   only the second blocks. So `esperadas ms ÷ fps` is the milliseconds per frame spent *blocked waiting for the
   GPU* — a direct measurement, and in the round-22 report it is `595.8 ÷ 4.3 = 138.6 ms` of a 232.6 ms frame, 59.6 %
   of the wall time. **The trap:** it is tempting to subtract it and call the rest the CPU half. Do not. The
   subtraction is circular — both terms come from the same `fps` — and it is wrong on the evidence: the remainder is
   94 ms in `normal` but only 30-36 ms in every skip mode, so it is not a fixed cost that survives the GPU getting
   faster, it is other GPU-related blocking plus CPU work. The `CPU total %` on the report's first line is not the
   CPU half either, because a thread blocked inside `nvFenceWait` still counts as running. The only way to find the
   floor under all the GPU work is to remove all of it at once (`sin nada`).
8. **How busy the GPU really was, which is the one thing the fence wait cannot say.** `esperadas` measures the guest
   blocking, and a guest blocks just as long when the GPU is the wall as when the two are taking turns and each one
   waits for the other — the two shapes are identical from the guest's side and need opposite fixes. The `gpu:` line
   is the direct answer: GPU-busy milliseconds per presented frame and as a share of the wall clock, from the
   timestamps of the section above. Read it as three cases. A share near the frame rate's own duty cycle with
   `si fuera el limite, el juego iria a N fps` close to the measured fps means the GPU work *is* the frame and the
   only way forward is less of it. A share well below that, with `N fps` far above the measured frame rate, means the
   frame is mostly waiting on something that is not GPU execution — a fence that is signalled late, a driver
   round trip, a guest thread — and cutting GPU work will not buy the frame rate it looks like it should. And a share
   far above 100 % means the scale is wrong, not that the GPU is overworked.
9. **The answer, and the budget it leaves.** The round-25 session ran this whole list and the reading is the first
   case. In game, `normal`, 5.9 fps: the GPU is busy **175.2 ms of a 169.5 ms frame** and `si fuera el limite` says
   5.7 fps, so the GPU work *is* the frame. `sin nada` — every category of GPU work removed, the 89.3 render pass ends
   and the ~277 barrier batches still recorded — runs at **29.0 fps with the GPU busy 24.5 ms of a 34.5 ms frame**,
   so everything the port does on its own, with nothing to draw, is 24.5 ms: removing all of the port's own overhead
   would take 5.9 fps to 6.9. The A/B deltas, in GPU milliseconds per frame, are the guest's own draws **87.4**,
   resolves 63.2, transfers 25.0, texture loads 20.8, the presentation effect 4.8. Against the 33.3 ms that 30 fps
   is, the guest's shading alone is 2.6× over budget before the port adds anything, and the only lever that scales
   it is rendering at a lower internal resolution — which `draw_resolution_scale` cannot express, being an integer
   from 1 up with `draw_util`, the render target cache and the scaled-resolve path all built on that. That is why
   the two hypothesis-test modes were added (item 5): if either of them moves the frame rate, this reading is wrong
   and there is a lever left; if neither does, the wall is the shading and the hardware.

## Verifying a change

- **Checking that a change was applied is not checking that it worked.** One build made NVK's command buffer memory
  go through the CPU cache, because of a neat argument about memory latency. The change was applied: cache
  maintenance traffic grew by exactly the predicted amount per draw. But the cost of recording did not move (the
  difference was 0.78 σ, which is noise). The reason: on the Switch's CPU, a Cortex-A57, writes to memory without
  cache are collected and merged by a write buffer. The latency in the argument belonged to reads and to device
  memory. If a theory predicts a large effect, give it a cheap test before spending a build on it.
- **Asynchronous system calls need a delayed check.** `apmSetPerformanceConfiguration` returns success before the
  clocks change, and later `pcv` puts back the memory clock of the active configuration (details in
  [platform-notes.md](platform-notes.md#clocks)). Check after a delay, keep a periodic check, and read the real table
  of configuration IDs instead of guessing neighbouring IDs by arithmetic.
- **Do not discard an idea with a measure that does not measure it.** A proposal to pin the ring thread to one core
  was first rejected by pointing at the load of each core of the whole system, which says nothing about threads
  moving between cores. Measured properly: the "preferred core" setting does not pin anything in Horizon (0.24 moves
  per ring loop wherever it is set), moves cost about 0.05 % of CPU, and an exclusive core mask does pin the thread
  but makes the worst frames worse (minimum 16.3 FPS against 21). The idea was closed, but with the right numbers.
- **When a measured section costs ten times what its code can cost, it is a bug.** For six builds, a recording stage
  showed 12-53 ms for about twenty Vulkan calls. Splitting it into four timers found the cause in one test: the
  swapchain was being recreated every frame (see [performance-history.md](performance-history.md)).

## Frame pacing: the mean is not what the player feels

*Frame pacing* is how evenly the frames reach the screen. The Switch compositor (the system part that puts the final
image on screen) runs at 60 Hz with FIFO presentation (see [platform-notes.md](platform-notes.md#presentation)), so
every frame is shown for a whole number of vblanks. Frame times are rounded up to steps of 16.67 ms (33.3, 50.0,
66.7 ms...). A game that averages 38 ms alternates between two and three vblanks.

- **A regular 38 ms frame feels better than one that jumps between 33 and 50.** One build gained 1.5 FPS on average
  with a presentation thread of its own, but made the range of frame times three times wider and more than doubled
  the frames above 50 ms. Players noticed the regression every time, before any measurement did. Always report the
  share of frames above 50 ms and the range, not only the average.
- **Reaching 33.3 ms on average does not remove stutter.** A simulation over 30,467 race frames (each frame shown for
  `ceil(T[i+1] / 16.67) − ceil(T[i] / 16.67)` vblanks) gave these deviations of the frame time the player sees:

  | Mean frame time | Deviation of what is shown | Frames that change cadence |
  |---|---|---|
  | 41.3 ms | 10.75 ms | 63.6 % |
  | 35.0 ms | 9.70 ms | 53.7 % |
  | 33.3 ms | 9.44 ms | 50.9 % |
  | 30.0 ms | 9.04 ms | 53.2 % |

  When the game makes frames as fast as it can (without waiting for the screen), and frame times vary by around 20 %
  from one frame to the next, no mean frame time avoids falling between two vblank steps. Two things together bring
  the deviation to zero:
  - frames that fit in about 31 ms (a median around 28 ms, so that 90 % of frames fit), **and**
  - a swap interval of 2, which shows each frame on every second vblank only.

  Either one alone leaves about 9 ms of deviation. A swap interval of 2 before frames fit makes things worse, because
  a late frame then costs 33.3 ms instead of 16.7.
- **More swapchain images do not help.** Adding swapchain images does not change the steps; it only shifts when the
  frames land.
- **Counting frames from a video can be wrong.** Video-based frame counting (for example with `mpdecimate`, an FFmpeg
  filter that drops repeated frames) undercounts in repetitive scenery, such as a street lined with identical fences.
  It reports pauses that the logs contradict.
