//! ESP32-P4 vendor-bulk display backend.
//!
//! Streams diffed RGB565 regions to ESP32-P4 firmware over a driverless USB-HS
//! vendor-bulk endpoint. The firmware owns the panel and touch controller;
//! this host side just frames messages on the wire:
//!
//! - `BLIT` frames stay raw (a full 360x360 frame is ~253 KB and needs no
//!   schema evolution): header + `x,y,w,h` (u16 LE) + RGB565-BE pixels.
//! - everything else is ONE protobuf-encoded message per `CTRL` frame:
//!   [`wire::HostToDevice`] on the OUT endpoint, [`wire::DeviceToHost`] on the
//!   IN endpoint (schema: `proto/pv_wire.proto`, shared with the firmware's
//!   nanopb build and the Dart FFI layer).
//!
//! - open: claim interface 0, `Hello`/`HelloAck` handshake, an advisory
//!   attestation exchange (a label only -- see [`attest`]), then a `Config`
//!   derived from the resolved [`PanelSpec`] (geometry/rotation/invert + touch
//!   address & axis flags), acknowledged by `ConfigAck` (v1 fired it blind).
//! - [`Transport::flush`]: one `BLIT` per dirty region, `PRESENT` on the last.
//! - touch: a background reader thread decodes `Touch` messages off the bulk IN
//!   endpoint and forwards them through the existing [`crate::touch::emit`]
//!   path, feeding the same Dart touch stream the widget tree consumes.
//!
//! The framing mirrors `firmwares/esp32p4/main/protocol.h`; keep the
//! two in sync.

use crate::auth;
use crate::config::PicoViewConfig;
use crate::lcd::Rect;
use crate::panels::PanelSpec;
use crate::proto::wire::{self, device_to_host, host_to_device};
use crate::transport::{DeviceIdentity, OpenError, PanelGeom, Transport};
use nusb::io::{EndpointRead, EndpointWrite};
use nusb::transfer::{Bulk, In, Out};
use nusb::MaybeFuture;
use prost::Message;
use sha2::{Digest, Sha256};
use std::io::{Read, Write};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{Receiver, RecvTimeoutError, Sender};
use std::sync::Arc;
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

// --- Wire framing (mirrors protocol.h) ----------------------------------------

/// "PVUS" as a little-endian u32 ('P'=0x50 is the first byte on the wire).
const PV_MAGIC: u32 = 0x5355_5650;
const PV_HEADER_LEN: usize = 12;
/// v2 = protobuf CTRL frames. Sent in `Hello`, echoed in `HelloAck`.
const PV_PROTO_VERSION: u32 = 2;

/// Frame types. 1..=31 are reserved (the v1 per-message ids), so a v2 host
/// talking to v1 firmware gets silence at the handshake instead of a mis-parse.
const MSG_BLIT: u16 = 3;
const MSG_CTRL: u16 = 32;

/// Max image bytes carried by one `OtaData` (mirrors pv_wire.options).
const OTA_CHUNK_MAX: usize = 8192;

/// BLIT flag: last region of the current frame.
const FLAG_PRESENT: u16 = 1 << 0;

// --- USB identity ------------------------------------------------------------

/// The ESP32-P4 product unit: the only device this backend drives. It speaks the
/// PVUS framing. VID is Espressif's shared vendor id + our allocated PID from
/// espressif/usb-pids.
const PV_VID: u16 = 0x303A;
const PV_PID: u16 = 0x839A;
const PV_NAME: &str = "ESP32-P4";

/// Locate an ESP32-P4 on the bus: by USB serial string when `serial` is set
/// (the eFuse-derived unique id the firmware reports, matched at enumeration
/// without opening any device), otherwise the nth device matching the VID/PID.
fn find_device(index: u32, serial: Option<&str>) -> Result<nusb::DeviceInfo, String> {
    let mut devices = nusb::list_devices()
        .wait()
        .map_err(|e| format!("USB enumerate failed: {e}"))?
        .filter(|d| d.vendor_id() == PV_VID && d.product_id() == PV_PID);
    match serial {
        Some(want) => devices
            .find(|d| d.serial_number() == Some(want))
            .ok_or_else(|| format!("no pico-view device with serial '{want}'")),
        None => devices
            .nth(index as usize)
            .ok_or_else(|| format!("no pico-view device found (index {index})")),
    }
}

const EP_OUT: u8 = 0x01;
const EP_IN: u8 = 0x81;

/// OUT writer transfer size. A full 360×360 frame is ~253 KB; 64 KB transfers
/// keep the queue shallow while amortising per-transfer overhead.
const OUT_XFER: usize = 64 * 1024;
/// IN reader transfer size (control messages are tiny; this is just headroom).
const IN_XFER: usize = 4096;

/// Upper bound on a device→host payload. The largest real message is an
/// `AuthResponse`: ~221 bytes (157-byte ECDSA cert + 64-byte signature),
/// comfortably under this cap.
/// A header whose length field exceeds this is a desynced stream, not a big
/// message — without the cap a single garbage length (up to 4 GiB) would stall
/// the framer forever waiting for bytes that never come.
const IN_MSG_MAX: usize = 4096;

