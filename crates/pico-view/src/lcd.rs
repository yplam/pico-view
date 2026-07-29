//! Frame diffing and RGB565 packing.
//!
//! The worker uses this to turn each captured RGBA8888 frame into the small set
//! of changed [`Rect`]s to stream ([`dirty_rects`]) and to pack each region into
//! big-endian RGB565 ([`rgba_rect_to_rgb565_be`]) for the ESP32-P4 backend.

/// A rectangular region of the panel in panel-local pixel coordinates.
/// Used to drive partial updates: only the changed box is windowed and streamed.
#[derive(Debug, Clone, Copy)]
pub struct Rect {
    pub x: u32,
    pub y: u32,
    pub w: u32,
    pub h: u32,
}

impl Rect {
    /// The whole panel.
    pub fn full(width: u32, height: u32) -> Self {
        Rect { x: 0, y: 0, w: width, h: height }
    }
}

/// Dirty-detection tile size, in pixels. Changed pixels are tracked on a
/// `TILE_PX`×`TILE_PX` grid, then coalesced into a small set of rectangles.
/// Larger tiles mean a coarser grid (cheaper to scan/merge) but round each
/// changed region up to a larger box; 32 keeps the grid tiny (e.g. 8×9 cells for
/// a 240×280 panel) while bounding edge waste to ~one tile.
const TILE_PX: usize = 32;

/// Estimated per-rectangle fixed cost, in units of "one ~507-byte USB write".
///
/// Every windowed region carries a `BLIT` header (window rect + framing) before
/// any pixels move — a fixed overhead comparable in latency to ~24 pixel-data
/// writes. [`dirty_rects`] charges this against every extra rectangle so it only
/// splits a region when doing so avoids more pixel data than the split costs.
const RECT_FIXED_COST: usize = 24;

/// Detect the regions that differ between two tightly-packed RGBA8888 frames of
/// the same `width`×`height` and coalesce them into a small set of rectangles to
/// stream. Returns an empty vec when the frames are identical.
///
/// Changes are tracked on a [`TILE_PX`] grid, the dirty tiles are merged into
/// maximal rectangles, and the result is compared by estimated transfer cost
/// against the single changed bounding box — whichever is cheaper wins. So a
/// localized edit stays one tight box, two far-apart edits become two small
/// boxes instead of their full-screen union, and a frame that changes
/// everywhere collapses back to one full-panel rectangle.
///
/// The diff is in RGBA space, so it can occasionally flag a tile whose 16-bit
/// RGB565 is actually unchanged (quantization) — harmless, it only grows the
/// streamed area by at most a tile.
pub fn dirty_rects(prev: &[u8], cur: &[u8], width: u32, height: u32) -> Vec<Rect> {
    let w = width as usize;
    let h = height as usize;
    let stride = w * 4;
    let cols = w.div_ceil(TILE_PX);
    let rows = h.div_ceil(TILE_PX);

    let mut dirty = vec![false; cols * rows];
    let mut any = false;
    for y in 0..h {
        let row = y * stride;
        let prev_row = &prev[row..row + stride];
        let cur_row = &cur[row..row + stride];
        if prev_row == cur_row {
            continue;
        }
        let base = (y / TILE_PX) * cols;
        for tc in 0..cols {
            let cell = base + tc;
            if dirty[cell] {
                continue;
            }
            let b0 = tc * TILE_PX * 4;
            let b1 = (((tc + 1) * TILE_PX).min(w)) * 4;
            if prev_row[b0..b1] != cur_row[b0..b1] {
                dirty[cell] = true;
                any = true;
            }
        }
    }
    if !any {
        return Vec::new();
    }

    // Map a tile box (in tile units) to a pixel-space rect, clamped to the panel.
    let to_px = |tc: usize, tr: usize, tw: usize, th: usize| -> Rect {
        let x = tc * TILE_PX;
        let y = tr * TILE_PX;
        Rect {
            x: x as u32,
            y: y as u32,
            w: (((tc + tw) * TILE_PX).min(w) - x) as u32,
            h: (((tr + th) * TILE_PX).min(h) - y) as u32,
        }
    };

    // Greedy maximal-rectangle coalescing: grow each unconsumed dirty tile as far
    // right as it stays dirty, then as far down as that full-width span stays
    // dirty, consuming every tile it covers.
    let mut used = vec![false; cols * rows];
    let mut rects = Vec::new();
    let mut bbox: Option<(usize, usize, usize, usize)> = None; // (tc0,tr0,tc1,tr1) exclusive
    for tr in 0..rows {
        for tc in 0..cols {
            let i = tr * cols + tc;
            if !dirty[i] || used[i] {
                continue;
            }
            let mut tw = 1;
            while tc + tw < cols && dirty[i + tw] && !used[i + tw] {
                tw += 1;
            }
            let mut th = 1;
            'down: while tr + th < rows {
                let r = (tr + th) * cols + tc;
                for k in 0..tw {
                    if !dirty[r + k] || used[r + k] {
                        break 'down;
                    }
                }
                th += 1;
            }
            for dr in 0..th {
                let r = (tr + dr) * cols + tc;
                for dc in 0..tw {
                    used[r + dc] = true;
                }
            }
            rects.push(to_px(tc, tr, tw, th));
            bbox = Some(match bbox {
                None => (tc, tr, tc + tw, tr + th),
                Some((x0, y0, x1, y1)) => {
                    (x0.min(tc), y0.min(tr), x1.max(tc + tw), y1.max(tr + th))
                }
            });
        }
    }

    // Fall back to the single bounding box when fragmentation would cost more
    // than one combined window (the per-rect fixed overhead is steep).
    let (x0, y0, x1, y1) = bbox.expect("any => at least one rect");
    let bounding = to_px(x0, y0, x1 - x0, y1 - y0);
    if cost(&rects) <= cost(std::slice::from_ref(&bounding)) {
        rects
    } else {
        vec![bounding]
    }
}

