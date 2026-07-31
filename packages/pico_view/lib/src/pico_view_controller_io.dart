/// The native [PicoViewController]: owns the FFI lifecycle (init / open / flush /
/// event channel) for the native pico_view bridge.
///
/// The control plane is protobuf over one generic call: requests are encoded
/// `PvRequest` messages through `pv_request`, and everything the engine sends
/// back — touch, link state and OTA progress, plus the `PvResponse` answering
/// each request — arrives on the `pv_init` SendPort as encoded `PvEvent` bytes.
/// Only frame delivery (`pv_lcd_flush`) bypasses the message channel.
///
/// `pv_request` returns as soon as the engine accepts a request, so opening a
/// device never parks the calling isolate on USB. Each request carries an `id`
/// that its response echoes; [PicoViewController] keeps a [Completer] per
/// outstanding id and completes the matching [Future] when the answer lands.
/// That is why every device call here is asynchronous — [PicoViewController.flushRgba]
/// excepted, which stays synchronous because it is the per-frame hot path.
library;

import 'dart:async';
import 'dart:ffi' as ffi;
import 'dart:isolate';

import 'package:ffi/ffi.dart';
import 'package:flutter/foundation.dart';

import 'gen/pv_ffi.pb.dart' as pb;
import 'gen/pv_wire.pb.dart' as pbw;
import 'pico_view_bindings_generated.dart' as bindings;
import 'pico_view_types.dart';

/// Owns the native bridge. Create one, [init] it once, then [open] a device.
///
/// The native side keeps a single device + SendPort, so use a single controller
/// per app.
class PicoViewController {
  final ReceivePort _rx = ReceivePort();
  final StreamController<PicoTouchEvent> _touch =
      StreamController<PicoTouchEvent>.broadcast();
  final StreamController<PicoLinkState> _link =
      StreamController<PicoLinkState>.broadcast();
  final StreamController<PicoOtaEvent> _ota =
      StreamController<PicoOtaEvent>.broadcast();

  bool _initialized = false;
  bool _opened = false;
  bool _disposed = false;

  /// Outstanding requests, keyed by the `id` their response will carry.
  final Map<int, Completer<pb.PvResponse>> _pending = {};

  /// Next request id. Starts at 1 because 0 means "no answer wanted" on the
  /// native side; wraps well before it could collide with a live request.
  int _nextId = 1;

  /// How long to wait for a response beyond the engine's own deadline. The
  /// engine always answers — timeouts included — so this only catches a
  /// response that was never posted at all (an engine crash, a closed port).
  static const Duration _responseGrace = Duration(seconds: 5);

  /// Deadlines sent to the engine as `PvRequest.timeout_ms` for the three
  /// requests that round-trip to the device. Passing them explicitly (rather
  /// than sending 0 and inheriting the engine's own defaults) keeps the wait
  /// this side is prepared for and the wait the engine enforces in step —
  /// these mirror `OPEN_TIMEOUT`, `CLOSE_TIMEOUT` and `DEVICE_INFO_TIMEOUT` in
  /// the engine's `worker.rs`.
  static const Duration _openTimeout = Duration(seconds: 10);
  static const Duration _closeTimeout = Duration(seconds: 5);
  static const Duration _deviceInfoTimeout = Duration(seconds: 2);

  PicoLinkState _linkState = PicoLinkState.disconnected;

  /// The connected device's firmware version, reported by the engine on the
  /// CONNECTED link event; `null` while disconnected or when the device didn't
  /// report one.
  String? _firmwareVersion;

  /// Whether the connected device proved it is genuine vendor hardware.
  bool _deviceVerified = false;

  /// The attested device id, set only when [_deviceVerified].
  String? _deviceId;

  PicoViewConfig _config = const PicoViewConfig();

  /// Reusable frame buffer (grows once, freed in [dispose]). Safe to reuse
  /// because the copy → FFI-call critical section is synchronous.
  ffi.Pointer<ffi.Uint8>? _frameBuffer;
  int _frameBufferCap = 0;