/// Per-transfer timeout for ongoing touch reads. Also bounds how long the reader
/// thread takes to notice the stop flag at shutdown.
const READ_TIMEOUT: Duration = Duration::from_millis(200);
/// How long `open` waits for `HelloAck` (retried once, then a hard failure --
/// a silent device can't be configured, so it is refused, not tolerated).
const HELLO_TIMEOUT: Duration = Duration::from_millis(500);
/// How long `open` waits for `AuthResponse` (ECDSA signing is sub-ms; this is
/// USB slack). Only ever spent on a device that advertised the auth capability,
/// and only to arrive at a label -- the open continues either way.
const AUTH_TIMEOUT: Duration = Duration::from_secs(2);
/// How long `open` waits for `ConfigAck` (panel reset + init regs + GRAM clear).
const CONFIG_ACK_TIMEOUT: Duration = Duration::from_secs(5);
/// Write timeout; a stalled bus surfaces as a flush error (no auto-reconnect).
const WRITE_TIMEOUT: Duration = Duration::from_secs(2);
/// How long to wait for the device to erase the target slot and ack `OtaBegin`
/// (a full 5 MB slot erase takes a few seconds).
const OTA_BEGIN_TIMEOUT: Duration = Duration::from_secs(30);
/// How long to wait for the device to verify + commit after `OtaEnd`.
const OTA_END_TIMEOUT: Duration = Duration::from_secs(30);
/// How long `get_device_info` waits for the device's `DeviceInfo` reply.
const DEVICE_INFO_TIMEOUT: Duration = Duration::from_secs(1);

/// The ESP32-P4 backend: an OUT writer for frames + a touch reader thread.
pub struct Esp32P4Transport {
    writer: EndpointWrite<Bulk>,
    touch_stop: Arc<AtomicBool>,
    touch_join: Option<JoinHandle<()>>,
    // Set by the reader thread when it exits on a bus error. `poll` reports it
    // so the worker's link supervisor reconnects even when no frames are being
    // flushed (a static screen would otherwise mask an unplug forever).
    link_dead: Arc<AtomicBool>,
    // OTA status messages decoded by the reader thread. `ota()` (on the worker
    // thread) drives the OUT side and reads results back here, so the single bulk
    // IN reader is shared with touch without a second endpoint claim.
    ota_status_rx: Receiver<wire::OtaStatus>,
    // DeviceInfo replies decoded by the reader thread. `get_device_info()` (on the
    // worker thread) writes the GetDeviceInfo request and reads the reply back
    // here, sharing the single bulk IN reader the same way OTA status does.
    device_info_rx: Receiver<wire::DeviceInfo>,
    // Kept alive so the claimed interface (and its endpoints) outlive the
    // transport; dropped after `Drop` joins the reader thread.
    _device: nusb::Device,
    _interface: nusb::Interface,
}

/// A failed OTA transfer, carried out of [`Esp32P4Transport::ota_run`] so the
/// single [`Esp32P4Transport::ota`] funnel posts exactly one terminal
/// `OtaState::Failed` to Dart. `code` is the device-reported `pv_ota_err` when
/// the device rejected the image, or `-1` for a host/USB-side failure; `pct`
/// carries the last known progress so the UI's terminal event is accurate.
struct OtaFail {
    msg: String,
    pct: u32,
    code: i32,
}

/// Build an [`OtaFail`] for a host/USB-side failure (no device err code).
fn ota_host_fail(msg: String) -> OtaFail {
    OtaFail { msg, pct: 0, code: -1 }
}

impl Esp32P4Transport {
    pub fn open(
        cfg: &PicoViewConfig,
        spec: &PanelSpec,
    ) -> Result<(Box<dyn Transport>, PanelGeom, DeviceIdentity), OpenError> {
        let dev_err = OpenError;
        let info = find_device(cfg.index, cfg.serial.as_deref()).map_err(dev_err)?;
        match &cfg.serial {
            Some(serial) => log::info!(
                "pico-view: opening {PV_NAME} {PV_VID:04x}:{PV_PID:04x} (serial {serial})"
            ),
            None => log::info!(
                "pico-view: opening {PV_NAME} {PV_VID:04x}:{PV_PID:04x} (index {})",
                cfg.index
            ),
        }

        // A missing/ineffective udev rule fails here, at open, not at claim.
        let device = info.open().wait().map_err(|e| {
            dev_err(format!(
                "USB open failed (is 99-pico-view.rules installed on Linux?): {e}"
            ))
        })?;
        let interface = device.claim_interface(0).wait().map_err(|e| {
            dev_err(format!(
                "claim interface 0 failed (is 99-pico-view.rules installed on Linux?): {e}"
            ))
        })?;

        let out_ep = interface
            .endpoint::<Bulk, Out>(EP_OUT)
            .map_err(|e| dev_err(format!("open bulk OUT {EP_OUT:#04x} failed: {e}")))?;
        let in_ep = interface
            .endpoint::<Bulk, In>(EP_IN)
            .map_err(|e| dev_err(format!("open bulk IN {EP_IN:#04x} failed: {e}")))?;

        let mut writer = out_ep.writer(OUT_XFER);
        writer.set_num_transfers(4);
        writer.set_write_timeout(WRITE_TIMEOUT);
        let mut reader = in_ep.reader(IN_XFER);

        // Handshake: Hello -> HelloAck is mandatory (the ACK carries the proto
        // version and capabilities the rest of the session depends on). Retried
        // once -- the device may still be settling right after enumeration.
        let mut framer = Framer::new();
        reader.set_read_timeout(HELLO_TIMEOUT);
        let mut ack: Option<wire::HelloAck> = None;
        'hello: for attempt in 0..2 {
            write_ctrl(
                &mut writer,
                host_to_device::Msg::Hello(wire::Hello { proto_version: PV_PROTO_VERSION }),
            )
            .map_err(|e| dev_err(format!("HELLO write failed: {e}")))?;
            writer.flush().map_err(|e| dev_err(format!("HELLO flush failed: {e}")))?;
            // Overall budget for this attempt: caps both a byte-trickle and a
            // flood of skippable frames, neither of which the per-read timeout
            // bounds on its own.
            let deadline = Instant::now() + HELLO_TIMEOUT;
            loop {
                match read_ctrl(&mut reader, &mut framer, deadline) {
                    Ok(Some(device_to_host::Msg::HelloAck(a))) => {
                        ack = Some(a);
                        break 'hello;
                    }
                    // Stale touch/status from a previous session; keep draining.
                    Ok(Some(other)) => {
                        log::debug!("ESP32-P4: skipping {} while awaiting HELLO_ACK", msg_name(&other))
                    }
                    Ok(None) => {
                        log::warn!("ESP32-P4: no HELLO_ACK (attempt {})", attempt + 1);
                        break;
                    }
                    Err(e) => {
                        log::warn!("ESP32-P4: HELLO_ACK read failed (attempt {}): {e}", attempt + 1);
                        break;
                    }
                }
            }
        }
        let ack =
            ack.ok_or_else(|| dev_err("no HELLO_ACK from device (pre-v2 firmware?)".to_string()))?;

