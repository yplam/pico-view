//! Frame-buffer plumbing shared between the FFI producer and the worker.
//!
//! Three pieces, none of which touches device state: [`Frame`] (a captured
//! panel image), [`BufferPool`] (recycles the RGBA byte buffers so a repainting
//! UI stops churning ~0.5 MB allocations per frame), and [`FrameSlot`] (the
//! single-slot latest-frame mailbox that bounds the engine's memory to one
//! frame regardless of how far behind a busy worker falls). The worker in
//! `crate::worker` owns the reader side; `pv_lcd_flush` is the writer.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

/// How many retired frame buffers to keep for reuse. Two is enough to cover the
/// steady state (one in the slot, one held as `prev`) while bounding the pool at
/// roughly two panels' worth of RGBA — ~1 MB for a 360x360 panel.
const POOL_CAP: usize = 2;

/// A captured frame to push to the panel.
pub struct Frame {
    pub rgba: Vec<u8>,
    pub width: u32,
    pub height: u32,
}

/// Recycler for frame buffers.
///
/// Every captured frame used to be a fresh `Vec` (a ~0.5 MB allocation per frame,
/// ~30 MB/s at 60 fps) while the worker simultaneously dropped the identically
/// sized buffer it had been holding as `prev`. The pool closes that loop: the
/// worker hands retired buffers back and [`copy_of`](Self::copy_of) reuses one
/// instead of allocating.
pub struct BufferPool {
    free: Mutex<Vec<Vec<u8>>>,
}

impl BufferPool {
    pub(crate) fn new() -> Self {
        Self { free: Mutex::new(Vec::with_capacity(POOL_CAP)) }
    }

    /// Copy `src` into a recycled buffer, allocating only when the pool has
    /// nothing big enough.
    pub fn copy_of(&self, src: &[u8]) -> Vec<u8> {
        let mut buf = self.checkout(src.len());
        buf.extend_from_slice(src);
        buf
    }

    /// Hand a buffer back for reuse. Dropped when the pool is already full, so a
    /// burst can never grow the pool past [`POOL_CAP`].
    pub fn put(&self, buf: Vec<u8>) {
        if buf.capacity() == 0 {
            return;
        }
        let mut free = self.lock();
        if free.len() < POOL_CAP {
            free.push(buf);
        }
    }

    /// An empty buffer with capacity for at least `len` bytes. The lock is
    /// released before any allocation, so the only work under it is the search
    /// and a `swap_remove`.
    fn checkout(&self, len: usize) -> Vec<u8> {
        let recycled = {
            let mut free = self.lock();
            free.iter().position(|b| b.capacity() >= len).map(|i| free.swap_remove(i))
        };
        match recycled {
            Some(mut buf) => {
                buf.clear();
                buf
            }
            None => Vec::with_capacity(len),
        }
    }

    /// The pool must survive a panicking worker, so a poisoned lock is recovered
    /// rather than propagated.
    fn lock(&self) -> std::sync::MutexGuard<'_, Vec<Vec<u8>>> {
        self.free.lock().unwrap_or_else(|e| e.into_inner())
    }

    /// Number of pooled buffers. Test-only introspection so the recycling tests
    /// need not reach through the internal lock.
    #[cfg(test)]
    pub(crate) fn len(&self) -> usize {
        self.lock().len()
    }

    /// Drop every pooled buffer. Test-only.
    #[cfg(test)]
    pub(crate) fn clear(&self) {
        self.lock().clear();
    }
}

/// Single-slot latest-frame mailbox shared between the FFI thread (the writer,
/// `pv_lcd_flush`) and the worker (the reader).
///
/// It holds at most one frame: a newer frame overwrites an unread one. This is
/// what bounds the engine's memory — frames used to ride the command channel,
/// so while the worker was blocked (most acutely mid-OTA, for tens of seconds) a
/// repainting UI could pile up hundreds of ~0.5 MB `Frame`s behind it. With the
/// slot, only the newest frame is ever retained; superseded buffers go back to
/// the [`BufferPool`].
///
/// `pending` gates the wake-up signal so the command channel carries at most one
/// outstanding [`Cmd::Flush`](crate::worker::Cmd::Flush) regardless of frame
/// rate. The ordering is the classic dirty-flag pattern (write data → set flag;
/// clear flag → read data), so no frame is ever silently lost: [`put`](Self::put)
/// writes the slot before flipping the flag, and [`take`](Self::take) clears the
/// flag before reading the slot, so a frame that lands during a render still
/// re-signals.
pub struct FrameSlot {
    latest: Mutex<Option<Frame>>,
    pending: AtomicBool,
    pool: Arc<BufferPool>,
}

impl FrameSlot {
    pub(crate) fn new(pool: Arc<BufferPool>) -> Self {
        Self { latest: Mutex::new(None), pending: AtomicBool::new(false), pool }
    }

    /// Store `frame` as the latest, recycling any still-unread frame. Returns
    /// `true` when the caller must send a wake-up
    /// [`Cmd::Flush`](crate::worker::Cmd::Flush) (i.e. this call flipped the
    /// pending gate from clear to set); `false` when a flush signal is already
    /// outstanding and will collect this newer frame.
    pub fn put(&self, frame: Frame) -> bool {
        // Release the slot lock before touching the pool so the two locks are
        // never held at once, in any order.
        let superseded = self.lock().replace(frame);
        if let Some(old) = superseded {
            self.pool.put(old.rgba);
        }
        !self.pending.swap(true, Ordering::AcqRel)
    }

