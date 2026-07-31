/// The native [PicoViewController]: owns the FFI lifecycle (init / open / flush /
/// event channel) for the native pico_view bridge.
///
/// The control plane is protobuf over one generic call: requests are encoded
/// `PvRequest` messages through `pv_request`, and engine events (touch, link
/// state, OTA progress) arrive on the `pv_init` SendPort as encoded `PvEvent`
/// bytes. Only frame delivery (`pv_lcd_flush`) bypasses the message channel.
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

  /// Send one encoded control-plane request and decode the response. Throws
  /// [PicoViewException] only when the native side produced no response at all
  pb.PvResponse _request(pb.PvRequest req) {
    final reqBytes = req.writeToBuffer();
    final reqPtr = malloc.allocate<ffi.Uint8>(reqBytes.length);
    final respPtr = malloc.allocate<ffi.Pointer<ffi.Uint8>>(
      ffi.sizeOf<ffi.Pointer<ffi.Uint8>>(),
    );
    final respLen = malloc.allocate<ffi.UintPtr>(ffi.sizeOf<ffi.UintPtr>());
    try {
      reqPtr.asTypedList(reqBytes.length).setAll(0, reqBytes);
      final rc = bindings.pv_request(reqPtr, reqBytes.length, respPtr, respLen);
      if (rc != 0) {
        throw PicoViewException('pv_request failed (code $rc)', code: rc);
      }
      final ptr = respPtr.value;
      final len = respLen.value;
      try {
        // Parsing copies out of the native buffer, so it can be freed after.
        return pb.PvResponse.fromBuffer(ptr.asTypedList(len));
      } finally {
        bindings.pv_free(ptr, len);
      }
    } finally {
      malloc.free(reqPtr);
      malloc.free(respPtr);
      malloc.free(respLen);
    }
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
  /// Blocks until the device is open and the panel initialized, so a normal
  /// return does mean a panel is attached; the engine gives up after ~10s.
  /// [linkState] is [PicoLinkState.connected] on return, and a matching
  /// transition is also delivered on [linkStates].
  ///
  /// The link can still drop afterwards — the engine reconnects on its own —
  /// so keep watching [linkStates] for the live state rather than assuming a
  /// successful open holds.
  ///
  /// Throws [PicoViewException] when no device could be opened.
  void open(PicoViewConfig config) {
    if (!_initialized) init();
    final req = pb.PvRequest(openDevice: pb.OpenDevice(model: config.model));
    var resp = _request(req);
    if (resp.whichResp() == pb.PvResponse_Resp.error &&
        resp.error.code == pb.ErrorCode.ERROR_CODE_ALREADY_OPEN &&
        !_opened) {
      // The native worker survived a hot restart (this controller never
      // opened it). Tear the stale one down and retry once.
      _request(pb.PvRequest(closeDevice: pb.CloseDevice()));
      resp = _request(req);
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
  bool setBrightness(int level) {
    if (_disposed || !_opened) return false;
    final clamped = level.clamp(0, 255);
    final resp = _request(
      pb.PvRequest(setParam: pbw.SetParam(brightness: clamped)),
    );
    return resp.whichResp() != pb.PvResponse_Resp.error;
  }

  /// Play one built-in DRV2605L haptic effect ([effect] is a ROM waveform id,
  /// 1–123). [library] picks the ROM library (1–7); 0 keeps the firmware
  /// default (the LRA library).
  bool playHaptic(int effect, {int library = 0}) {
    if (_disposed || !_opened) return false;
    final resp = _request(
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
  bool stopHaptic() {
    if (_disposed || !_opened) return false;
    final resp = _request(
      pb.PvRequest(haptics: pbw.Haptics(stop: pbw.HapticsStop())),
    );
    return resp.whichResp() != pb.PvResponse_Resp.error;
  }

  /// Ask the connected device to describe itself: serial, firmware version,
  /// panel geometry and capabilities.
  ///
  /// Unlike the fire-and-forget commands this round-trips to the device, so it
  /// blocks the caller briefly (sub-second in practice; the engine gives up
  /// after a couple of seconds). Query it after a CONNECTED transition on
  /// [linkStates] rather than caching it across a reconnect — a different unit
  /// may have been plugged in.
  ///
  /// Throws [PicoViewException] when no device is open, the link is down, or
  /// the device did not answer in time.
  PicoDeviceInfo getDeviceInfo() {
    if (_disposed || !_opened) {
      throw PicoViewException('getDeviceInfo: device not open', code: -1);
    }
    final resp = _request(
      pb.PvRequest(getDeviceInfo: pbw.GetDeviceInfo()),
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
  /// open, or the worker is gone).
  void otaStart(Uint8List image) {
    if (_disposed || !_opened) {
      throw PicoViewException('otaStart: device not open', code: -1);
    }
    final resp = _request(pb.PvRequest(otaStart: pb.OtaStart(image: image)));
    if (resp.whichResp() == pb.PvResponse_Resp.error) {
      _throwError(resp.error, 'otaStart');
    }
  }

  /// Decode one `PvEvent` pushed from the native side and route it to the
  /// matching stream. Unknown variants are ignored so newer native libraries
  /// stay compatible with older Dart code.
  void _onMessage(dynamic raw) {
    if (raw is! Uint8List) return;
    final pb.PvEvent event;
    try {
      event = pb.PvEvent.fromBuffer(raw);
    } catch (_) {
      return;
    }
    switch (event.whichEvent()) {
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
  /// is a process-wide singleton, so `pv_close` is called only when [open]
  /// succeeded here — an unconditional close would tear down a device worker
  /// this controller never owned.
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    if (_opened) {
      bindings.pv_close();
      _opened = false;
    }
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