  /// When true, an external frame producer is pushing frames straight to the
  /// panel via [flushRgba] — a video decoder, say — so the `PicoView` mirror
  /// loop must pause its own captures to avoid two writers fighting over the
  /// panel. Set it before the producer's first frame and clear it when the
  /// producer is done; the mirror then re-sends the current subtree.
  bool suspendCapture = false;

  /// Physical-touch events in LCD pixel coordinates.
  Stream<PicoTouchEvent> get touches => _touch.stream;

  /// Link-state transitions (connected / disconnected). The native engine
  /// reconnects on its own; listen here to reflect it in the UI.
  Stream<PicoLinkState> get linkStates => _link.stream;

  /// The most recent link state (kept current from [linkStates]).
  PicoLinkState get linkState => _linkState;

  /// The connected device's firmware version, or `null` while disconnected (or
  /// when the device runs firmware too old to report one).
  String? get firmwareVersion => _firmwareVersion;

  /// Whether the connected device proved it is genuine vendor hardware.
  bool get deviceVerified => _deviceVerified;

  /// The attested device id (e.g. `PV4-A00123`), or `null` when the device is
  /// disconnected or not [deviceVerified].
  String? get attestedDeviceId => _deviceId;

  /// Firmware-update progress/result events (see [otaStart]).
  Stream<PicoOtaEvent> get otaEvents => _ota.stream;

  /// The currently-open device config (geometry used by `PicoView`).
  PicoViewConfig get config => _config;

  /// Whether [open] succeeded. For the *live* device state, use [linkState]:
  /// the engine keeps reconnecting behind this flag.
  bool get isOpen => _opened;

  /// Wire up the Dart DL API + SendPort. Call once before [open]. Throws
  /// [PicoViewException] when the Dart DL API handshake fails (a native
  /// library / SDK version mismatch) — no events would ever be delivered.
  void init() {
    if (_initialized) return;
    final rc = bindings.pv_init(
      ffi.NativeApi.initializeApiDLData,
      _rx.sendPort.nativePort,
    );
    if (rc != 0) {
      throw PicoViewException(
        'pv_init failed: Dart DL API version mismatch (code $rc)',
        code: rc,
      );
    }
    _rx.listen(_onMessage);
    _initialized = true;
  }

  /// Send one encoded control-plane request and await its response.
  ///
  /// The native call only *accepts* the request; the answer arrives later on
  /// the SendPort tagged with the id assigned here, and [_onMessage] completes
  /// the future. Throws [PicoViewException] when the request could not be
  /// handed over at all, or when no answer arrived in time.
  ///
  /// [engineTimeout] is the deadline the engine is asked to enforce, for the
  /// requests that round-trip to the device; leave it null for the ones that
  /// answer immediately.
  Future<pb.PvResponse> _request(pb.PvRequest req, {Duration? engineTimeout}) {
    if (_disposed) {
      throw PicoViewException('controller disposed', code: -1);
    }
    if (!_initialized) init();

    final id = _nextId;
    // Skip 0 on wrap: the engine reads it as "run this, answer nobody".
    _nextId = _nextId == 0xFFFFFFFF ? 1 : _nextId + 1;
    req.id = id;
    if (engineTimeout != null) {
      req.timeoutMs = engineTimeout.inMilliseconds;
    }

    final reqBytes = req.writeToBuffer();
    final reqPtr = malloc.allocate<ffi.Uint8>(reqBytes.length);
    final completer = Completer<pb.PvResponse>();
    _pending[id] = completer;
    try {
      reqPtr.asTypedList(reqBytes.length).setAll(0, reqBytes);
      final rc = bindings.pv_request(reqPtr, reqBytes.length);
      if (rc != 0) {
        _pending.remove(id);
        throw PicoViewException('pv_request rejected (code $rc)', code: rc);
      }
    } finally {
      malloc.free(reqPtr);
    }

    // The engine's own deadline plus a grace margin: it answers on time or
    // answers ERROR_CODE_TIMEOUT, so this only fires if nothing came back.
    final deadline = (engineTimeout ?? Duration.zero) + _responseGrace;
    return completer.future.timeout(
      deadline,
      onTimeout: () {
        _pending.remove(id);
        throw PicoViewException(
          'no response from the engine after $deadline',
          code: pb.ErrorCode.ERROR_CODE_TIMEOUT.value,
        );
      },
    );
  }

