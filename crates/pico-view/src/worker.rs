//! The permanent supervisor thread that owns the display backend.
//!
//! All device work is serialized on one thread. The FFI layer holds a cheap
//! [`Engine`] handle and communicates only by sending [`Cmd`]s down a channel —
//! there is no shared mutable device state and so no lock on any FFI path.
//!
//! The thread is spawned once (on first use) and lives for the process. Opening
//! and closing a device are *commands*, not thread lifecycle events, which is
//! what removes the lock: because `Open` and `Close` are serialized in the same
//! queue as everything else, "is a device open" has exactly one owner — the
//! supervisor — and races like double-open or open-during-close cannot be
//! expressed. Commands that need an answer carry a [`Reply`] and the caller
//! blocks on it with a deadline, so a busy worker (an in-flight OTA occupies it
//! for tens of seconds) degrades to a timeout instead of an unbounded stall.
//!
//! Within a session the worker diffs each frame against the last, packs the
//! changed regions to big-endian RGB565, and hands them to the ESP32-P4
//! [`Transport`] over USB vendor-bulk. Between commands it ticks the transport on
//! a fixed cadence.

use crate::config::PicoViewConfig;
use crate::frame::{BufferPool, Frame, FrameSlot};
use crate::lcd::{self, Rect};
use crate::panels::{PanelShape, PanelSpec};
use crate::proto::ffi::LinkState;
use crate::proto::wire::{self, Haptics, OtaState};
use crate::transport::{DeviceIdentity, OpenError, PanelGeom, Transport};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{Receiver, RecvTimeoutError, Sender, SyncSender};
use std::sync::Arc;
use std::time::{Duration, Instant};

/// Touch poll cadence (also the idle `recv` timeout while a session is open).
const POLL_INTERVAL: Duration = Duration::from_millis(15);

/// How long touch polling must fail *continuously* before the device is treated
/// as unplugged and the worker starts trying to reopen it. Comfortably longer
/// than the occasional single-tick I2C error seen during fast touches, so a live
/// device is never torn down by bus noise.
const LINK_LOST_AFTER: Duration = Duration::from_millis(500);

/// How often to attempt reopening the device while it is disconnected.
const RECONNECT_INTERVAL: Duration = Duration::from_secs(1);

/// After this many consecutive failed reopen attempts, force a USB bus reset to
/// kick a device that a bare re-open can't recover (halted endpoint, or a
/// firmware framer left mid-frame by the teardown). At ~1 reopen/sec this is a
/// few seconds of "plugged in but not answering" before we escalate — long
/// enough not to fight an ordinary unplug/replug, short enough to self-heal a
/// wedged link without user intervention.
const RESET_AFTER_FAILS: u32 = 5;

/// How often to send a liveness [`Transport::keepalive`] while connected. The
/// device blanks its panel after several seconds of host silence, so a static UI
/// (which emits no frames) must still tick this well inside that window; 1s
/// leaves ample margin against a couple of dropped writes.
const KEEPALIVE_INTERVAL: Duration = Duration::from_secs(1);

/// Deadline for [`Cmd::Open`]. Generous: it covers USB enumeration, interface
/// claim, the HELLO handshake and full panel init.
pub const OPEN_TIMEOUT: Duration = Duration::from_secs(10);

/// Deadline for [`Cmd::Close`]. The `cancel` flag is set before the command is
/// sent, so even a close landing behind an in-flight OTA unwinds well inside
/// this.
pub const CLOSE_TIMEOUT: Duration = Duration::from_secs(5);

/// Deadline for [`Cmd::GetDeviceInfo`]. Short: the device round-trip itself is
/// sub-second, so exceeding this means the worker is busy elsewhere and the
/// caller is better off being told than blocked.
pub const DEVICE_INFO_TIMEOUT: Duration = Duration::from_secs(2);

/// One-shot answer channel for a request/reply [`Cmd`].
///
/// A `sync_channel(1)` rather than an unbounded one so the worker's send can
/// never block, and dropping the `Reply` without sending (which is what happens
/// if the worker panics mid-command) disconnects the receiver — surfacing to the
/// caller as [`ReqError::Panicked`] instead of a hang.
pub struct Reply<T>(SyncSender<T>);

impl<T> Reply<T> {
    /// Answer the waiting caller. A caller that already timed out and dropped the
    /// receiver makes this a silent no-op, which is correct — the answer is just
    /// discarded.
    pub fn send(self, value: T) {
        let _ = self.0.send(value);
    }
}

fn reply_channel<T>() -> (Reply<T>, Receiver<T>) {
    let (tx, rx) = std::sync::mpsc::sync_channel(1);
    (Reply(tx), rx)
}

/// Why a command could not be carried out, as decided by the supervisor (which
/// is the sole owner of session state). Mapped to an FFI `ErrorCode` by the
/// caller.
#[derive(Debug, PartialEq, Eq)]
pub enum CmdError {
    /// A device-dependent command arrived with no session open.
    NotOpen,
    /// `Open` arrived while a session was already open.
    AlreadyOpen,
    /// The device itself failed (open, or a round-trip once open).
    Device(String),
}

impl std::fmt::Display for CmdError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::NotOpen => f.write_str("no device open"),
            Self::AlreadyOpen => f.write_str("a device is already open"),
            Self::Device(m) => f.write_str(m),
        }
    }
}