    /// Take the latest frame for rendering. Clears the pending gate *before*
    /// reading the slot so a frame that arrives mid-render re-signals rather than
    /// being stranded. `None` when the slot was empty (a spurious wake-up, e.g.
    /// after a frame was superseded — harmless).
    pub(crate) fn take(&self) -> Option<Frame> {
        self.pending.store(false, Ordering::Release);
        self.lock().take()
    }

    /// Discard any queued frame and clear the gate. Called when a session closes,
    /// so a frame captured against the old session is never rendered into a new
    /// one (and its buffer is recycled rather than held until the next capture).
    pub(crate) fn drain(&self) {
        if let Some(frame) = self.take() {
            self.pool.put(frame.rgba);
        }
    }

    /// See [`BufferPool::lock`] — recovered rather than propagated, because the
    /// slot holds at most one frame and a panic can't leave that inconsistent.
    fn lock(&self) -> std::sync::MutexGuard<'_, Option<Frame>> {
        self.latest.lock().unwrap_or_else(|e| e.into_inner())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    // --- frame slot ------------------------------------------------------------

    fn slot() -> (Arc<BufferPool>, FrameSlot) {
        let pool = Arc::new(BufferPool::new());
        (pool.clone(), FrameSlot::new(pool))
    }

    fn frame(width: u32) -> Frame {
        Frame { rgba: vec![0u8; (width * width * 4) as usize], width, height: width }
    }

    #[test]
    fn slot_signals_only_on_the_clear_to_set_edge() {
        let (_pool, slot) = slot();
        // First frame flips the gate 0->1: the writer must wake the worker.
        assert!(slot.put(frame(1)));
        // A second frame while a signal is still outstanding must NOT re-signal,
        // so the command channel never accrues a Flush per frame.
        assert!(!slot.put(frame(2)));
        assert!(!slot.put(frame(3)));
    }

    #[test]
    fn slot_keeps_only_the_newest_frame() {
        let (_pool, slot) = slot();
        slot.put(frame(1));
        slot.put(frame(2));
        slot.put(frame(3)); // supersedes the two unread frames
        let taken = slot.take().expect("a frame is waiting");
        assert_eq!(taken.width, 3);
        // Only one frame was ever retained; the slot is now empty.
        assert!(slot.take().is_none());
    }

    #[test]
    fn slot_re_signals_after_a_take() {
        let (_pool, slot) = slot();
        assert!(slot.put(frame(1)));
        assert!(slot.take().is_some());
        // Gate cleared by take(), so the next frame signals again (no lost wake-up).
        assert!(slot.put(frame(2)));
    }

    #[test]
    fn slot_take_clears_gate_before_read_so_mid_render_frames_re_signal() {
        // Models the worker taking a frame, then a new frame landing "during the
        // render": because take() clears the gate first, that frame re-signals.
        let (_pool, slot) = slot();
        slot.put(frame(1));
        let _first = slot.take(); // gate now clear, slot empty
        assert!(slot.put(frame(2)), "a frame arriving after take() must re-signal");
        assert_eq!(slot.take().expect("second frame").width, 2);
    }

    #[test]
    fn slot_recycles_superseded_frames() {
        let (pool, slot) = slot();
        slot.put(frame(4));
        assert_eq!(pool.len(), 0, "nothing retired yet");
        slot.put(frame(4)); // supersedes the first
        assert_eq!(pool.len(), 1, "the superseded buffer went back to the pool");
    }

    #[test]
    fn slot_drain_clears_the_gate_and_recycles() {
        let (pool, slot) = slot();
        slot.put(frame(4));
        slot.drain();
        assert!(slot.take().is_none(), "slot emptied");
        assert_eq!(pool.len(), 1, "drained buffer recycled");
        assert!(slot.put(frame(4)), "gate cleared, so the next frame re-signals");
    }

    // --- buffer pool -----------------------------------------------------------

    #[test]
    fn pool_reuses_a_returned_buffer_without_allocating() {
        let pool = BufferPool::new();
        let first = pool.copy_of(&[1u8; 64]);
        let addr = first.as_ptr();
        pool.put(first);
        let second = pool.copy_of(&[2u8; 64]);
        assert_eq!(second.as_ptr(), addr, "the same allocation came back");
        assert_eq!(second, vec![2u8; 64], "and holds the new contents, not the old");
    }

    #[test]
    fn pool_skips_buffers_that_are_too_small() {
        let pool = BufferPool::new();
        pool.put(Vec::with_capacity(8));
        // A panel change asks for more than the pooled buffer holds; reusing it
        // would just force a realloc, so it must be left alone.
        let big = pool.copy_of(&[0u8; 4096]);
        assert_eq!(big.len(), 4096);
        assert_eq!(pool.len(), 1, "the small buffer is still pooled");
    }

    #[test]
    fn pool_is_bounded() {
        let pool = BufferPool::new();
        for _ in 0..POOL_CAP + 5 {
            pool.put(Vec::with_capacity(64));
        }
        assert_eq!(pool.len(), POOL_CAP, "a burst cannot grow the pool");
    }

    #[test]
    fn pool_ignores_empty_buffers() {
        let pool = BufferPool::new();
        pool.put(Vec::new());
        assert_eq!(pool.len(), 0, "a zero-capacity buffer is worth nothing");
    }
}