        if ack.proto_version != PV_PROTO_VERSION {
            log::warn!(
                "ESP32-P4 proto mismatch: host {PV_PROTO_VERSION}, device {}",
                ack.proto_version
            );
        }
        let caps = ack.caps.unwrap_or_default();
        log::info!(
            "ESP32-P4 HELLO_ACK (proto {}, auth {}, fw '{}')",
            ack.proto_version,
            caps.auth,
            ack.fw_version
        );

        // Carry the firmware version out to Dart on the CONNECTED event; empty
        // means the device didn't report one.
        let mut identity = DeviceIdentity {
            fw_version: (!ack.fw_version.is_empty()).then(|| ack.fw_version.clone()),
            verified: false,
            device_id: None,
        };

        // Attestation: ask the device to prove it is a genuine vendor unit.
        if caps.auth {
            match attest(&mut writer, &mut reader, &mut framer) {
                Ok(device_id) => {
                    log::info!("{PV_NAME}: attestation OK, genuine device '{device_id}'");
                    identity.verified = true;
                    identity.device_id = Some(device_id);
                }
                Err(e) => log::warn!(
                    "{PV_NAME}: device advertised auth but failed attestation \
                     (driving it anyway, unverified): {e}"
                ),
            }
        } else {
            log::info!("{PV_NAME}: device does not advertise auth; unverified (not a fault)");
        }

        let config = build_config(spec).map_err(dev_err)?;
        write_ctrl(&mut writer, host_to_device::Msg::Config(config))
            .map_err(|e| dev_err(format!("CONFIG write failed: {e}")))?;
        writer.flush().map_err(|e| dev_err(format!("CONFIG flush failed: {e}")))?;

        reader.set_read_timeout(CONFIG_ACK_TIMEOUT);
        let deadline = Instant::now() + CONFIG_ACK_TIMEOUT;
        loop {
            if Instant::now() > deadline {
                return Err(dev_err("device did not acknowledge CONFIG in time".to_string()));
            }
            match read_ctrl(&mut reader, &mut framer, deadline) {
                Ok(Some(device_to_host::Msg::ConfigAck(a))) => {
                    if a.status() == wire::Status::Ok {
                        break;
                    }
                    return Err(dev_err(format!("device rejected CONFIG: {}", a.detail)));
                }
                Ok(Some(other)) => {
                    log::debug!("ESP32-P4: skipping {} while awaiting CONFIG_ACK", msg_name(&other))
                }
                Ok(None) => {
                    return Err(dev_err("device did not acknowledge CONFIG".to_string()))
                }
                Err(e) => return Err(dev_err(format!("CONFIG_ACK read failed: {e}"))),
            }
        }

        reader.set_read_timeout(READ_TIMEOUT);
        let (ota_tx, ota_status_rx) = std::sync::mpsc::channel::<wire::OtaStatus>();
        let (info_tx, device_info_rx) = std::sync::mpsc::channel::<wire::DeviceInfo>();
        let touch_stop = Arc::new(AtomicBool::new(false));
        let link_dead = Arc::new(AtomicBool::new(false));
        let stop = touch_stop.clone();
        let dead = link_dead.clone();
        let touch_join = std::thread::Builder::new()
            .name("pico-view-touch".into())
            .spawn(move || touch_loop(reader, framer, stop, dead, ota_tx, info_tx))
            .map_err(|e| dev_err(format!("failed to spawn touch reader thread: {e}")))?;