/// Why a request/reply round-trip produced no answer. Distinct from
/// [`CmdError`], which *is* an answer.
#[derive(Debug, PartialEq, Eq)]
pub enum ReqError {
    /// The supervisor's queue is gone. Only reachable during process teardown.
    WorkerGone,
    /// No answer within the deadline — the worker is occupied (in practice, an
    /// in-flight firmware update). The command may still run.
    Timeout,
    /// The worker panicked handling the command and dropped the reply. The
    /// supervisor recovers by reconnecting the link.
    Panicked,
}

impl std::fmt::Display for ReqError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::WorkerGone => f.write_str("engine worker is gone"),
            Self::Timeout => f.write_str("the engine worker is busy"),
            Self::Panicked => f.write_str("the engine worker failed"),
        }
    }
}

/// Commands the supervisor accepts.
pub enum Cmd {
    /// Open a device and start a session. Request/reply: the supervisor owns the
    /// "already open" decision, so this is where that race is resolved.
    Open { cfg: PicoViewConfig, panel: PanelSpec, reply: Reply<Result<(), CmdError>> },
    /// End the session and close the device. Request/reply so `pv_close` can
    /// return only once the device has actually torn down. Idempotent.
    Close(Reply<()>),
    /// Wake-up signal that a new frame is waiting in the shared [`FrameSlot`].
    /// Carries no payload — the frame itself lives in the slot (newest wins),
    /// which is what keeps a backlog from building up while the worker is busy.
    Flush,
    /// Stream a firmware image to the device and commit it. Blocks the worker for
    /// the transfer; on success the device reboots and the link is reconnected.
    Ota(Vec<u8>),
    /// Set the panel backlight brightness (0–255). Fire-and-forget.
    SetBrightness(u8),
    /// Play/stop a haptic effect on the device's DRV2605L. Fire-and-forget.
    Haptics(Haptics),
    /// Query the device for its [`wire::DeviceInfo`]. Request/reply, so
    /// `pv_request` can answer `get_device_info` in-band.
    GetDeviceInfo(Reply<Result<wire::DeviceInfo, CmdError>>),
}

/// Cheap, permanent handle to the supervisor thread.
///
/// Held as a process-wide `&'static` by the FFI layer, so it is never cloned and
/// never locked: every method is either a channel send or an atomic load. The
/// `Arc`s exist only to share the three pieces of state that genuinely straddle
/// the boundary (the frame slot, the buffer pool, and two flags), none of which
/// is device state.
pub struct Engine {
    tx: Sender<Cmd>,
    frame: Arc<FrameSlot>,
    pool: Arc<BufferPool>,
    /// Set by `pv_close` before sending [`Cmd::Close`] so a blocking transfer in
    /// progress (an OTA stream) bails out promptly instead of pinning the close
    /// behind the full transfer/timeout. Cleared by the supervisor once the
    /// session is torn down, so the next session starts un-cancelled.
    cancel: Arc<AtomicBool>,
    /// Whether a session is open, published by the supervisor. Read (never
    /// written) by the FFI layer to answer `NotOpen` for fire-and-forget commands
    /// without a round-trip.
    session: Arc<AtomicBool>,
}

/// How the supervisor opens a device. A function pointer rather than a direct
/// call so tests can drive the whole supervisor against a fake backend.
type OpenFn =
    fn(&PicoViewConfig, &PanelSpec) -> Result<(Box<dyn Transport>, PanelGeom, DeviceIdentity), OpenError>;

/// The shared state the supervisor thread needs, split out from [`Engine`] so
/// the two halves can't accidentally reach into each other's fields.
struct WorkerCtx {
    frame: Arc<FrameSlot>,
    pool: Arc<BufferPool>,
    cancel: Arc<AtomicBool>,
    session: Arc<AtomicBool>,
    open: OpenFn,
}

impl Engine {
    /// Spawn the supervisor thread. `None` when the thread could not be created,
    /// which leaves the engine permanently unavailable (surfaced as an internal
    /// error) rather than panicking inside a `OnceLock` initializer.
    pub fn spawn() -> Option<Engine> {
        Self::spawn_with(open_transport)
    }

    fn spawn_with(open: OpenFn) -> Option<Engine> {
        let (tx, rx) = std::sync::mpsc::channel::<Cmd>();
        let pool = Arc::new(BufferPool::new());
        let frame = Arc::new(FrameSlot::new(pool.clone()));
        let cancel = Arc::new(AtomicBool::new(false));
        let session = Arc::new(AtomicBool::new(false));
        let ctx = WorkerCtx {
            frame: frame.clone(),
            pool: pool.clone(),
            cancel: cancel.clone(),
            session: session.clone(),
            open,
        };
        match std::thread::Builder::new().name("pico-view".into()).spawn(move || run(rx, ctx)) {
            Ok(_join) => Some(Engine { tx, frame, pool, cancel, session }),
            Err(e) => {
                log::error!("failed to spawn the pico-view supervisor thread: {e}");
                None
            }
        }
    }

    /// Whether a session is currently open. Best-effort: it can go stale the
    /// instant after it is read, so it gates only fire-and-forget commands (where
    /// the supervisor drops a command that arrives too late anyway). Anything
    /// that must be authoritative — `Open`'s already-open check — is decided on
    /// the worker instead.
    pub fn is_open(&self) -> bool {
        self.session.load(Ordering::Acquire)
    }

    /// Ask a blocking transfer in progress to bail out. Sent ahead of
    /// [`Cmd::Close`] so the close isn't pinned behind a full OTA.
    pub fn cancel(&self) {
        self.cancel.store(true, Ordering::Relaxed);
    }

    /// Queue a fire-and-forget command. `Err` only when the supervisor is gone.
    pub fn send(&self, cmd: Cmd) -> Result<(), ReqError> {
        self.tx.send(cmd).map_err(|_| ReqError::WorkerGone)
    }

