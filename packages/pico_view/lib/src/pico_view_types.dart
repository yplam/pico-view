/// Pure-Dart types shared by the native and web [PicoViewController] backends.
///
/// Kept free of `dart:ffi`/`dart:isolate` so it compiles on every target.
library;

import 'package:flutter/foundation.dart';

/// Phase of a physical-touch event reported by the panel.
enum TouchPhase {
  /// Finger landed on the panel.
  down,

  /// Finger moved while still down.
  move,

  /// Finger lifted. The panel reports `(0, 0)` for this phase, so consumers
  /// should reuse the last known position.
  up,
}

/// Live state of the USB link to the panel, reported by the native engine on
/// transitions (the engine reconnects on its own; the app only displays this).
enum PicoLinkState {
  /// Device attached and configured.
  connected,

  /// Device lost (unplugged / rebooting after an update); the engine is
  /// retrying in the background.
  disconnected,
}

/// One firmware-update progress/result event from the native event channel.
@immutable
class PicoOtaEvent {
  /// Creates an update event with its [state], progress [pct] and error [err].
  const PicoOtaEvent(this.state, this.pct, this.err);

  /// `receiving`, `verifying`, `done`, `failed`, or `unknown`.
  final String state;

  /// Receive progress, 0–100.
  final int pct;

  /// `pv_ota_err` code (0 = none; -1 = device disconnected).
  final int err;

  /// Whether the update has finished, either way — no further events follow.
  bool get isTerminal => state == 'done' || state == 'failed';

  @override
  String toString() => 'PicoOtaEvent($state, $pct%, err $err)';
}

/// Visible outline of the panel's active area, as reported by
/// [PicoDeviceInfo.panelShape].
enum PicoPanelShape {
  /// The full rectangle is visible glass.
  rect,

  /// Only the inscribed circle (diameter `min(width, height)`) is visible
  /// glass; lay content out to clear the round rim.
  round,

  /// The engine did not report an outline (firmware too old to answer).
  unknown,
}

/// What the connected device reports about itself, from
/// [PicoViewController.getDeviceInfo].
///
/// Every field is a device-reported value, so it is only as current as the last
/// call — re-query after a reconnect rather than caching across one.
@immutable
class PicoDeviceInfo {
  /// Creates a device descriptor. Defaults match a device that reported
  /// nothing for the field.
  const PicoDeviceInfo({
    this.deviceId = '',
    this.serial = '',
    this.firmwareVersion = '',
    this.protoVersion = 0,
    this.panelWidth = 0,
    this.panelHeight = 0,
    this.panelShape = PicoPanelShape.unknown,
    this.supportsBrightness = false,
    this.supportsHaptics = false,
    this.supportsAttestation = false,
  });

  /// Factory device id burned in at provisioning (e.g. `PV4-A00123`), or empty
  /// on an unprovisioned board.
  ///
  /// Unlike [PicoViewController.attestedDeviceId] this is *self-reported* and
  /// proves nothing; use the attested id when the identity has to be trusted.
  final String deviceId;

  /// The device's USB serial string, which identifies this exact unit.
  final String serial;

  /// Firmware (ESP-IDF app) version, e.g. `1.4.0`; empty if not reported.
  final String firmwareVersion;

  /// Wire protocol version the device speaks.
  final int protoVersion;

  /// Visible panel width in pixels, or `0` if not reported.
  final int panelWidth;

  /// Visible panel height in pixels, or `0` if not reported.
  final int panelHeight;

  /// Visible outline of the panel.
  final PicoPanelShape panelShape;

  /// Whether the device honours [PicoViewController.setBrightness].
  final bool supportsBrightness;

  /// Whether the device has a haptic driver and honours
  /// [PicoViewController.playHaptic].
  final bool supportsHaptics;

  /// Whether the device carries a vendor-provisioned identity and can answer
  /// the attestation challenge (see [PicoViewController.deviceVerified]).
  final bool supportsAttestation;

  @override
  String toString() =>
      'PicoDeviceInfo($deviceId, serial $serial, fw $firmwareVersion, '
      'proto $protoVersion, ${panelWidth}x$panelHeight ${panelShape.name})';
}

/// A touch event in LCD pixel coordinates.
@immutable
class PicoTouchEvent {
  /// Creates a touch in [phase] at panel pixel ([x], [y]).
  const PicoTouchEvent(this.phase, this.x, this.y);

  /// Whether the finger landed, moved, or lifted.
  final TouchPhase phase;

  /// Horizontal panel pixel, 0 at the left edge.
  final int x;

  /// Vertical panel pixel, 0 at the top edge.
  final int y;

  @override
  String toString() => 'PicoTouchEvent($phase, $x, $y)';
}

/// Width/height of each built-in panel [model](PicoViewConfig.model), so the
/// `PicoView` widget can size its capture surface without a round-trip to native.
/// Keep in sync with the Rust `panels` preset registry.
const Map<String, ({int width, int height})> kPicoViewModels = {
  'st77916-round-360': (width: 360, height: 360),
};

/// The model assumed when [PicoViewConfig] is constructed without one: the
/// round 360x360 ST77916 panel.
const String kPicoViewDefaultModel = 'st77916-round-360';

/// Open-time device configuration.
@immutable
class PicoViewConfig {
  /// Creates a config for [model], defaulting to [kPicoViewDefaultModel].
  const PicoViewConfig({this.model = kPicoViewDefaultModel});

  /// Panel model name; resolved to a preset on the native side.
  final String model;

  /// Visible width of the selected [model] in pixels, or `0` if unknown.
  int get width => kPicoViewModels[model]?.width ?? 0;

  /// Visible height of the selected [model] in pixels, or `0` if unknown.
  int get height => kPicoViewModels[model]?.height ?? 0;
}

/// Thrown when a native call fails.
class PicoViewException implements Exception {
  /// Creates an exception describing [message], optionally with the native
  /// return [code].
  PicoViewException(this.message, {this.code});

  /// Human-readable description of what failed.
  final String message;

  /// The native return code, when the failure came from an FFI call.
  final int? code;

  @override
  String toString() => 'PicoViewException: $message';
}