        let geom = PanelGeom { width: spec.width, height: spec.height, shape: spec.shape };
        let transport = Box::new(Self {
            writer,
            touch_stop,
            touch_join: Some(touch_join),
            link_dead,
            ota_status_rx,
            device_info_rx,
            _device: device,
            _interface: interface,
        });
        log::info!(
            "pico-view backend open ({PV_NAME} {PV_VID:04x}:{PV_PID:04x}, index {})",
            cfg.index
        );
        Ok((transport, geom, identity))
    }

    /// Force a USB-level reset of the device without claiming it. The worker
    /// calls this when repeated reopen attempts fail: a bus reset re-enumerates
    /// the device, clearing a halted endpoint or a firmware framer that desynced
    /// when the host tore the link down mid-frame — states a bare re-`open`/claim
    /// can't clear because they don't re-enumerate. Best-effort; the error is
    /// returned for the caller to log, and the device re-enumerates asynchronously.
    pub fn reset_device(cfg: &PicoViewConfig) -> Result<(), String> {
        let info = find_device(cfg.index, cfg.serial.as_deref())?;
        let device = info.open().wait().map_err(|e| format!("USB open failed: {e}"))?;
        device.reset().wait().map_err(|e| format!("USB reset failed: {e}"))
    }

    /// Block up to `timeout` for the next OTA status from the reader thread,
    /// waking every slice to honour `cancel` (set by `pv_close`) so a stalled
    /// device can't pin the worker — and the app's dispose — for the full
    /// timeout.
    fn wait_status(&self, timeout: Duration, cancel: &AtomicBool) -> Result<wire::OtaStatus, String> {
        const SLICE: Duration = Duration::from_millis(100);
        let deadline = Instant::now() + timeout;
        loop {
            if cancel.load(Ordering::Relaxed) {
                return Err("update cancelled".to_string());
            }
            match self.ota_status_rx.recv_timeout(SLICE) {
                Ok(s) => return Ok(s),
                // The reader thread dropped its sender: the bulk-IN reader has
                // died (device unplugged / bus error), so no status will ever
                // arrive. `recv_timeout` returns this *immediately*, so treat it
                // as a hard failure — matching it under `Err(_) if .. < deadline`
                // would busy-spin a core until the (up to 30s) deadline.
                Err(RecvTimeoutError::Disconnected) => {
                    return Err("device link dropped while waiting for OTA status".to_string());
                }
                Err(RecvTimeoutError::Timeout) if Instant::now() < deadline => continue,
                Err(RecvTimeoutError::Timeout) => {
                    return Err("timed out waiting for OTA status from device".to_string());
                }
            }
        }
    }

    /// Post one intermediate progress status to Dart. A device-reported failure
    /// is *not* posted here — it is returned as an [`OtaFail`] so the single
    /// [`Self::ota`] funnel posts exactly one terminal `Failed` (avoiding a
    /// double terminal event).
    fn report_status(&self, s: &wire::OtaStatus) -> Result<(), OtaFail> {
        if s.state() == wire::OtaState::Failed {
            return Err(OtaFail {
                msg: format!("device reported update failure (err {})", s.err),
                pct: s.pct,
                code: s.err,
            });
        }
        crate::post::post_ota_status(s.state(), s.pct, s.err);
        Ok(())
    }

    /// Run one OTA transfer end to end. Every failure path returns an [`OtaFail`]
    /// carrying the message + progress + err code; the [`Self::ota`] wrapper turns
    /// that into the single terminal `Failed` event. Intermediate progress
    /// (`Receiving`/`Verifying`) and the terminal `Done` are posted here.
    fn ota_run(&mut self, image: &[u8], cancel: &AtomicBool) -> Result<(), OtaFail> {
        if image.is_empty() {
            return Err(ota_host_fail("firmware image is empty".to_string()));
        }
        // Discard any status left over from a previous (aborted) attempt.
        while self.ota_status_rx.try_recv().is_ok() {}

        let sha: [u8; 32] = {
            let mut h = Sha256::new();
            h.update(image);
            h.finalize().into()
        };
        let version = app_version_from_image(image);

        log::info!("ESP32-P4 OTA: {} bytes, version '{version}'; streaming", image.len());
        write_ctrl(
            &mut self.writer,
            host_to_device::Msg::OtaBegin(wire::OtaBegin {
                image_size: image.len() as u32,
                sha256: sha.to_vec(),
                version,
            }),
        )
        .map_err(|e| ota_host_fail(format!("OTA_BEGIN write failed: {e}")))?;
        self.writer
            .flush()
            .map_err(|e| ota_host_fail(format!("OTA_BEGIN flush failed: {e}")))?;

        // The device erases the target slot before acking, so allow generous time.
        let st = self.wait_status(OTA_BEGIN_TIMEOUT, cancel).map_err(ota_host_fail)?;
        if st.state() == wire::OtaState::Failed {
            return Err(OtaFail {
                msg: format!("device rejected update (err {})", st.err),
                pct: 0,
                code: st.err,
            });
        }
        crate::post::post_ota_status(wire::OtaState::Receiving, 0, 0);

        // Stream the image; each chunk flush blocks until it's on the wire, so the
        // device's bulk-OUT backpressure paces us. Drain acked progress as we go.
        for (seq, chunk) in image.chunks(OTA_CHUNK_MAX).enumerate() {
            if cancel.load(Ordering::Relaxed) {
                // Best-effort: tell the device to discard the partial image.
                let _ = write_ctrl(
                    &mut self.writer,
                    host_to_device::Msg::OtaAbort(wire::OtaAbort {}),
                );
                let _ = self.writer.flush();
                return Err(ota_host_fail("update cancelled".to_string()));
            }
            write_ctrl(
                &mut self.writer,
                host_to_device::Msg::OtaData(wire::OtaData {
                    seq: seq as u32,
                    data: chunk.to_vec(),
                }),
            )
            .map_err(|e| ota_host_fail(format!("OTA_DATA write failed: {e}")))?;
            self.writer
                .flush()
                .map_err(|e| ota_host_fail(format!("OTA_DATA flush failed: {e}")))?;
            while let Ok(s) = self.ota_status_rx.try_recv() {
                self.report_status(&s)?;
            }
        }

        // OtaEnd: device verifies hash + signature, sets the boot slot, reboots.
        write_ctrl(&mut self.writer, host_to_device::Msg::OtaEnd(wire::OtaEnd {}))
            .map_err(|e| ota_host_fail(format!("OTA_END write failed: {e}")))?;
        self.writer
            .flush()
            .map_err(|e| ota_host_fail(format!("OTA_END flush failed: {e}")))?;

        loop {
            let s = self.wait_status(OTA_END_TIMEOUT, cancel).map_err(ota_host_fail)?;
            match s.state() {
                wire::OtaState::Done => {
                    crate::post::post_ota_status(wire::OtaState::Done, 100, 0);
                    log::info!("ESP32-P4 OTA complete; device rebooting into new image");
                    return Ok(());
                }
                wire::OtaState::Failed => {
                    return Err(OtaFail {
                        msg: format!("update failed (err {})", s.err),
                        pct: s.pct,
                        code: s.err,
                    });
                }
                // VERIFYING / late RECEIVING progress: report and keep waiting.
                _ => self.report_status(&s)?,
            }
        }
    }
}

