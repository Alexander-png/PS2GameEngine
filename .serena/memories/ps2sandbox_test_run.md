# PS2Sandbox ELF run in PCSX2 via virtual pad (2026-10-16)
Driven with tools/vpad (vpad.py + shot.sh). All 9 menu states entered and observed; app left at main menu.
- Menu: UP/DOWN select, CROSS enter, L2 memory page, START quit app. Returning to the menu resets the cursor to the top (enter state N = N dpad-downs).
- START inside Memory Test and Memory Arena Test first shows a Summary screen, a second START returns to the menu. A START tapped right after other taps was sometimes dropped: verify with a screenshot before sending more input.
- Sprite Test: dpad moves yellow sprite ~235-240 px/s (verified). Sprite Animation Test: frame 3/6, sheet 354x139, one sampled run at 41 FPS / 69% speed (other states 55-63 FPS), cause unknown (software renderer or screenshot load).
- Resource Test: RESULT: PASS (raw -> release -> sprite_bank -> release, ~2.8 s). Memory Test: alloc/free, overrun, underrun, leak counters all 1 as designed. Memory Arena Test: basic, mark/release, reset, oom, alignment all 1, final used=0, peak=1016, capacity 1024.
- Audio Test plays assets/test/mus/tsoi_muzika_voln.wav (playing=1, vol 100/100). Audio Mix Test renders. Audio inputs not exercised beyond opening the screens.
- Possible bug seen in the terminal log: "text_block refresh failed: text_rich_layout_build failed" and "debug_overlay refresh failed" when screens have long text (block bounds 16,16,620,260); on-screen text is truncated mid-word at the bottom ("sprite_ba", "PRESS"). Overlay has 512 glyph slots / 32 lines / 1024-byte buffer; `dirty` is cleared only on a successful refresh, so a failing refresh retries every frame. Not fixed in PS2Sandbox (note for the new engine).
- Intro Test verified both languages: language menu (English / Русский / Exit) -> logo -> 10 slides with typewriter text and fades -> returns to the Debug Menu by itself after ~75 s. Cyrillic renders; RU set swaps slide 6 art. No overflow or errors.

## Text Test (22 cases, rerun with corrected Square/Triangle mapping)
Per case: R1 = next, TRIANGLE = reveal finish; sheets in .screenshots/textB_sheet1-3.png.
- Render correctly: demo_word (EN+RU, 10 wrapped lines), word_simple, word_cyrillic_mixed (wraps at space, long Cyrillic word stays on one line), rich_color_rgb/rgba (rgba is dimmer yellow), rich_shake_in_color, rich_adjacent_tags (RedGreenBlue), rich_unclosed_tag (colour runs to end), rich_reveal_words, rich_yellow_ok, rich_shake_ok, rich_shake_nested, rich_plain, rich_shake, rich_wave (EN+RU), rich_wave_shake.
- Malformed tags are shown as literal text: rich_mismatch shows "mismatch[/color]", rich_leading_close shows "[/color] leading close" (no crash).
- Suspicious frames after TRIANGLE (text cut off): long_mixed_char (only "Supercalifragilist", one line, though CHAR wrap should continue onto more lines), char_cyrillic_long (one line "ОченьДлинноеСловоБ"), rich_color_shake ("Green words and sha"). All three use GLYPH reveal; other GLYPH cases finished fine, so this may be reveal not finished at capture time (dropped TRIANGLE or timing) rather than a layout bug. NOT yet re-checked; re-run those three with a screenshot after a second TRIANGLE and a longer wait before concluding.
- Static screenshots cannot show shake/wave motion; these were only checked as present, not animated.
- Final frame (23rd) wrapped to demo_word and was captured before reveal finished ("Hello, world! This").
- Not exercised: SQUARE/L1 alignment, LEFT/RIGHT reveal speed, UP/DOWN wave scale, L2/R2 shake amplitude.

## Audio Test playback speed (recorded from PCSX2 output, 2026-10-16)
Measured vs on-screen speed: 0.33->0.333, 0.5->0.499, 0.75->0.750, 1.0->1.000, 1.25->1.250, 2.25->2.246, 3.0->3.001 (true resample, pitch follows speed; limits 1/3..3.0, step 0.25).
Streaming problem for the new engine: at 1.0-1.25x the mixer log is clean, but from about 2.25x the stream ring buffer is drained (`[audio:mixer] voice=0 underruns=N rb_size=8`, N grows ~10k/s) and at 3.0x about 14% of 10 ms frames in the recording are silent (audible dropouts); at 2.25x only 0.2% silent. Chunk loader (128 KB chunks, one request at a time) cannot feed the voice at high speed: use bigger/prefetched chunks or more than one chunk in flight. Audio Mix Test music speed not measured.

## Audio Mix Test (2026-10-16, recorded + log)
- Music (assets/test/mus/scary_music.wav) speed: 0.5 -> 0.501, 1.25 -> 1.249 measured. Volume is linear amplitude (UP/DOWN 0.1 steps, 100 -> 49 = about -6 dB). Music pan 0.59 -> L/R level -7.7 dB (pan works; centre = 0.0 dB); sfx1 (SQUARE, scary_yelling.wav, ~2.2 s) plays.
- BUG for the new engine: `s->music_volume = 80;` (audio_mix_test_state.c:146, meant 0.8): initial log shows `vol=8000/100`, DOWN gives 79.9 (still full volume via clamp in mixer), only UP resets it to 1.0.
- SFX2/SFX3/burst not verified: face buttons CIRCLE/R1/SQUARE were mostly dropped by the virtual pad in this state (hold 1-2 s, 1 of ~7 registered) while CROSS, L2 and the dpad worked. Cause unknown (VM load / PCSX2 input?).