  /// Throw the matching exception for an `error` response.
  Never _throwError(pb.Error error, String op) {
    throw PicoViewException(
      '$op failed: ${error.message} (${error.code.name})',
      code: error.code.value,
    );
  }

  /// Open the panel described by [config] and start the device worker,
  /// calling [init] first if it hasn't run.
  ///
  /// Completes once the device is open and the panel initialized, so a normal
  /// return does mean a panel is attached; the engine gives up after ~10s. The
  /// wait happens on the engine's own thread, so awaiting this does not stall
  /// the isolate. [linkState] is [PicoLinkState.connected] on return, and a
  /// matching transition is also delivered on [linkStates].
  ///
  /// The link can still drop afterwards — the engine reconnects on its own —
  /// so keep watching [linkStates] for the live state rather than assuming a
  /// successful open holds.
  ///
  /// Throws [PicoViewException] when no device could be opened.
  Future<void> open(PicoViewConfig config) async {
    if (!_initialized) init();
    pb.PvRequest openReq() =>
        pb.PvRequest(openDevice: pb.OpenDevice(model: config.model));

    var resp = await _request(openReq(), engineTimeout: _openTimeout);
    if (resp.whichResp() == pb.PvResponse_Resp.error &&
        resp.error.code == pb.ErrorCode.ERROR_CODE_ALREADY_OPEN &&
        !_opened) {
      // The native worker survived a hot restart (this controller never
      // opened it). Tear the stale one down and retry once. A fresh request
      // each time: ids are stamped on send, so one can't be reused.
      await _request(
        pb.PvRequest(closeDevice: pb.CloseDevice()),
        engineTimeout: _closeTimeout,
      );
      resp = await _request(openReq(), engineTimeout: _openTimeout);
    }
    if (resp.whichResp() == pb.PvResponse_Resp.error) {
      _throwError(resp.error, 'open');
    }
    _config = config;
    _opened = true;
    _linkState = PicoLinkState.connected;
  }

  /// Push one tightly-packed RGBA8888 frame (`rgba.length == width*height*4`).
  /// Returns false if the device isn't open, the frame is empty, or the enqueue
  /// was rejected.
  bool flushRgba(Uint8List rgba, int width, int height) {
    // An empty frame has nothing to send, and would also leave `_frameBuffer`
    // unallocated below (0 is never `> _frameBufferCap`).
    if (_disposed || !_opened || rgba.isEmpty) return false;
    if (rgba.length > _frameBufferCap) {
      if (_frameBuffer != null) malloc.free(_frameBuffer!);
      _frameBuffer = malloc.allocate<ffi.Uint8>(rgba.length);
      _frameBufferCap = rgba.length;
    }
    _frameBuffer!.asTypedList(rgba.length).setAll(0, rgba);
    return bindings.pv_lcd_flush(_frameBuffer!, rgba.length, width, height) ==
        0;
  }

  /// Set the panel backlight level, 0 (off) – 255 (full).
  ///
  /// The engine queues the command and acks without waiting for the device, so
  /// this completes on the next turn of the event loop. `true` means queued,
  /// not applied.
  Future<bool> setBrightness(int level) async {
    if (_disposed || !_opened) return false;
    final clamped = level.clamp(0, 255);
    final resp = await _request(
      pb.PvRequest(setParam: pbw.SetParam(brightness: clamped)),
    );
    return resp.whichResp() != pb.PvResponse_Resp.error;
  }

  /// Play one built-in DRV2605L haptic effect ([effect] is a ROM waveform id,
  /// 1–123). [library] picks the ROM library (1–7); 0 keeps the firmware
  /// default (the LRA library). Queued, not awaited on the device — see
  /// [setBrightness].
  Future<bool> playHaptic(int effect, {int library = 0}) async {
    if (_disposed || !_opened) return false;
    final resp = await _request(
      pb.PvRequest(
        haptics: pbw.Haptics(
          play: pbw.HapticsPlay(effect: effect, library: library),
        ),
      ),
    );
    return resp.whichResp() != pb.PvResponse_Resp.error;
  }