impl Transport for Esp32P4Transport {
    fn flush(&mut self, rects: Vec<(Rect, Vec<u8>)>) -> Result<(), String> {
        if rects.is_empty() {
            return Ok(());
        }
        let last = rects.len() - 1;
        for (i, (rect, pixels)) in rects.iter().enumerate() {
            let flags = if i == last { FLAG_PRESENT } else { 0 };
            write_blit(&mut self.writer, *rect, pixels, flags)
                .map_err(|e| format!("BLIT write failed: {e}"))?;
        }
        // Block until the frame is on the wire — backpressure for the worker's
        // newest-wins coalescing (a synchronous flush).
        self.writer.flush().map_err(|e| format!("frame flush failed: {e}"))
    }

    // Touch itself is async (reader thread); the tick only surfaces reader
    // death so the worker's link supervisor engages without waiting for a
    // frame flush to fail.
    fn poll(&mut self) -> Result<(), String> {
        if self.link_dead.load(Ordering::Relaxed) {
            Err("bulk IN reader stopped (device unplugged?)".to_string())
        } else {
            Ok(())
        }
    }

    fn ota(&mut self, image: &[u8], cancel: &AtomicBool) -> Result<(), String> {
        match self.ota_run(image, cancel) {
            Ok(()) => Ok(()),
            Err(f) => {
                crate::post::post_ota_status(wire::OtaState::Failed, f.pct, f.code);
                Err(f.msg)
            }
        }
    }

    fn get_device_info(&mut self) -> Result<wire::DeviceInfo, String> {
        while self.device_info_rx.try_recv().is_ok() {}
        write_ctrl(
            &mut self.writer,
            host_to_device::Msg::GetDeviceInfo(wire::GetDeviceInfo {}),
        )
        .map_err(|e| format!("GET_DEVICE_INFO write failed: {e}"))?;
        self.writer
            .flush()
            .map_err(|e| format!("GET_DEVICE_INFO flush failed: {e}"))?;
        // The reader thread decodes the DeviceInfo reply and forwards it here.
        match self.device_info_rx.recv_timeout(DEVICE_INFO_TIMEOUT) {
            Ok(info) => Ok(info),
            Err(RecvTimeoutError::Timeout) => {
                Err("timed out waiting for DeviceInfo from device".to_string())
            }
            Err(RecvTimeoutError::Disconnected) => {
                Err("device link dropped while waiting for DeviceInfo".to_string())
            }
        }
    }

    fn set_brightness(&mut self, brightness: u8) -> Result<(), String> {
        write_ctrl(
            &mut self.writer,
            host_to_device::Msg::SetParam(wire::SetParam {
                param: Some(wire::set_param::Param::Brightness(brightness as u32)),
            }),
        )
        .map_err(|e| format!("SET_PARAM write failed: {e}"))?;
        self.writer.flush().map_err(|e| format!("SET_PARAM flush failed: {e}"))?;
        log::debug!("ESP32-P4: set backlight brightness {brightness}");
        Ok(())
    }

    fn haptics(&mut self, cmd: wire::Haptics) -> Result<(), String> {
        write_ctrl(&mut self.writer, host_to_device::Msg::Haptics(cmd))
            .map_err(|e| format!("HAPTICS write failed: {e}"))?;
        self.writer.flush().map_err(|e| format!("HAPTICS flush failed: {e}"))?;
        log::debug!("ESP32-P4: haptics command sent");
        Ok(())
    }

    fn keepalive(&mut self) -> Result<(), String> {
        write_ctrl(&mut self.writer, host_to_device::Msg::Keepalive(wire::Keepalive {}))
            .map_err(|e| format!("KEEPALIVE write failed: {e}"))?;
        self.writer.flush().map_err(|e| format!("KEEPALIVE flush failed: {e}"))?;
        Ok(())
    }
}

/// Best-effort extraction of the app version from an ESP-IDF app image. The
/// `esp_app_desc_t` sits right after the image + first-segment headers (file
/// offset `0x20`): a magic word at `0x20` and a 32-byte `version` field at `0x30`.
/// Returns an empty string if the layout doesn't match — the device only logs it.
fn app_version_from_image(image: &[u8]) -> String {
    const DESC_OFF: usize = 0x20;
    const DESC_MAGIC: u32 = 0xABCD_5432;
    const VER_OFF: usize = DESC_OFF + 16;
    if image.len() < VER_OFF + 32 {
        return String::new();
    }
    let magic = u32::from_le_bytes([
        image[DESC_OFF],
        image[DESC_OFF + 1],
        image[DESC_OFF + 2],
        image[DESC_OFF + 3],
    ]);
    if magic != DESC_MAGIC {
        return String::new();
    }
    let raw = &image[VER_OFF..VER_OFF + 32];
    let end = raw.iter().position(|&b| b == 0).unwrap_or(raw.len());
    String::from_utf8_lossy(&raw[..end]).into_owned()
}

impl Drop for Esp32P4Transport {
    fn drop(&mut self) {
        self.touch_stop.store(true, Ordering::Relaxed);
        if let Some(join) = self.touch_join.take() {
            let _ = join.join();
        }
    }
}

// --- Attestation ---------------------------------------------------------------

