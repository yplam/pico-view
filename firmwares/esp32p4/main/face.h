// Idle "face": when no USB host is driving frames, render a looping facial
// animation to the panel instead of a black screen -- the same emote engine the
// Espressif speaker/robot demos use (espressif2022/esp_emote_expression), fed
// straight into our QSPI panel via pv_panel_blit.
//
// The emote engine owns its OWN render task, whose flush callback routes each
// stripe through the panel task (panel_task.h) -- the sole esp_lcd owner -- as
// a synchronous RPC, so face and host blits are serialised by construction.
// pv_face_stop() is still called (from rx_task) before host BLIT/CONFIG traffic
// resumes, but that is about reclaiming the screen contents, not SPI safety.
// See the idle/reconnect wiring in main.c.
#pragma once
#include <stdbool.h>
#include "esp_err.h"

// True if a face asset pack is present: the `assets` partition exists (a unit
// flashed before the repartition, or with an unwritten pack, has none). When
// false, callers fall back to blanking the panel as before.
bool pv_face_available(void);

// Bring up the emote engine and start the idle face animation. If the host never
// configured the panel (boot-idle), configures it with the board default first;
// mounts the asset pack; shows the idle emoji; lights the backlight. Returns
// ESP_OK on success. On any failure everything is torn back down and the caller
// should blank instead. rx_task only (panel ownership).
esp_err_t pv_face_start(void);

// Stop the face and fully tear down the emote engine, joining its render task so
// no further panel access can come from it. Idempotent. Call from rx_task before
// it drives any host frame. Leaves the last face frame lit for a seamless handoff.
void pv_face_stop(void);

// Advance the idle "personality": while a face is up, this wanders among a few
// friendly expressions and throws in the occasional wink so the bot looks alive
// rather than frozen on one grin. Cheap and self-throttled -- it only switches
// expression when its own internal timer is due, so it is safe (and intended) to
// call every rx_task idle iteration. No-op when no face is showing. rx_task only
// (it mutates the emote handle, which is rx_task-owned).
void pv_face_tick(void);