  /// Stop any haptic effect currently playing on the device. Best-effort; see
  /// [playHaptic].
  Future<bool> stopHaptic() async {
    if (_disposed || !_opened) return false;
    final resp = await _request(
      pb.PvRequest(haptics: pbw.Haptics(stop: pbw.HapticsStop())),
    );
    return resp.whichResp() != pb.PvResponse_Resp.error;
  }

  /// Ask the connected device to describe itself: serial, firmware version,
  /// panel geometry and capabilities.
  ///
  /// Unlike the fire-and-forget commands this round-trips to the device, so it
  /// takes a moment to complete (sub-second in practice; the engine gives up
  /// after a couple of seconds). Query it after a CONNECTED transition on
  /// [linkStates] rather than caching it across a reconnect — a different unit
  /// may have been plugged in.
  ///
  /// Throws [PicoViewException] when no device is open, the link is down, or
  /// the device did not answer in time.
  Future<PicoDeviceInfo> getDeviceInfo() async {
    if (_disposed || !_opened) {
      throw PicoViewException('getDeviceInfo: device not open', code: -1);
    }
    final resp = await _request(
      pb.PvRequest(getDeviceInfo: pbw.GetDeviceInfo()),
      engineTimeout: _deviceInfoTimeout,
    );
    if (resp.whichResp() == pb.PvResponse_Resp.error) {
      _throwError(resp.error, 'getDeviceInfo');
    }
    return _toDeviceInfo(resp.deviceInfo);
  }

  /// Map the wire `DeviceInfo` onto the package's public type, so protobuf
  /// classes stay out of the API (as they do for touch / link / OTA events).
  static PicoDeviceInfo _toDeviceInfo(pbw.DeviceInfo info) {
    final panel = info.hasPanel() ? info.panel : null;
    return PicoDeviceInfo(
      deviceId: info.deviceId,
      serial: info.serial,
      firmwareVersion: info.fwVersion,
      protoVersion: info.protoVersion,
      panelWidth: panel?.width ?? 0,
      panelHeight: panel?.height ?? 0,
      panelShape: switch (panel?.shape) {
        pbw.PanelShape.PANEL_SHAPE_RECT => PicoPanelShape.rect,
        pbw.PanelShape.PANEL_SHAPE_ROUND => PicoPanelShape.round,
        _ => PicoPanelShape.unknown,
      },
      supportsBrightness: info.caps.setParam,
      supportsHaptics: info.caps.haptics,
      supportsAttestation: info.caps.auth,
    );
  }

  /// Stream a signed firmware image to the device. Fire-and-forget: progress
  /// and the result arrive on [otaEvents]; while it runs, frames are dropped
  /// and the device reboots into the new image on success (a
  /// disconnected→connected pair appears on [linkStates]).
  ///
  /// Throws [PicoViewException] if the update couldn't be enqueued (no device
  /// open, or the worker is gone). Completing means enqueued, not flashed.
  Future<void> otaStart(Uint8List image) async {
    if (_disposed || !_opened) {
      throw PicoViewException('otaStart: device not open', code: -1);
    }
    final resp = await _request(
      pb.PvRequest(otaStart: pb.OtaStart(image: image)),
    );
    if (resp.whichResp() == pb.PvResponse_Resp.error) {
      _throwError(resp.error, 'otaStart');
    }
  }