/// Challenge the device to prove it is genuine vendor hardware (see
/// [`crate::auth`]) and return the attested device id.
///
/// Only called when the device advertises the `auth` capability. **The `Err` is
/// not fatal**: [`open`] logs it and carries on with an unverified device.
fn attest(
    writer: &mut EndpointWrite<Bulk>,
    reader: &mut EndpointRead<Bulk>,
    framer: &mut Framer,
) -> Result<String, String> {
    let mut nonce = [0u8; auth::NONCE_LEN];
    getrandom::fill(&mut nonce).map_err(|e| format!("nonce generation failed: {e}"))?;
    write_ctrl(
        writer,
        host_to_device::Msg::AuthChallenge(wire::AuthChallenge { nonce: nonce.to_vec() }),
    )
    .map_err(|e| format!("AUTH_CHALLENGE write failed: {e}"))?;
    writer.flush().map_err(|e| format!("AUTH_CHALLENGE flush failed: {e}"))?;

    reader.set_read_timeout(AUTH_TIMEOUT);
    // Deadline across the whole exchange: skipped stale messages each reset the
    // read timeout, so without it a device that floods Touch/OtaStatus could
    // hold `open` here indefinitely -- and this step is not worth waiting on.
    let deadline = Instant::now() + AUTH_TIMEOUT;
    let response = loop {
        if Instant::now() > deadline {
            return Err("device did not answer the attestation challenge in time".to_string());
        }
        match read_ctrl(reader, framer, deadline) {
            Ok(Some(device_to_host::Msg::AuthResponse(r))) => break r,
            // A stale Touch/OtaStatus message may still be in flight; skip it.
            Ok(Some(other)) => {
                log::debug!("ESP32-P4: skipping {} while awaiting AUTH_RESPONSE", msg_name(&other))
            }
            Ok(None) => return Err("device did not answer the attestation challenge".to_string()),
            Err(e) => return Err(format!("AUTH_RESPONSE read failed: {e}")),
        }
    };

    match response.status() {
        wire::AuthStatus::Ok => {
            auth::verify_attestation(&nonce, &response.certificate, &response.signature)
        }
        other => Err(format!("device reported {}", auth_status_name(other))),
    }
}

fn auth_status_name(status: wire::AuthStatus) -> &'static str {
    match status {
        wire::AuthStatus::Ok => "ok",
        wire::AuthStatus::Unprovisioned => "unprovisioned",
        wire::AuthStatus::SigningError => "internal signing error",
        wire::AuthStatus::MalformedChallenge => "malformed challenge",
        wire::AuthStatus::Unspecified => "no status",
    }
}

/// Short name of a device→host message for skip/trace logs.
fn msg_name(msg: &device_to_host::Msg) -> &'static str {
    match msg {
        device_to_host::Msg::HelloAck(_) => "HelloAck",
        device_to_host::Msg::Touch(_) => "Touch",
        device_to_host::Msg::OtaStatus(_) => "OtaStatus",
        device_to_host::Msg::AuthResponse(_) => "AuthResponse",
        device_to_host::Msg::ConfigAck(_) => "ConfigAck",
        device_to_host::Msg::DeviceInfo(_) => "DeviceInfo",
        device_to_host::Msg::ParamAck(_) => "ParamAck",
    }
}

// --- Message framing ---------------------------------------------------------

/// Encode one [`wire::HostToDevice`] and write it as a CTRL frame (buffered;
/// the caller flushes at the message/frame boundary).
fn write_ctrl(w: &mut EndpointWrite<Bulk>, msg: host_to_device::Msg) -> std::io::Result<()> {
    let payload = wire::HostToDevice { msg: Some(msg) }.encode_to_vec();
    w.write_all(&header(MSG_CTRL, 0, payload.len()))?;
    w.write_all(&payload)?;
    Ok(())
}

/// Write a BLIT message: header + `x,y,w,h` (u16 LE) + RGB565-BE pixels. The
/// pixels are written directly (no extra copy into a combined buffer).
fn write_blit(
    w: &mut EndpointWrite<Bulk>,
    rect: Rect,
    pixels: &[u8],
    flags: u16,
) -> std::io::Result<()> {
    let payload_len = 8 + pixels.len();
    w.write_all(&header(MSG_BLIT, flags, payload_len))?;
    let mut geom = [0u8; 8];
    geom[0..2].copy_from_slice(&(rect.x as u16).to_le_bytes());
    geom[2..4].copy_from_slice(&(rect.y as u16).to_le_bytes());
    geom[4..6].copy_from_slice(&(rect.w as u16).to_le_bytes());
    geom[6..8].copy_from_slice(&(rect.h as u16).to_le_bytes());
    w.write_all(&geom)?;
    w.write_all(pixels)?;
    Ok(())
}

/// Build the 12-byte little-endian PVUS header.
fn header(typ: u16, flags: u16, payload_len: usize) -> [u8; PV_HEADER_LEN] {
    let mut h = [0u8; PV_HEADER_LEN];
    h[0..4].copy_from_slice(&PV_MAGIC.to_le_bytes());
    h[4..6].copy_from_slice(&typ.to_le_bytes());
    h[6..8].copy_from_slice(&flags.to_le_bytes());
    h[8..12].copy_from_slice(&(payload_len as u32).to_le_bytes());
    h
}

/// Build the CTRL `Config` message from the panel preset.
fn build_config(spec: &PanelSpec) -> Result<wire::Config, String> {
    let model = match spec.driver {
        "st77916" => wire::PanelModel::St77916,
        "st7789" => wire::PanelModel::St7789,
        other => return Err(format!("driver '{other}' has no ESP32-P4 panel model id")),
    };
    let touch_addr = if spec.touch == "none" { 0 } else { spec.touch_addr as u32 };
    Ok(wire::Config {
        model: model as i32,
        width: spec.width,
        height: spec.height,
        x_offset: spec.x_offset as u32,
        y_offset: spec.y_offset as u32,
        rotation_deg: spec.rotation,
        invert: spec.invert,
        touch_addr,
        touch_swap_xy: spec.touch_swap_xy,
        touch_flip_x: spec.touch_flip_x,
        touch_flip_y: spec.touch_flip_y,
    })
}

/// Incremental reassembler for PVUS frames arriving on the bulk IN stream.
struct Framer {
    buf: Vec<u8>,
}

impl Framer {
    fn new() -> Self {
        Self { buf: Vec::with_capacity(IN_XFER) }
    }