/// Estimated transfer cost of one `w`×`h` window.
fn rect_cost(w: usize, h: usize) -> usize {
    RECT_FIXED_COST + (w * h * 2).div_ceil(507)
}

/// Estimated transfer cost of a rectangle set (sum of [`rect_cost`]).
fn cost(rects: &[Rect]) -> usize {
    rects.iter().map(|r| rect_cost(r.w as usize, r.h as usize)).sum()
}

/// Estimated transfer cost of streaming `rects`.
pub fn transfer_cost(rects: &[Rect]) -> usize {
    cost(rects)
}

/// Convert the `rect` sub-region of a tightly-packed RGBA8888 frame
/// (`full_width` pixels per row) into packed big-endian RGB565, row-major,
/// `rect.w * rect.h * 2` bytes — exactly what a windowed RAMWR expects. The byte
/// order matches what ST7789/ILI9341 latch at RAMWR.
pub fn rgba_rect_to_rgb565_be(rgba: &[u8], full_width: u32, rect: Rect) -> Vec<u8> {
    let fw = full_width as usize;
    let (rx, ry, rw, rh) = (rect.x as usize, rect.y as usize, rect.w as usize, rect.h as usize);
    let mut out = Vec::with_capacity(rw * rh * 2);
    for row in 0..rh {
        let base = ((ry + row) * fw + rx) * 4;
        for col in 0..rw {
            let i = base + col * 4;
            let r = rgba[i];
            let g = rgba[i + 1];
            let b = rgba[i + 2];
            let v = (((r as u16) & 0xF8) << 8) | (((g as u16) & 0xFC) << 3) | ((b as u16) >> 3);
            out.push((v >> 8) as u8);
            out.push(v as u8);
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    const W: u32 = 240;
    const H: u32 = 280;

    fn blank() -> Vec<u8> {
        vec![0u8; (W * H) as usize * 4]
    }

    /// Set one pixel to white so its tile reads dirty.
    fn touch(buf: &mut [u8], x: u32, y: u32) {
        touch_w(buf, W, x, y);
    }

    /// Width-aware variant for buffers that aren't the default `W` wide.
    fn touch_w(buf: &mut [u8], width: u32, x: u32, y: u32) {
        let i = ((y * width + x) * 4) as usize;
        buf[i..i + 4].copy_from_slice(&[0xFF, 0xFF, 0xFF, 0xFF]);
    }

    fn contains(r: &Rect, x: u32, y: u32) -> bool {
        x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h
    }

    #[test]
    fn identical_frames_are_empty() {
        let a = blank();
        let b = blank();
        assert!(dirty_rects(&a, &b, W, H).is_empty());
    }

    #[test]
    fn single_edit_is_one_tile_box() {
        let a = blank();
        let mut b = blank();
        touch(&mut b, 10, 10);
        let rects = dirty_rects(&a, &b, W, H);
        assert_eq!(rects.len(), 1);
        let r = rects[0];
        // One 32px tile at the origin, clamped to the panel.
        assert_eq!((r.x, r.y, r.w, r.h), (0, 0, TILE_PX as u32, TILE_PX as u32));
        assert!(contains(&r, 10, 10));
    }

    #[test]
    fn two_far_apart_edits_stay_split() {
        let a = blank();
        let mut b = blank();
        touch(&mut b, 10, 10);
        touch(&mut b, 200, 260);
        let rects = dirty_rects(&a, &b, W, H);
        // Two small windows, not their full-screen union.
        assert_eq!(rects.len(), 2);
        assert!(rects.iter().any(|r| contains(r, 10, 10)));
        assert!(rects.iter().any(|r| contains(r, 200, 260)));
        let covered: u32 = rects.iter().map(|r| r.w * r.h).sum();
        assert!(covered < W * H / 4, "split set should stay small, got {covered}px");
    }

    #[test]
    fn many_scattered_edits_fall_back_to_bounding_box() {
        // A grid of single-pixel edits across the whole panel fragments into so
        // many tiles that one combined window is cheaper — expect the fallback.
        let a = blank();
        let mut b = blank();
        for gy in 0..H / TILE_PX as u32 {
            for gx in 0..W / TILE_PX as u32 {
                touch(&mut b, gx * TILE_PX as u32 + 1, gy * TILE_PX as u32 + 1);
            }
        }
        let rects = dirty_rects(&a, &b, W, H);
        assert_eq!(rects.len(), 1);
    }

    #[test]
    fn full_change_is_one_full_rect() {
        let a = blank();
        let b = vec![0xFFu8; (W * H) as usize * 4];
        let rects = dirty_rects(&a, &b, W, H);
        assert_eq!(rects.len(), 1);
        assert_eq!((rects[0].x, rects[0].y, rects[0].w, rects[0].h), (0, 0, W, H));
    }

    #[test]
    fn adjacent_tiles_coalesce_into_one_rect() {
        // Two edits in horizontally neighbouring tiles merge into a single window.
        let a = blank();
        let mut b = blank();
        touch(&mut b, 10, 10);
        touch(&mut b, 40, 10); // next tile to the right
        let rects = dirty_rects(&a, &b, W, H);
        assert_eq!(rects.len(), 1);
        let r = rects[0];
        assert_eq!((r.x, r.y, r.h), (0, 0, TILE_PX as u32));
        assert_eq!(r.w, 2 * TILE_PX as u32);
    }
}