    /// Send a command that carries a [`Reply`] and block for its answer, up to
    /// `timeout`.
    pub fn request<T>(
        &self,
        build: impl FnOnce(Reply<T>) -> Cmd,
        timeout: Duration,
    ) -> Result<T, ReqError> {
        let (reply, rx) = reply_channel();
        self.send(build(reply))?;
        match rx.recv_timeout(timeout) {
            Ok(value) => Ok(value),
            Err(RecvTimeoutError::Timeout) => Err(ReqError::Timeout),
            // The reply was dropped without an answer: the worker panicked
            // handling this command (or is shutting down).
            Err(RecvTimeoutError::Disconnected) => Err(ReqError::Panicked),
        }
    }

    /// Copy one captured frame into the latest-frame slot and wake the supervisor
    /// if no flush signal is outstanding. `false` only when the supervisor's queue
    /// is gone.
    pub fn push_frame(&self, rgba: &[u8], width: u32, height: u32) -> bool {
        let buf = self.pool.copy_of(rgba);
        if self.frame.put(Frame { rgba: buf, width, height }) {
            self.tx.send(Cmd::Flush).is_ok()
        } else {
            true
        }
    }
}

/// The worker's view of the device link. `Up` owns the live backend; `Down`
/// means the device was lost (unplugged) and the worker is waiting until
/// `next_retry` before the next reopen attempt. Dropping the `Up` transport
/// closes the underlying USB handle, so a stale handle never lingers across a
/// reconnect.
enum Link {
    Up(Box<dyn Transport>),
    Down { next_retry: Instant },
}

/// Everything scoped to one open device, created by [`Cmd::Open`] and dropped by
/// [`Cmd::Close`]. Holding it in an `Option` on the supervisor's stack is what
/// makes "a device is open" a single-owner fact.
struct Session {
    cfg: PicoViewConfig,
    panel: PanelSpec,
    geom: PanelGeom,
    link: Link,
    events: LinkReporter,
    /// Previous frame (RGBA, as received) for dirty-rectangle diffing. The first
    /// flush pushes the whole panel; thereafter only the changed tiles are
    /// streamed (coalesced into a few windows), and an unchanged frame is dropped
    /// without touching the bus.
    prev: Option<Vec<u8>>,
    /// Start of the current run of touch-poll failures (`None` once one succeeds).
    poll_fail_since: Option<Instant>,
    /// Consecutive failed reopen attempts while disconnected; reset to 0 on a
    /// successful open. Drives the USB-reset escalation (RESET_AFTER_FAILS).
    reconnect_fails: u32,
    last_poll: Instant,
    last_keepalive: Instant,
}

/// Open the ESP32-P4 backend, returning the transport plus the geometry the
/// worker needs to diff and pack frames.
fn open_transport(
    cfg: &PicoViewConfig,
    spec: &PanelSpec,
) -> Result<(Box<dyn Transport>, PanelGeom, DeviceIdentity), OpenError> {
    crate::esp32p4::Esp32P4Transport::open(cfg, spec)
}

/// Supervisor loop. Runs for the life of the process, alternating between two
/// modes:
///
/// - **Idle** (no session): blocks in `recv`, costing nothing until a command
///   arrives.
/// - **In session**: `recv_timeout`s on the [`POLL_INTERVAL`] cadence so the
///   transport is ticked even while a burst of flushes keeps the channel busy.
///
/// The device link is supervised here: a failed [`Transport::flush`], or touch
/// polling that fails continuously for [`LINK_LOST_AFTER`], is taken as the
/// device having been unplugged. The worker then drops the backend (closing the
/// handle) and retries the open every [`RECONNECT_INTERVAL`] until the device
/// comes back, at which point it re-runs panel init and re-pushes the last frame
/// so the display is restored without waiting for Dart to repaint.
fn run(rx: Receiver<Cmd>, ctx: WorkerCtx) {
    let mut session: Option<Session> = None;
    loop {
        let cmd = match &session {
            None => match rx.recv() {
                Ok(cmd) => Some(cmd),
                Err(_) => return,
            },
            Some(s) => {
                let timeout = POLL_INTERVAL.saturating_sub(s.last_poll.elapsed());
                match rx.recv_timeout(timeout) {
                    Ok(cmd) => Some(cmd),
                    Err(RecvTimeoutError::Timeout) => None,
                    Err(RecvTimeoutError::Disconnected) => return,
                }
            }
        };

        // A panic in the device path (a backend bug, a slice index off a
        // malformed device reply) must not take the supervisor — and with it the
        // host application — down. Contain it, then fall through to the recovery
        // below.
        let step = AssertUnwindSafe(|| {
            if let Some(cmd) = cmd {
                dispatch(cmd, &mut session, &ctx);
            }
            if let Some(s) = session.as_mut() {
                s.tick(&ctx);
            }
        });
        if catch_unwind(step).is_err() {
            recover_from_panic(&mut session);
        }
    }
}

/// Recover the supervisor after a contained panic. The session is kept (silently
/// closing it would look to Dart like the app had dropped the device) but the
/// link is torn down, which puts the ordinary reconnect path in charge: Dart sees
/// DISCONNECTED then CONNECTED, exactly as for an unplug.
fn recover_from_panic(session: &mut Option<Session>) {
    log::error!("worker panicked handling a command; dropping the device link to recover");
    if let Some(s) = session.as_mut() {
        s.events.report(LinkState::Disconnected, "internal engine error");
        // Assignment drops the old transport, closing the USB handle.
        s.link = Link::Down { next_retry: Instant::now() + RECONNECT_INTERVAL };
        s.poll_fail_since = None;
    }
}