    fn push(&mut self, data: &[u8]) {
        self.buf.extend_from_slice(data);
    }

    /// Pop the next complete `(type, flags, payload)`, resyncing on the magic if
    /// the stream desyncs. Returns `None` when more bytes are needed.
    fn next(&mut self) -> Option<(u16, u16, Vec<u8>)> {
        loop {
            if self.buf.len() < PV_HEADER_LEN {
                return None;
            }
            let magic = u32::from_le_bytes([self.buf[0], self.buf[1], self.buf[2], self.buf[3]]);
            if magic != PV_MAGIC {
                self.resync();
                continue;
            }
            let typ = u16::from_le_bytes([self.buf[4], self.buf[5]]);
            let flags = u16::from_le_bytes([self.buf[6], self.buf[7]]);
            let plen =
                u32::from_le_bytes([self.buf[8], self.buf[9], self.buf[10], self.buf[11]]) as usize;
            if plen > IN_MSG_MAX {
                log::warn!("ESP32-P4: framer desync (claimed payload {plen} bytes); resyncing");
                self.resync();
                continue;
            }
            if self.buf.len() < PV_HEADER_LEN + plen {
                return None;
            }
            let payload = self.buf[PV_HEADER_LEN..PV_HEADER_LEN + plen].to_vec();
            self.buf.drain(0..PV_HEADER_LEN + plen);
            return Some((typ, flags, payload));
        }
    }

    /// Discard bytes up to the next candidate magic ('P' = 0x50), or everything
    /// if none is buffered.
    fn resync(&mut self) {
        match self.buf[1..].iter().position(|&b| b == 0x50) {
            Some(off) => {
                self.buf.drain(0..off + 1);
            }
            None => self.buf.clear(),
        }
    }
}