  /// Decode one `PvEvent` pushed from the native side and route it to the
  /// matching stream — or, for a response, to the request waiting on its id.
  /// Unknown variants are ignored so newer native libraries stay compatible
  /// with older Dart code.
  void _onMessage(dynamic raw) {
    if (raw is! Uint8List) return;
    final pb.PvEvent event;
    try {
      event = pb.PvEvent.fromBuffer(raw);
    } catch (_) {
      return;
    }
    switch (event.whichEvent()) {
      case pb.PvEvent_Event.response:
        _onResponse(event.response);
      case pb.PvEvent_Event.touch:
        _onTouch(event.touch);
      case pb.PvEvent_Event.link:
        final state = switch (event.link.state) {
          pb.LinkState.LINK_STATE_CONNECTED => PicoLinkState.connected,
          pb.LinkState.LINK_STATE_DISCONNECTED => PicoLinkState.disconnected,
          _ => null,
        };
        if (state != null) {
          _linkState = state;
          // The identity fields are only meaningful on CONNECTED; clear them
          // otherwise so a stale version or device id can't linger after unplug.
          final connected = state == PicoLinkState.connected;
          _firmwareVersion = connected && event.link.fwVersion.isNotEmpty
              ? event.link.fwVersion
              : null;
          _deviceVerified = connected && event.link.verified;
          _deviceId = connected && event.link.deviceId.isNotEmpty
              ? event.link.deviceId
              : null;
          _link.add(state);
        }
      case pb.PvEvent_Event.ota:
        _ota.add(
          PicoOtaEvent(
            switch (event.ota.state) {
              pbw.OtaState.OTA_STATE_RECEIVING => 'receiving',
              pbw.OtaState.OTA_STATE_VERIFYING => 'verifying',
              pbw.OtaState.OTA_STATE_DONE => 'done',
              pbw.OtaState.OTA_STATE_FAILED => 'failed',
              _ => 'unknown',
            },
            event.ota.pct,
            event.ota.err,
          ),
        );
      default:
        break;
    }
  }

  /// Hand one response to the request that is waiting for it. An id with no
  /// waiter is dropped: the request already timed out on this side, or the
  /// engine answered one that was never sent.
  void _onResponse(pb.PvResponse resp) {
    final completer = _pending.remove(resp.id);
    if (completer == null || completer.isCompleted) return;
    completer.complete(resp);
  }

  void _onTouch(pbw.Touch touch) {
    final phase = switch (touch.phase) {
      pbw.TouchPhase.TOUCH_PHASE_DOWN => TouchPhase.down,
      pbw.TouchPhase.TOUCH_PHASE_MOVE => TouchPhase.move,
      pbw.TouchPhase.TOUCH_PHASE_UP => TouchPhase.up,
      _ => null,
    };
    if (phase == null) return;
    _touch.add(PicoTouchEvent(phase, touch.x, touch.y));
  }

  /// Close the device and release all resources. Safe to call multiple times.
  ///
  /// Only tears down what *this* controller actually opened. The native engine
  /// is a process-wide singleton, so the device is closed only when [open]
  /// succeeded here — an unconditional close would tear down a device worker
  /// this controller never owned.
  ///
  /// Awaits the teardown, so the `ReceivePort` stays open long enough to
  /// receive the engine's answer. Callers that cannot await — a widget
  /// `dispose`, say — should use [disposeSync] instead.
  Future<void> dispose() async {
    if (_disposed) return;
    if (_opened) {
      try {
        await _request(
          pb.PvRequest(closeDevice: pb.CloseDevice()),
          engineTimeout: _closeTimeout,
        );
      } on PicoViewException {
        // Teardown is best-effort: an engine that won't answer is being
        // released anyway, and the rest of the cleanup still has to happen.
      }
      _opened = false;
    }
    _teardown();
  }

  /// Close the device and release all resources without awaiting.
  ///
  /// Blocks the calling isolate for as long as the teardown takes (up to ~5s
  /// in the worst case), which is the price of a synchronous close — prefer
  /// [dispose] wherever a `Future` can be awaited. Meant for the paths that
  /// have no isolate left to receive an answer: a widget `dispose`, or a
  /// last-resort cleanup on shutdown.
  void disposeSync() {
    if (_disposed) return;
    if (_opened) {
      bindings.pv_close();
      _opened = false;
    }
    _teardown();
  }

  /// Release everything held on this side. Shared by [dispose] and
  /// [disposeSync], both of which have already dealt with the device.
  void _teardown() {
    _disposed = true;
    // Nothing will answer these now — fail them rather than leave the callers
    // waiting for their own timeouts to fire.
    for (final completer in _pending.values) {
      if (!completer.isCompleted) {
        completer.completeError(
          PicoViewException('controller disposed', code: -1),
        );
      }
    }
    _pending.clear();
    if (_frameBuffer != null) {
      malloc.free(_frameBuffer!);
      _frameBuffer = null;
    }
    _touch.close();
    _link.close();
    _ota.close();
    _rx.close();
  }
}