/// Execute one command. Every arm that can fail answers through its [`Reply`];
/// fire-and-forget arms log instead.
fn dispatch(cmd: Cmd, session: &mut Option<Session>, ctx: &WorkerCtx) {
    match cmd {
        Cmd::Open { cfg, panel, reply } => reply.send(open_session(session, cfg, panel, ctx)),
        Cmd::Close(reply) => {
            close_session(session, ctx);
            reply.send(());
        }
        Cmd::Flush => flush(session, ctx),
        Cmd::Ota(image) => match session.as_mut() {
            Some(s) => handle_ota(s, &image, &ctx.cancel),
            None => {
                log::warn!("firmware update requested while no device is open");
                crate::post::post_ota_status(OtaState::Failed, 0, -1);
            }
        },
        Cmd::SetBrightness(v) => match session.as_mut() {
            Some(s) => handle_set_brightness(&mut s.link, v),
            None => log::debug!("brightness set with no device open; ignored"),
        },
        Cmd::Haptics(h) => match session.as_mut() {
            Some(s) => handle_haptics(&mut s.link, h),
            None => log::debug!("haptics command with no device open; ignored"),
        },
        Cmd::GetDeviceInfo(reply) => reply.send(match session.as_mut() {
            Some(s) => device_info(&mut s.link, &s.geom),
            None => Err(CmdError::NotOpen),
        }),
    }
}

/// Open a device and start a session. This is the *only* place that decides
/// whether a device is already open, which is what makes the check race-free —
/// two concurrent `open_device` calls are serialized here, and the second sees
/// the first's session.
fn open_session(
    slot: &mut Option<Session>,
    cfg: PicoViewConfig,
    panel: PanelSpec,
    ctx: &WorkerCtx,
) -> Result<(), CmdError> {
    if slot.is_some() {
        return Err(CmdError::AlreadyOpen);
    }
    let (transport, geom, identity) =
        (ctx.open)(&cfg, &panel).map_err(|e| CmdError::Device(e.0))?;
    let mut events = LinkReporter::new();
    events.report_connected(&identity);
    let now = Instant::now();
    *slot = Some(Session {
        cfg,
        panel,
        geom,
        link: Link::Up(transport),
        events,
        prev: None,
        poll_fail_since: None,
        reconnect_fails: 0,
        last_poll: now,
        last_keepalive: now,
    });
    ctx.session.store(true, Ordering::Release);
    Ok(())
}

/// End the session and close the device.
fn close_session(slot: &mut Option<Session>, ctx: &WorkerCtx) {
    ctx.session.store(false, Ordering::Release);
    if let Some(session) = slot.take() {
        if let Some(prev) = session.prev {
            ctx.pool.put(prev);
        }
        // Drop closes the USB handle.
        drop(session.link);
    }
    ctx.cancel.store(false, Ordering::Relaxed);
    ctx.frame.drain();
}

/// Render the queued frame, if any. Frames that arrive with no session, or while
/// the link is down, are dropped (their buffers recycled); the cached `prev` is
/// what restores the display on reconnect.
fn flush(session: &mut Option<Session>, ctx: &WorkerCtx) {
    let Some(frame) = ctx.frame.take() else {
        return;
    };
    let Some(s) = session.as_mut() else {
        ctx.pool.put(frame.rgba);
        return;
    };
    let Link::Up(t) = &mut s.link else {
        ctx.pool.put(frame.rgba);
        return;
    };
    if render(&mut **t, &s.geom, frame, &mut s.prev, &ctx.pool).is_err() {
        log::warn!("display link lost (flush failed); will try to reopen");
        s.events.report(LinkState::Disconnected, "frame flush failed");
        s.link = Link::Down { next_retry: Instant::now() };
        s.poll_fail_since = None;
    }
}

impl Session {
    /// One cadence tick: advance the link (poll touch, or retry a reopen) and
    /// send the periodic keepalive.
    fn tick(&mut self, ctx: &WorkerCtx) {
        if self.last_poll.elapsed() >= POLL_INTERVAL {
            self.last_poll = Instant::now();
            self.advance_link(ctx);
        }

        // Heartbeat so an idle-but-live host doesn't look "gone" to the device
        // (which self-blanks the panel on host silence). Best-effort: a failed
        // write is only logged -- the poll/flush supervisor owns link-loss.
        if self.last_keepalive.elapsed() >= KEEPALIVE_INTERVAL {
            self.last_keepalive = Instant::now();
            if let Link::Up(t) = &mut self.link {
                if let Err(e) = t.keepalive() {
                    log::debug!("keepalive failed: {e}");
                }
            }
        }
    }

    /// Advance the link by one poll tick: poll touch while up (declaring the link
    /// lost after a sustained failure run), or attempt a throttled reopen while
    /// down. The link is moved out and back so state transitions don't run afoul
    /// of the borrow checker.
    fn advance_link(&mut self, ctx: &WorkerCtx) {
        let link = std::mem::replace(&mut self.link, Link::Down { next_retry: Instant::now() });
        self.link = match link {
            Link::Up(mut transport) => match transport.poll() {
                Ok(()) => {
                    self.poll_fail_since = None;
                    Link::Up(transport)
                }
                Err(e) => {
                    let now = Instant::now();
                    let since = *self.poll_fail_since.get_or_insert(now);
                    if now.duration_since(since) >= LINK_LOST_AFTER {
                        log::warn!(
                            "display link lost (touch poll failing: {e}); will try to reopen"
                        );
                        self.events.report(LinkState::Disconnected, &e);
                        self.poll_fail_since = None;
                        // Drop closes the USB handle before we reopen.
                        drop(transport);
                        Link::Down { next_retry: now }
                    } else {
                        Link::Up(transport)
                    }
                }
            },
            Link::Down { next_retry } => {
                if Instant::now() < next_retry {
                    // Throttled: not time to retry yet, put it back untouched.
                    Link::Down { next_retry }
                } else {
                    self.reopen(ctx)
                }
            }
        };
    }

