/// The web [PicoViewController]: a no-op stub.
library;

import 'dart:async';
import 'dart:typed_data';

import 'pico_view_types.dart';

/// The web build of the controller. There is no USB on the web, so every device
/// call is a no-op and the streams never emit — this exists only so code using
/// `package:pico_view` still compiles for web.
class PicoViewController {
  final StreamController<PicoTouchEvent> _touch =
      StreamController<PicoTouchEvent>.broadcast();
  final StreamController<PicoLinkState> _link =
      StreamController<PicoLinkState>.broadcast();
  final StreamController<PicoOtaEvent> _ota =
      StreamController<PicoOtaEvent>.broadcast();

  PicoViewConfig _config = const PicoViewConfig();

  /// Physical-touch events. Never emits on web.
  Stream<PicoTouchEvent> get touches => _touch.stream;

  /// Link-state transitions. Never emits on web.
  Stream<PicoLinkState> get linkStates => _link.stream;

  /// Always [PicoLinkState.disconnected] on web.
  PicoLinkState get linkState => PicoLinkState.disconnected;

  /// No device on web, so no firmware version.
  String? get firmwareVersion => null;

  /// No device on web, so nothing is ever attested.
  bool get deviceVerified => false;

  /// No device on web, so no attested device id.
  String? get attestedDeviceId => null;

  /// Firmware-update events. Never emits on web.
  Stream<PicoOtaEvent> get otaEvents => _ota.stream;

  /// The config last passed to [open]; used by `PicoView` for geometry.
  PicoViewConfig get config => _config;

  /// Whether an external producer owns the panel. Unused on web.
  bool suspendCapture = false;

  /// Always false on web.
  bool get isOpen => false;

  /// No-op on web.
  void init() {}

  /// Records [config] for geometry; opens nothing.
  Future<void> open(PicoViewConfig config) async {
    _config = config;
  }

  /// Always returns false on web — there is nowhere to send the frame.
  bool flushRgba(Uint8List rgba, int width, int height) => false;

  /// Always returns false on web.
  Future<bool> setBrightness(int level) async => false;

  /// Always returns false on web.
  Future<bool> playHaptic(int effect, {int library = 0}) async => false;

  /// Always returns false on web.
  Future<bool> stopHaptic() async => false;

  /// Always throws [PicoViewException] on web — there is no device to ask.
  Future<PicoDeviceInfo> getDeviceInfo() {
    throw PicoViewException('device info is not available on web');
  }

  /// Always throws [PicoViewException] on web.
  Future<void> otaStart(Uint8List image) {
    throw PicoViewException('firmware update is not supported on web');
  }

  /// Close the stream controllers. Safe to call multiple times.
  Future<void> dispose() async => disposeSync();

  /// Close the stream controllers without awaiting. Safe to call multiple times.
  void disposeSync() {
    _touch.close();
    _link.close();
    _ota.close();
  }
}
