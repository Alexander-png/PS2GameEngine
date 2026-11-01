# Lessons from PS2Sandbox for the new engine (found 2026-10-16)
Reference code: /home/user/Projects/PS2Sandbox. Do not copy these problems:
- Text capacity mismatch: debug_overlay allocates 512 glyph/item slots and 32 lines, but its text buffer is 1024 bytes. Text over 512 glyphs makes text_rich_layout_build fail ("item capacity exceeded"), the partially built layout is still drawn (text cut mid-word). Size layout buffers from the text buffer size, or truncate cleanly and report once.
- Sticky dirty flag: debug_overlay_update only clears `dirty` after a successful refresh, so a failing refresh is retried every frame and logs two lines per frame (log spam, wasted CPU). Clear/latch the flag on failure and log once.
- Menu text rebuilt and re-laid-out every frame (intro_test_update_menu calls intro_test_apply_menu_text each update). Rebuild only when the selection changes.
- Test states needing two START presses (Memory, Memory Arena: summary, then exit) and a START sometimes lost right after other input: keep input handling consistent (one meaning for START) and make input edge detection robust.
- PCSX2 software renderer was used; other renderers crash the emulator with the PS2Sandbox ELF (per user). A single 41 FPS sample was seen in the Sprite Animation Test, cause unknown.