    /// One reopen attempt while disconnected.
    fn reopen(&mut self, ctx: &WorkerCtx) -> Link {
        match (ctx.open)(&self.cfg, &self.panel) {
            Ok((mut transport, _geom, identity)) => {
                // Panel was re-initialized (blank); re-push the last frame so
                // the display returns even if Dart never repaints.
                if let Err(e) = restore(&mut *transport, &self.geom, &self.prev) {
                    log::warn!("display reopened but restoring frame failed: {e}");
                    self.reconnect_fails += 1;
                    return maybe_reset_after_fails(&self.cfg, self.reconnect_fails);
                }
                log::info!("display reconnected (after {} failed attempt(s))", self.reconnect_fails);
                self.reconnect_fails = 0;
                self.events.report_connected(&identity);
                Link::Up(transport)
            }
            Err(e) => {
                self.reconnect_fails += 1;
                if self.reconnect_fails == 1 || self.reconnect_fails % RESET_AFTER_FAILS == 0 {
                    log::warn!("reconnect attempt {} failed: {e}", self.reconnect_fails);
                } else {
                    log::debug!("reconnect attempt {} failed: {e}", self.reconnect_fails);
                }
                maybe_reset_after_fails(&self.cfg, self.reconnect_fails)
            }
        }
    }
}

/// De-duplicating link event reporter: Dart only hears transitions
/// (connected / disconnected), not every retry tick. One per session, so each
/// newly opened device re-reports CONNECTED.
struct LinkReporter {
    last: LinkState,
}

impl LinkReporter {
    fn new() -> Self {
        Self { last: LinkState::Unspecified }
    }

    fn report(&mut self, state: LinkState, detail: &str) {
        if self.last != state {
            self.last = state;
            crate::post::post_link(state, detail);
        }
    }

    /// Report a CONNECTED transition carrying what the handshake learned about
    /// the device, so Dart can show its `fw_version` and whether it is a genuine
    /// vendor unit.
    fn report_connected(&mut self, identity: &DeviceIdentity) {
        if self.last != LinkState::Connected {
            self.last = LinkState::Connected;
            crate::post::post_link_connected(identity);
        }
    }
}

/// Stream a firmware image to the device (blocking) and, on success, mark the
/// link down so the supervisor reconnects once the device finishes rebooting into
/// the new image. `ota()` posts its own progress/result to Dart; here we only log
/// and drive the reconnect. A no-op (with an error status to Dart) if the device
/// is currently disconnected.
fn handle_ota(session: &mut Session, image: &[u8], cancel: &AtomicBool) {
    // Run the transfer while the transport is borrowed, then release the borrow
    // before mutating `link` (the success path replaces it with `Down`).
    let result = match &mut session.link {
        Link::Up(t) => Some(t.ota(image, cancel)),
        Link::Down { .. } => None,
    };
    match result {
        Some(Ok(())) => {
            log::info!("firmware update complete; device rebooting, will reconnect");
            session.events.report(LinkState::Disconnected, "rebooting into updated firmware");
            session.link = Link::Down { next_retry: Instant::now() + RECONNECT_INTERVAL };
            session.poll_fail_since = None;
        }
        Some(Err(e)) => log::warn!("firmware update failed: {e}"),
        None => {
            log::warn!("firmware update requested while device disconnected");
            crate::post::post_ota_status(OtaState::Failed, 0, -1);
        }
    }
}

/// Push a backlight brightness to the device (best-effort, fire-and-forget). A
/// no-op while disconnected, and a write error is only logged — the poll/flush
/// supervisor owns link-loss detection, so a transient brightness failure must
/// not tear the link down here.
fn handle_set_brightness(link: &mut Link, brightness: u8) {
    match link {
        Link::Up(t) => {
            if let Err(e) = t.set_brightness(brightness) {
                log::warn!("set brightness failed: {e}");
            }
        }
        Link::Down { .. } => log::debug!("brightness set while device disconnected; ignored"),
    }
}

/// Send a haptic command to the device (best-effort, fire-and-forget). A no-op
/// while disconnected; a write error is only logged so a transient haptics
/// failure never tears the link down (the poll/flush supervisor owns that).
fn handle_haptics(link: &mut Link, cmd: Haptics) {
    match link {
        Link::Up(t) => {
            if let Err(e) = t.haptics(cmd) {
                log::warn!("haptics command failed: {e}");
            }
        }
        Link::Down { .. } => log::debug!("haptics command while device disconnected; ignored"),
    }
}

/// Query the device for its [`wire::DeviceInfo`]. The device can't know the
/// panel's visible *shape* (the wire `Config` carries only geometry, not the
/// round/rect outline), so the worker overlays it from the [`PanelGeom`] it
/// resolved at open — the app then gets a complete panel descriptor from one
/// call, which is the only consumer of the shape.
fn device_info(link: &mut Link, geom: &PanelGeom) -> Result<wire::DeviceInfo, CmdError> {
    match link {
        Link::Up(t) => t.get_device_info().map_err(CmdError::Device).map(|mut info| {
            // The device reports UNSPECIFIED shape; fill it from what we resolved.
            let wire_shape = match geom.shape {
                PanelShape::Round => wire::PanelShape::Round,
                PanelShape::Rect => wire::PanelShape::Rect,
            };
            let panel = info.panel.get_or_insert_with(Default::default);
            panel.shape = wire_shape as i32;
            info
        }),
        Link::Down { .. } => Err(CmdError::Device("device is disconnected".to_string())),
    }
}

