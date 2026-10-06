# AnyPS5 session handoff — 2026-10-06 (laptop → main PC)

Work now happens as **pull requests to `boykopovar/AnyPS5`** from the fork
`tv7/AnyPS5`, not as a private patch set. The old solo repo
`tv7/anyps5-patches` is superseded; its `HANDOFF.md` still has the title-level
state (Dreaming Sarah, Big Helmet Heroes, Trifox) but nothing about this flow.

This branch is an **orphan** holding only this file, so it can never be merged
into a PR branch. Nothing here is meant for upstream — the repo forbids notes
and investigation `.md` files in a PR.

---

## Why you are reading this on the main PC

The laptop has **Intel integrated graphics**. 29 of 358 ctest tests fail there,
all of them `agc_driver_*` shader-execution tests. The main PC has the NVIDIA
GPU, so it is the machine that can say whether they pass. **That is the one open
task.** Everything else is finished and pushed.

## State of the three PRs

| PR | Branch | Head | Mergeable | Needs |
|---|---|---|---|---|
| [#369](https://github.com/boykopovar/AnyPS5/pull/369) libSceJson2 | `feat/json2-value-access` | `fcd349fb` | yes | reply (drafted below) |
| [#696](https://github.com/boykopovar/AnyPS5/pull/696) libSceIme | `feat/ime-keyboard-resource-id` | `54f0e03c` | yes | reply (drafted below) |
| [#695](https://github.com/boykopovar/AnyPS5/pull/695) libSceAgcDriver | `feat/agc-submit-multi-dcbs` | `3e65449c` | yes | nothing; untouched |

All three are rebased on upstream `481c85e7` or merge cleanly into it. Both
rebases were done to the written instructions of `oneandonlydean`:

- **#369** — `Export.cpp` and `GuestJson.cpp` merged clean (the nesting limit and
  Cyberpunk exports are in the base now). Only `docs/dev/TechnicalDebt.md`
  conflicted: kept the four lines from main, **plus** the `sceNgs2VoiceControl
  sampler filter` line that landed with #778 after the reviewer note, then the
  `InitParameter2` line. The reviewer is handling the #407 overlap themselves —
  they asked #407 to drop `Initializer::initialize` and
  `InitParameter2::setFileBufferSize` since #369 implements both. Nothing to do.
- **#696** — resolved per the updated note, which **supersedes** the obvious
  reading. #759 added `core/libs/tests/GuestImeKeyboard.cpp` with its own
  `guest_ime_keyboard` target, and keyboard tests now live there — but the
  reviewer asked for this test to stay in `GuestImeParameters.cpp`. It did.
  Do not "tidy" it into the keyboard file without asking them first.
  `Export.cpp` is the version from main plus one constant
  (`ErrorConnectionFailed`) and the six-line body.

Upstream `main` moves roughly hourly; both PRs went from conflicting to
mergeable twice during one session. Rebase immediately before pushing, and
expect `docs/dev/TechnicalDebt.md` to be the only conflict most times, because
every PR appends to the end of the same two lists.

## Build on a fresh machine

```bash
git clone --recurse-submodules https://github.com/tv7/AnyPS5.git anyps5
cd anyps5 && git remote add upstream https://github.com/boykopovar/AnyPS5.git
export PATH="/c/winlibs/mingw64/bin:$PATH"     # winlibs 15.2.0posix-14.0.0-ucrt-r7
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_TLS_CAINFO="C:/Program Files/Git/usr/ssl/certs/ca-bundle.crt"
cmake --build build -j 8
ctest --test-dir build --output-on-failure --timeout 180 -j 4
```

**`CMAKE_TLS_CAINFO` is not optional on Windows.** Without it the configure step
dies at `3rdparty/ffmpeg-core`: the CMake that ships with winlibs has no CA
bundle, so the FFmpeg prebuilt download fails with *"SSL peer certificate or SSH
remote key was not OK"*. Git for Windows ships the bundle at the path above. Do
**not** reach for `CMAKE_TLS_VERIFY=0`.

## The open task: the 29 failures

On the laptop, both branches fail the **identical** 29 tests — confirmed by
diffing the two sorted failure lists, so the set does not move with either PR.
They are independent of both changes: the binaries link only `libSceAgcDriver`,
`libkernel` and `libc`, neither `libSceJson2` nor `libSceIme`. `agc_driver`
itself (test #50, the submission test that #695 extends) **passes**.

The failure mode is wrong values read back from the shader, e.g.
`AGC graphics: vopc compares: thread 0 compare 36 is 0, expected 1`, with the
device log reading `Selected GPU name=Intel(R) Graphics vendor=0x8086
device=0xa7ac subgroup=32`, `tessellationAvailable=1`.

```
agc_driver_buffer_atomics              agc_driver_f64_nan_conversions
agc_driver_buffer_atomics_float        agc_driver_f64_rounding
agc_driver_buffer_atomics_inc_dec64    agc_driver_f64_transcendental
agc_driver_buffer_atomics_or_swap64    agc_driver_float16_dot
agc_driver_division_output_modifiers   agc_driver_image_sample_derivatives
agc_driver_division_result_modifiers   agc_driver_lds_addtid
agc_driver_dpp16_inactive_source       agc_driver_lds_atomics64
agc_driver_dpp16_operands              agc_driver_permlane16
agc_driver_dpp16_row_share_xmask       agc_driver_sdwa_float_selectors
agc_driver_dpp8                        agc_driver_shader_clock
agc_driver_ds_permute                  agc_driver_vop1_float_unary
agc_driver_ds_swizzle                  agc_driver_vop3_mad_i64
agc_driver_f64_arithmetic              agc_driver_vopc_compare
agc_driver_f64_conversions             (+ agc_driver_image_sample_lod_clamp,
agc_driver_f64_division                    reported Skipped, not Failed)
agc_driver_f64_division_modifiers
```

On the NVIDIA box, for either PR branch:

```bash
ctest --test-dir build --output-on-failure -R "^agc_driver" --timeout 180 -j 4
```

Expected classes if it is purely the Intel iGPU: f64 arithmetic, DPP8/DPP16,
64-bit LDS and buffer atomics, `permlane16`, `ds_permute`/`ds_swizzle`,
`shader_clock` — all features an Intel iGPU implements differently or not at
all. **Anything still failing on NVIDIA is a real finding** and worth a separate
issue upstream; do not fold it into #369 or #696.

Two cautions from the handoff in `tv7/anyps5-patches` that still apply: cap `-j`
on a low-memory host (killed compilers show `FAILED: [code=1]` with no
diagnostic — that is memory, not code), and the `agc_driver` test is racy at
high ctest parallelism (`-j12` failed 3 of 4 runs there; `-j4` was used here).

## Drafted PR replies, not yet posted

Held back deliberately: they claim local results, and the honest version of
"Tested" depends on what the NVIDIA box says. Post them once the run above is
in, with the agc paragraph corrected to match. **Do not reuse the earlier
"288/288 passed" line** — the suite is 354–358 tests now and the laptop does not
produce a clean run.

### #369

> Rebased onto current `main` (`481c85e7`). `libSceJson2/Export.cpp` and
> `GuestJson.cpp` merged cleanly this time — the nesting limit and the Cyberpunk
> exports are in the base now. `docs/dev/TechnicalDebt.md` resolved as you
> described: the four lines from main kept, plus the `sceNgs2VoiceControl sampler
> filter` line that landed with #778 since your note, and the
> `sce::Json::InitParameter2` line after them.
>
> Thanks for taking the #407 overlap up there — nothing changed on this side for it.
>
> ### Tested
>
> Windows 11, MinGW-w64 GCC 15.2.0 (winlibs ucrt posix seh): full build, and
> `guest_json`, `param_json_parser` and `strict_nid_filter` pass.
> <!-- replace this paragraph with the NVIDIA result -->

### #696

> Rebased onto current `main` (`481c85e7`), resolved exactly as you set out:
>
> - `core/libs/prx/libSceIme/Export.cpp`: the version from main, with only
>   `ErrorConnectionFailed` added.
> - `core/libs/tests/GuestImeParameters.cpp`: the `sceImeClose`/`SetCaret`/
>   `SetText`/`SetTextGeometry` declarations and `CheckClosedPanel()` from main
>   kept, my keyboard declarations and `CheckKeyboardResourceIds()` added, both
>   called in `main()`.
> - `docs/dev/TechnicalDebt.md`: the lines from main at the end of the Functional
>   list, mine after them.
>
> One thing worth your call: #759 also added
> `core/libs/tests/GuestImeKeyboard.cpp` with its own `guest_ime_keyboard`
> target, which is now where the other keyboard tests live. I kept this test in
> `guest_ime_parameters` as your note says; say the word if you would rather it
> move next to the `GetInfo`/`SetMode` tests and I will shift it.
>
> ### Tested
>
> Windows 11, MinGW-w64 GCC 15.2.0 (winlibs ucrt posix seh): full build, and
> `guest_ime_parameters`, `guest_ime_keyboard` and `strict_nid_filter` pass.
> <!-- replace this paragraph with the NVIDIA result -->

Whether to keep the "worth your call" paragraph on #696 is still an open
question for the author — it was raised and not yet settled.