/// Read until one complete frame is reassembled, or the read times out / errors.
/// Returns `Ok(None)` on timeout with nothing decoded.
///
/// Bounded by `deadline` (an absolute [`Instant`]): the per-read timeout on its
/// own doesn't cap total time here, because a read that returns *some* bytes
/// without completing a frame loops for another read. Without the deadline a
/// device could trickle one byte per sub-timeout window forever and never let
/// this return, stalling `open`.
fn read_frame(
    reader: &mut EndpointRead<Bulk>,
    framer: &mut Framer,
    deadline: Instant,
) -> std::io::Result<Option<(u16, u16, Vec<u8>)>> {
    if let Some(msg) = framer.next() {
        return Ok(Some(msg));
    }
    let mut chunk = [0u8; IN_XFER];
    loop {
        if Instant::now() >= deadline {
            return Ok(None);
        }
        match reader.read(&mut chunk) {
            Ok(0) => return Ok(None),
            Ok(n) => {
                framer.push(&chunk[..n]);
                if let Some(msg) = framer.next() {
                    return Ok(Some(msg));
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => return Ok(None),
            Err(e) => return Err(e),
        }
    }
}

/// Decode the [`wire::DeviceToHost`] payload of a CTRL frame. `None` for
/// non-CTRL frames, undecodable payloads, and unknown/absent oneof variants.
fn decode_ctrl(typ: u16, payload: &[u8]) -> Option<device_to_host::Msg> {
    if typ != MSG_CTRL {
        log::debug!("ESP32-P4: ignoring device frame type {typ}");
        return None;
    }
    match wire::DeviceToHost::decode(payload) {
        Ok(m) => {
            if m.msg.is_none() {
                log::debug!("ESP32-P4: CTRL message with no known variant; ignoring");
            }
            m.msg
        }
        Err(e) => {
            log::warn!("ESP32-P4: CTRL decode failed ({} bytes): {e}", payload.len());
            None
        }
    }
}

/// Read frames until one decodes to a device→host message, the `deadline`
/// passes (`Ok(None)`), or the bus errors. The deadline also caps the skip loop
/// so a device flooding stale/unknown frames can't hold the handshake open past
/// it (see [`read_frame`]).
fn read_ctrl(
    reader: &mut EndpointRead<Bulk>,
    framer: &mut Framer,
    deadline: Instant,
) -> std::io::Result<Option<device_to_host::Msg>> {
    loop {
        match read_frame(reader, framer, deadline)? {
            Some((typ, _flags, payload)) => {
                if let Some(msg) = decode_ctrl(typ, &payload) {
                    return Ok(Some(msg));
                }
                // Skippable frame (stale/unknown); keep reading until the
                // deadline rather than spinning on a flood indefinitely.
                if Instant::now() >= deadline {
                    return Ok(None);
                }
            }
            None => return Ok(None),
        }
    }
}

/// Background loop: decode device→host messages off the bulk IN stream —
/// `Touch` messages forwarded to the Dart stream, `OtaStatus` messages forwarded
/// to an in-progress [`Esp32P4Transport::ota`] over `ota_tx`, and `DeviceInfo`
/// replies forwarded to [`Esp32P4Transport::get_device_info`] over `info_tx`.
fn touch_loop(
    mut reader: EndpointRead<Bulk>,
    mut framer: Framer,
    stop: Arc<AtomicBool>,
    dead: Arc<AtomicBool>,
    ota_tx: Sender<wire::OtaStatus>,
    info_tx: Sender<wire::DeviceInfo>,
) {
    let mut chunk = [0u8; IN_XFER];
    while !stop.load(Ordering::Relaxed) {
        match reader.read(&mut chunk) {
            Ok(0) => {}
            Ok(n) => {
                framer.push(&chunk[..n]);
                while let Some((typ, _flags, payload)) = framer.next() {
                    match decode_ctrl(typ, &payload) {
                        Some(device_to_host::Msg::Touch(t)) => handle_touch(&t),
                        Some(device_to_host::Msg::OtaStatus(s)) => {
                            let _ = ota_tx.send(s);
                        }
                        Some(device_to_host::Msg::DeviceInfo(i)) => {
                            let _ = info_tx.send(i);
                        }
                        Some(other) => {
                            log::debug!("ESP32-P4: ignoring {}", msg_name(&other))
                        }
                        None => {}
                    }
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {}
            Err(e) => {
                log::warn!("ESP32-P4 touch read failed; marking link dead: {e}");
                dead.store(true, Ordering::Relaxed);
                return;
            }
        }
    }
}

/// Forward one decoded `Touch` through the shared touch event path. The
/// firmware already runs the down/move/up state machine and axis transforms and
/// reports (0,0) on release, so this is a pure forward to the Dart touch stream.
fn handle_touch(t: &wire::Touch) {
    let phase = t.phase();
    if phase == wire::TouchPhase::Unspecified {
        log::warn!("ESP32-P4: touch with unspecified phase; dropping");
        return;
    }
    crate::touch::emit(phase, t.x as u16, t.y as u16);
}

#[cfg(test)]
mod tests {
    use super::*;

    fn frame(typ: u16, flags: u16, payload: &[u8]) -> Vec<u8> {
        let mut v = header(typ, flags, payload.len()).to_vec();
        v.extend_from_slice(payload);
        v
    }

    fn touch_msg(phase: wire::TouchPhase, x: u32, y: u32) -> Vec<u8> {
        wire::DeviceToHost {
            msg: Some(device_to_host::Msg::Touch(wire::Touch { phase: phase as i32, x, y })),
        }
        .encode_to_vec()
    }

    #[test]
    fn framer_decodes_one_frame() {
        let payload = touch_msg(wire::TouchPhase::Down, 1, 2);
        let mut f = Framer::new();
        f.push(&frame(MSG_CTRL, 0, &payload));
        assert_eq!(f.next(), Some((MSG_CTRL, 0, payload)));
        assert_eq!(f.next(), None);
    }

    #[test]
    fn framer_reassembles_across_pushes() {
        let payload = touch_msg(wire::TouchPhase::Move, 42, 7);
        let m = frame(MSG_CTRL, 0, &payload);
        let mut f = Framer::new();
        for b in &m {
            assert_eq!(f.next(), None);
            f.push(std::slice::from_ref(b));
        }
        assert_eq!(f.next(), Some((MSG_CTRL, 0, payload)));
    }

    #[test]
    fn framer_resyncs_on_garbage_prefix() {
        let payload = touch_msg(wire::TouchPhase::Up, 0, 0);
        let mut f = Framer::new();
        f.push(&[0xDE, 0xAD, 0x50, 0xBE, 0xEF]); // includes a decoy 'P'
        f.push(&frame(MSG_CTRL, 0, &payload));
        assert_eq!(f.next(), Some((MSG_CTRL, 0, payload)));
    }

    #[test]
    fn framer_rejects_oversize_length_and_recovers() {
        // A desynced "header" claiming a 4 GiB payload must not stall the stream.
        let mut bad = header(MSG_CTRL, 0, 0).to_vec();
        bad[8..12].copy_from_slice(&u32::MAX.to_le_bytes());
        let payload = touch_msg(wire::TouchPhase::Down, 3, 4);
        let mut f = Framer::new();
        f.push(&bad);
        f.push(&frame(MSG_CTRL, 0, &payload));
        assert_eq!(f.next(), Some((MSG_CTRL, 0, payload)));
    }

    #[test]
    fn framer_handles_back_to_back_frames() {
        let p1 = touch_msg(wire::TouchPhase::Down, 1, 1);
        let p2 = touch_msg(wire::TouchPhase::Up, 0, 0);
        let mut f = Framer::new();
        let mut both = frame(MSG_CTRL, 0, &p1);
        both.extend_from_slice(&frame(MSG_CTRL, 0, &p2));
        f.push(&both);
        assert_eq!(f.next(), Some((MSG_CTRL, 0, p1)));
        assert_eq!(f.next(), Some((MSG_CTRL, 0, p2)));
        assert_eq!(f.next(), None);
    }

    #[test]
    fn framer_empty_payload_frame() {
        let mut f = Framer::new();
        f.push(&frame(MSG_CTRL, 0, &[]));
        assert_eq!(f.next(), Some((MSG_CTRL, 0, vec![])));
    }

    #[test]
    fn decode_ctrl_roundtrips_touch() {
        let payload = touch_msg(wire::TouchPhase::Move, 120, 240);
        match decode_ctrl(MSG_CTRL, &payload) {
            Some(device_to_host::Msg::Touch(t)) => {
                assert_eq!(t.phase(), wire::TouchPhase::Move);
                assert_eq!((t.x, t.y), (120, 240));
            }
            other => panic!("unexpected decode: {other:?}"),
        }
    }

    #[test]
    fn decode_ctrl_ignores_unknown_variant_and_garbage() {
        // An empty HostToDevice-style payload decodes to no variant.
        assert_eq!(decode_ctrl(MSG_CTRL, &[]), None);
        // Garbage fails to decode without panicking.
        assert_eq!(decode_ctrl(MSG_CTRL, &[0xFF, 0xFF, 0xFF]), None);
        // Non-CTRL frame types are not decoded at all.
        let payload = touch_msg(wire::TouchPhase::Down, 1, 2);
        assert_eq!(decode_ctrl(MSG_BLIT, &payload), None);
    }

    #[test]
    fn config_from_default_panel_spec() {
        let spec = crate::panels::resolve(crate::panels::DEFAULT_MODEL).unwrap();
        let c = build_config(&spec).unwrap();
        assert_eq!(c.model, wire::PanelModel::St77916 as i32);
        assert_eq!((c.width, c.height), (spec.width, spec.height));
        assert!(c.touch_addr > 0);
    }
}