/// Pick the next `Down` retry deadline after a failed reopen, escalating to a USB
/// bus reset every `RESET_AFTER_FAILS` consecutive failures. The reset kicks a
/// device a bare re-open can't recover (halted endpoint, or a firmware framer
/// left mid-frame by the teardown); since it re-enumerates the device, we back
/// off a little longer before the next attempt. `reconnect_fails` is the (already
/// incremented) count of consecutive failures.
fn maybe_reset_after_fails(cfg: &PicoViewConfig, reconnect_fails: u32) -> Link {
    if reconnect_fails % RESET_AFTER_FAILS == 0 {
        log::warn!(
            "device unresponsive after {reconnect_fails} reopen attempts; forcing USB bus reset"
        );
        match crate::esp32p4::Esp32P4Transport::reset_device(cfg) {
            Ok(()) => log::info!("USB bus reset issued; device re-enumerating"),
            Err(e) => log::debug!("USB bus reset failed (device may be gone): {e}"),
        }
        // Give the device time to re-enumerate before the next reopen.
        Link::Down { next_retry: Instant::now() + RECONNECT_INTERVAL * 2 }
    } else {
        Link::Down { next_retry: Instant::now() + RECONNECT_INTERVAL }
    }
}

/// Re-push the last rendered frame in full to a freshly (re)initialized panel, so
/// the display comes back after a reconnect without waiting for Dart. No-op until
/// the first frame has been rendered.
fn restore(
    transport: &mut dyn Transport,
    geom: &PanelGeom,
    prev: &Option<Vec<u8>>,
) -> Result<(), String> {
    let Some(rgba) = prev.as_ref() else {
        return Ok(());
    };
    let rect = Rect::full(geom.width, geom.height);
    let pixels = lcd::rgba_rect_to_rgb565_be(rgba, geom.width, rect);
    transport.flush(vec![(rect, pixels)])
}

/// Diff + convert + window + stream one frame to the backend. Only the regions
/// that changed since the previous frame are pushed (coalesced changed tiles, or
/// their bounding box when that is cheaper); an unchanged frame is dropped
/// without any bus traffic. `prev` holds the last *rendered* frame (RGBA) for
/// the next diff.
///
/// Owns the frame's buffer for its whole lifetime: whichever buffer stops being
/// needed — the incoming frame on every skip path, the outgoing `prev` on a
/// successful render — goes back to `pool` for the next capture to reuse.
///
/// Returns `Err(())` only when the transport flush itself failed (the signal the
/// worker uses to mark the link lost); frames that are merely skipped (geometry
/// mismatch, unchanged) return `Ok(())`. On a flush failure `prev` is left
/// intact so it can be re-pushed once the device is reopened.
fn render(
    transport: &mut dyn Transport,
    geom: &PanelGeom,
    frame: Frame,
    prev: &mut Option<Vec<u8>>,
    pool: &BufferPool,
) -> Result<(), ()> {
    if frame.width != geom.width || frame.height != geom.height {
        log::warn!(
            "frame {}x{} != panel {}x{}; skipping",
            frame.width,
            frame.height,
            geom.width,
            geom.height
        );
        pool.put(frame.rgba);
        return Ok(());
    }
    let needed = (geom.width * geom.height) as usize * 4;
    if frame.rgba.len() < needed {
        log::warn!("frame too small: got {} bytes, need {needed}", frame.rgba.len());
        pool.put(frame.rgba);
        return Ok(());
    }

    let start = Instant::now();

    // The regions to push: the changed regions vs. the previous frame, or the
    // whole panel when there's nothing comparable to diff against.
    let rects = match prev.as_deref() {
        Some(p) if p.len() == frame.rgba.len() => {
            let rects = lcd::dirty_rects(p, &frame.rgba, geom.width, geom.height);
            // Identical frame: skip the bus entirely, keep `prev` as-is.
            if rects.is_empty() {
                log::trace!("frame unchanged; skipping flush");
                pool.put(frame.rgba);
                return Ok(());
            }
            rects
        }
        _ => vec![Rect::full(geom.width, geom.height)],
    };

    let dirty_px: u64 = rects.iter().map(|r| r.w as u64 * r.h as u64).sum();
    let panel_px = geom.width as u64 * geom.height as u64;
    let rect_count = rects.len();
    let est_cost = lcd::transfer_cost(&rects);
    if log::log_enabled!(log::Level::Debug) {
        let coverage = if panel_px > 0 {
            dirty_px as f64 / panel_px as f64 * 100.0
        } else {
            0.0
        };
        log::debug!(
            "dirty: {} rect(s), {dirty_px}/{panel_px} px ({coverage:.1}%) {:?}",
            rects.len(),
            rects,
        );
    }

    // Pack each changed region to big-endian RGB565 and hand ownership to the
    // backend (the P4 streams them as BLITs).
    let packed: Vec<(Rect, Vec<u8>)> = rects
        .into_iter()
        .map(|rect| {
            let pixels = lcd::rgba_rect_to_rgb565_be(&frame.rgba, geom.width, rect);
            (rect, pixels)
        })
        .collect();

    if let Err(e) = transport.flush(packed) {
        log::warn!("flush failed: {e}");
        // Keep `prev` so the worker can restore it after reopening the device.
        pool.put(frame.rgba);
        return Err(());
    }
    // The frame just rendered becomes the diff baseline; the baseline it replaces
    // is exactly the right size for the next capture, so recycle it.
    if let Some(retired) = prev.replace(frame.rgba) {
        pool.put(retired);
    }

    let elapsed_ms = start.elapsed().as_secs_f64() * 1e3;
    log::debug!(
        "{rect_count} rect(s), {dirty_px}/{panel_px} px, cost {est_cost}, drew in {elapsed_ms:.2} ms"
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    const PANEL: u32 = 8;

    /// Minimal fake backend. `info` answers `get_device_info`; `ota_delay` lets a
    /// test park the worker inside a command to exercise request deadlines;
    /// `panic_on_info` models a backend bug for the containment test.
    #[derive(Default)]
    struct FakeTransport {
        info: wire::DeviceInfo,
        ota_delay: Duration,
        panic_on_info: bool,
    }

    impl Transport for FakeTransport {
        fn flush(&mut self, _rects: Vec<(Rect, Vec<u8>)>) -> Result<(), String> {
            Ok(())
        }
        fn get_device_info(&mut self) -> Result<wire::DeviceInfo, String> {
            assert!(!self.panic_on_info, "simulated backend bug");
            Ok(self.info.clone())
        }
        fn ota(&mut self, _image: &[u8], _cancel: &AtomicBool) -> Result<(), String> {
            std::thread::sleep(self.ota_delay);
            Ok(())
        }
    }

    fn geom(shape: PanelShape) -> PanelGeom {
        PanelGeom { width: PANEL, height: PANEL, shape }
    }

    type Opened = Result<(Box<dyn Transport>, PanelGeom, DeviceIdentity), OpenError>;

    fn fake_open(_cfg: &PicoViewConfig, _panel: &PanelSpec) -> Opened {
        let info = wire::DeviceInfo { serial: "PV-P4-0001".into(), ..Default::default() };
        let t = FakeTransport { info, ..Default::default() };
        Ok((Box::new(t), geom(PanelShape::Round), DeviceIdentity::default()))
    }

    fn failing_open(_cfg: &PicoViewConfig, _panel: &PanelSpec) -> Opened {
        Err(OpenError("no such device".into()))
    }

    /// Opens a backend whose OTA parks the worker for 300ms.
    fn slow_ota_open(_cfg: &PicoViewConfig, _panel: &PanelSpec) -> Opened {
        let t = FakeTransport { ota_delay: Duration::from_millis(300), ..Default::default() };
        Ok((Box::new(t), geom(PanelShape::Round), DeviceIdentity::default()))
    }

    /// Opens a backend that panics on `get_device_info`, once. The reopen after
    /// the panic yields a healthy transport, mirroring how a real reconnect
    /// re-runs the whole open path.
    fn panicking_open(_cfg: &PicoViewConfig, _panel: &PanelSpec) -> Opened {
        static FIRST: AtomicBool = AtomicBool::new(true);
        let panic_on_info = FIRST.swap(false, Ordering::SeqCst);
        let t = FakeTransport { panic_on_info, ..Default::default() };
        Ok((Box::new(t), geom(PanelShape::Round), DeviceIdentity::default()))
    }

    /// Open a session on `engine`, asserting it succeeded.
    fn open(engine: &Engine) -> Result<(), CmdError> {
        engine
            .request(
                |reply| Cmd::Open {
                    cfg: PicoViewConfig::default(),
                    panel: crate::panels::resolve(crate::panels::DEFAULT_MODEL).unwrap(),
                    reply,
                },
                OPEN_TIMEOUT,
            )
            .expect("open round-trip")
    }

    fn close(engine: &Engine) {
        engine.request(Cmd::Close, CLOSE_TIMEOUT).expect("close round-trip");
    }

    // --- supervisor lifecycle --------------------------------------------------

    #[test]
    fn open_close_reopen_on_one_permanent_worker() {
        let engine = Engine::spawn_with(fake_open).expect("supervisor");
        assert!(!engine.is_open());

        assert_eq!(open(&engine), Ok(()));
        assert!(engine.is_open());

        close(&engine);
        assert!(!engine.is_open());

        // The thread outlives the session: a second open must work on the same
        // supervisor, which is the whole point of the permanent-actor shape.
        assert_eq!(open(&engine), Ok(()));
        assert!(engine.is_open());
    }

    #[test]
    fn second_open_is_rejected_by_the_worker() {
        let engine = Engine::spawn_with(fake_open).expect("supervisor");
        assert_eq!(open(&engine), Ok(()));
        // Dart's hot-restart recovery keys off this being distinct from Device.
        assert_eq!(open(&engine), Err(CmdError::AlreadyOpen));
    }

    #[test]
    fn failed_open_leaves_no_session() {
        let engine = Engine::spawn_with(failing_open).expect("supervisor");
        assert!(matches!(open(&engine), Err(CmdError::Device(_))));
        assert!(!engine.is_open());
        // ... and the failure is not sticky: the session slot is still free.
        assert!(matches!(open(&engine), Err(CmdError::Device(_))));
    }

    #[test]
    fn close_with_nothing_open_is_a_no_op() {
        let engine = Engine::spawn_with(fake_open).expect("supervisor");
        close(&engine);
        assert!(!engine.is_open());
    }

    #[test]
    fn close_rearms_the_cancel_flag_for_the_next_session() {
        let engine = Engine::spawn_with(fake_open).expect("supervisor");
        assert_eq!(open(&engine), Ok(()));
        // `pv_close` sets this so an in-flight OTA bails out.
        engine.cancel();
        close(&engine);
        // A permanent worker keeps the flag across sessions, so close must clear
        // it -- otherwise the next session's first OTA would cancel itself.
        assert!(!engine.cancel.load(Ordering::Relaxed), "cancel must be re-armed by close");
    }

    // --- request/reply ---------------------------------------------------------

    #[test]
    fn device_info_round_trips_and_overlays_panel_shape() {
        let engine = Engine::spawn_with(fake_open).expect("supervisor");
        assert_eq!(open(&engine), Ok(()));
        let got = engine
            .request(Cmd::GetDeviceInfo, DEVICE_INFO_TIMEOUT)
            .expect("round-trip")
            .expect("device info");
        assert_eq!(got.serial, "PV-P4-0001");
        // The device reports UNSPECIFIED; the worker fills it from `geom`.
        assert_eq!(got.panel.expect("panel filled").shape, wire::PanelShape::Round as i32);
    }

    #[test]
    fn device_info_with_nothing_open_is_not_open() {
        let engine = Engine::spawn_with(fake_open).expect("supervisor");
        let answer = engine.request(Cmd::GetDeviceInfo, DEVICE_INFO_TIMEOUT).expect("round-trip");
        assert_eq!(answer, Err(CmdError::NotOpen));
    }

    #[test]
    fn a_busy_worker_times_out_instead_of_stalling_the_caller() {
        let engine = Engine::spawn_with(slow_ota_open).expect("supervisor");
        assert_eq!(open(&engine), Ok(()));
        // Park the worker inside a blocking transfer, then ask it something. The
        // pre-fix behaviour here was an unbounded block for the whole transfer.
        engine.send(Cmd::Ota(vec![0u8; 4])).expect("queue ota");
        let start = Instant::now();
        let answer = engine.request(Cmd::GetDeviceInfo, Duration::from_millis(50));
        assert_eq!(answer.err(), Some(ReqError::Timeout));
        assert!(start.elapsed() < Duration::from_millis(250), "returned on its own deadline");
    }

    #[test]
    fn a_panicking_command_is_contained_and_the_worker_recovers() {
        let engine = Engine::spawn_with(panicking_open).expect("supervisor");
        assert_eq!(open(&engine), Ok(()));

        // The backend blows up mid-command. Pre-fix this aborted the process
        // (`panic = "abort"`) or, unwinding, killed the worker for good.
        let answer = engine.request(Cmd::GetDeviceInfo, DEVICE_INFO_TIMEOUT);
        assert_eq!(answer.err(), Some(ReqError::Panicked), "the dropped reply must surface");

        // The session survives; the link was dropped, so the ordinary reconnect
        // path brings it back with a fresh (healthy) transport.
        assert!(engine.is_open(), "a panic must not silently close the session");
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            match engine.request(Cmd::GetDeviceInfo, DEVICE_INFO_TIMEOUT).expect("round-trip") {
                Ok(_) => break,
                Err(e) => {
                    assert!(Instant::now() < deadline, "worker never recovered: {e}");
                    std::thread::sleep(Duration::from_millis(20));
                }
            }
        }
    }

    #[test]
    fn device_info_overlays_rect_shape_too() {
        let mut link = Link::Up(Box::new(FakeTransport::default()));
        let got = device_info(&mut link, &geom(PanelShape::Rect)).expect("device info");
        assert_eq!(got.panel.expect("panel filled").shape, wire::PanelShape::Rect as i32);
    }

    #[test]
    fn device_info_while_disconnected_is_a_device_error() {
        let mut link = Link::Down { next_retry: Instant::now() };
        assert!(matches!(
            device_info(&mut link, &geom(PanelShape::Rect)),
            Err(CmdError::Device(_))
        ));
    }

    // --- rendering / recycling -------------------------------------------------

    fn rgba(width: u32, fill: u8) -> Frame {
        Frame {
            rgba: vec![fill; (width * width * 4) as usize],
            width,
            height: width,
        }
    }

    #[test]
    fn render_recycles_the_retired_baseline() {
        let pool = BufferPool::new();
        let mut t = FakeTransport::default();
        let mut prev = None;
        let g = geom(PanelShape::Rect);

        render(&mut t, &g, rgba(PANEL, 0), &mut prev, &pool).expect("first render");
        assert!(prev.is_some(), "first frame becomes the baseline");
        assert_eq!(pool.len(), 0, "nothing retired yet");

        render(&mut t, &g, rgba(PANEL, 1), &mut prev, &pool).expect("second render");
        assert_eq!(pool.len(), 1, "the old baseline was recycled");
    }

    #[test]
    fn render_recycles_skipped_frames() {
        let pool = BufferPool::new();
        let mut t = FakeTransport::default();
        let mut prev = None;
        let g = geom(PanelShape::Rect);

        // Geometry mismatch: dropped, but the buffer must not be.
        render(&mut t, &g, rgba(PANEL + 1, 0), &mut prev, &pool).expect("skip");
        assert_eq!(pool.len(), 1, "mismatched frame recycled");

        // Unchanged frame: skipped before any bus traffic, likewise recycled.
        render(&mut t, &g, rgba(PANEL, 7), &mut prev, &pool).expect("baseline");
        pool.clear();
        render(&mut t, &g, rgba(PANEL, 7), &mut prev, &pool).expect("unchanged");
        assert_eq!(pool.len(), 1, "unchanged frame recycled");
    }
}
