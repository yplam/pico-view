// Round-trips the protobuf control plane that carries every non-frame call
// across the FFI boundary. No device or native library is involved: these
// encode a message, parse the bytes back, and assert the contract the Rust side
// relies on.

import 'dart:typed_data';

import 'package:flutter_test/flutter_test.dart';
import 'package:pico_view/src/gen/pv_ffi.pb.dart' as pb;
import 'package:pico_view/src/gen/pv_wire.pb.dart' as pbw;

void main() {
  group('PvRequest', () {
    test('open_device survives a round trip', () {
      final req = pb.PvRequest(
        openDevice: pb.OpenDevice(model: 'st77916-round-360'),
      );
      final decoded = pb.PvRequest.fromBuffer(req.writeToBuffer());
      expect(decoded.whichReq(), pb.PvRequest_Req.openDevice);
      expect(decoded.openDevice.model, 'st77916-round-360');
    });

    test('setting a second variant clears the first', () {
      final req = pb.PvRequest(openDevice: pb.OpenDevice(model: 'a'))
        ..closeDevice = pb.CloseDevice();
      final decoded = pb.PvRequest.fromBuffer(req.writeToBuffer());
      expect(decoded.whichReq(), pb.PvRequest_Req.closeDevice);
      expect(decoded.openDevice.model, isEmpty);
    });

    test('ota_start carries the image bytes intact', () {
      final image = List<int>.generate(1024, (i) => i % 256);
      final req = pb.PvRequest(otaStart: pb.OtaStart(image: image));
      final decoded = pb.PvRequest.fromBuffer(req.writeToBuffer());
      expect(decoded.otaStart.image, image);
    });

    test('wire messages nest inside the FFI request', () {
      final req = pb.PvRequest(
        haptics: pbw.Haptics(play: pbw.HapticsPlay(effect: 47, library: 6)),
      );
      final decoded = pb.PvRequest.fromBuffer(req.writeToBuffer());
      expect(decoded.whichReq(), pb.PvRequest_Req.haptics);
      expect(decoded.haptics.play.effect, 47);
      expect(decoded.haptics.play.library, 6);
    });

    test('get_device_info survives a round trip', () {
      final req = pb.PvRequest(getDeviceInfo: pbw.GetDeviceInfo());
      final decoded = pb.PvRequest.fromBuffer(req.writeToBuffer());
      // An empty nested message still selects its oneof arm, which is what
      // tells the engine to query the device rather than answering `notSet`.
      expect(decoded.whichReq(), pb.PvRequest_Req.getDeviceInfo);
    });

    test('an empty request decodes as notSet rather than throwing', () {
      final decoded = pb.PvRequest.fromBuffer(pb.PvRequest().writeToBuffer());
      expect(decoded.whichReq(), pb.PvRequest_Req.notSet);
    });

    test('the id and timeout ride alongside the variant, not inside it', () {
      // Both sit outside the oneof, so setting the variant must not clear
      // them — the id is what the response is matched on.
      final req = pb.PvRequest(getDeviceInfo: pbw.GetDeviceInfo())
        ..id = 42
        ..timeoutMs = 2000;
      final decoded = pb.PvRequest.fromBuffer(req.writeToBuffer());
      expect(decoded.id, 42);
      expect(decoded.timeoutMs, 2000);
      expect(decoded.whichReq(), pb.PvRequest_Req.getDeviceInfo);
    });

    test('an unset id and timeout default to 0', () {
      // 0 is meaningful on both: "answer nobody" and "use the engine default".
      final decoded = pb.PvRequest.fromBuffer(
        pb.PvRequest(closeDevice: pb.CloseDevice()).writeToBuffer(),
      );
      expect(decoded.id, 0);
      expect(decoded.timeoutMs, 0);
    });
  });

  group('PvResponse', () {
    test('error carries code and message', () {
      final resp = pb.PvResponse(
        error: pb.Error(
          code: pb.ErrorCode.ERROR_CODE_DEVICE,
          message: 'USB open failed',
        ),
      );
      final decoded = pb.PvResponse.fromBuffer(resp.writeToBuffer());
      expect(decoded.whichResp(), pb.PvResponse_Resp.error);
      expect(decoded.error.code, pb.ErrorCode.ERROR_CODE_DEVICE);
      expect(decoded.error.message, 'USB open failed');
    });

    test('device_info round trips with the nested panel and caps', () {
      // What `PicoViewController.getDeviceInfo` maps to `PicoDeviceInfo`. The
      // panel is a nested message, so `hasPanel()` must distinguish "reported
      // 0x0" from "not reported".
      final resp = pb.PvResponse(
        deviceInfo: pbw.DeviceInfo(
          deviceId: 'PV4-A00123',
          serial: 'PV-P4-0001',
          fwVersion: '1.4.0',
          protoVersion: 2,
          panel: pbw.PanelGeometry(
            width: 360,
            height: 360,
            shape: pbw.PanelShape.PANEL_SHAPE_ROUND,
          ),
          caps: pbw.Capabilities(setParam: true, haptics: true, auth: false),
        ),
      );
      final decoded = pb.PvResponse.fromBuffer(resp.writeToBuffer());
      expect(decoded.whichResp(), pb.PvResponse_Resp.deviceInfo);

      final info = decoded.deviceInfo;
      expect(info.deviceId, 'PV4-A00123');
      expect(info.serial, 'PV-P4-0001');
      expect(info.fwVersion, '1.4.0');
      expect(info.protoVersion, 2);
      expect(info.hasPanel(), isTrue);
      expect(info.panel.width, 360);
      expect(info.panel.shape, pbw.PanelShape.PANEL_SHAPE_ROUND);
      expect(info.caps.setParam, isTrue);
      expect(info.caps.auth, isFalse);
    });

    test('device_info without a panel reports hasPanel false', () {
      final decoded = pb.PvResponse.fromBuffer(
        pb.PvResponse(deviceInfo: pbw.DeviceInfo(serial: 'x')).writeToBuffer(),
      );
      // The mapping falls back to 0x0 / unknown shape rather than inventing one.
      expect(decoded.deviceInfo.hasPanel(), isFalse);
      expect(decoded.deviceInfo.panel.width, 0);
      expect(
        decoded.deviceInfo.panel.shape,
        pbw.PanelShape.PANEL_SHAPE_UNSPECIFIED,
      );
    });

    test('ack is distinguishable from error when both are empty', () {
      final ack = pb.PvResponse.fromBuffer(
        pb.PvResponse(ack: pb.Ack()).writeToBuffer(),
      );
      expect(ack.whichResp(), pb.PvResponse_Resp.ack);

      final err = pb.PvResponse.fromBuffer(
        pb.PvResponse(error: pb.Error()).writeToBuffer(),
      );
      expect(err.whichResp(), pb.PvResponse_Resp.error);
    });
  });

  group('PvEvent', () {
    test('touch survives a round trip', () {
      final event = pb.PvEvent(
        touch: pbw.Touch(phase: pbw.TouchPhase.TOUCH_PHASE_MOVE, x: 359, y: 0),
      );
      final decoded = pb.PvEvent.fromBuffer(event.writeToBuffer());
      expect(decoded.whichEvent(), pb.PvEvent_Event.touch);
      expect(decoded.touch.phase, pbw.TouchPhase.TOUCH_PHASE_MOVE);
      expect(decoded.touch.x, 359);
      expect(decoded.touch.y, 0);
    });

    test('link event carries the firmware version', () {
      final event = pb.PvEvent(
        link: pb.LinkEvent(
          state: pb.LinkState.LINK_STATE_CONNECTED,
          fwVersion: '1.4.0',
        ),
      );
      final decoded = pb.PvEvent.fromBuffer(event.writeToBuffer());
      expect(decoded.link.state, pb.LinkState.LINK_STATE_CONNECTED);
      expect(decoded.link.fwVersion, '1.4.0');
    });

    test('ota status carries a negative error code', () {
      // `err` is sint32 on the wire; -1 (disconnected) must not come back as a
      // huge unsigned value.
      final event = pb.PvEvent(
        ota: pbw.OtaStatus(
          state: pbw.OtaState.OTA_STATE_FAILED,
          pct: 42,
          err: -1,
        ),
      );
      final decoded = pb.PvEvent.fromBuffer(event.writeToBuffer());
      expect(decoded.ota.err, -1);
      expect(decoded.ota.pct, 42);
    });

    test('a response arrives as an event carrying its request id', () {
      // How every non-frame call is answered now: the engine posts the
      // PvResponse on the same SendPort as touch/link/OTA, and the controller
      // matches `id` back to the Completer that is waiting on it.
      final event = pb.PvEvent(
        response: pb.PvResponse(id: 7, ack: pb.Ack()),
      );
      final decoded = pb.PvEvent.fromBuffer(event.writeToBuffer());
      expect(decoded.whichEvent(), pb.PvEvent_Event.response);
      expect(decoded.response.id, 7);
      expect(decoded.response.whichResp(), pb.PvResponse_Resp.ack);
    });

    test('a response keeps its id alongside a device_info payload', () {
      // The id sits outside the `resp` oneof, so it survives the largest
      // payload the control plane carries.
      final event = pb.PvEvent(
        response: pb.PvResponse(
          id: 4294967295,
          deviceInfo: pbw.DeviceInfo(serial: 'PV-P4-0001'),
        ),
      );
      final decoded = pb.PvEvent.fromBuffer(event.writeToBuffer());
      expect(decoded.response.id, 4294967295);
      expect(decoded.response.deviceInfo.serial, 'PV-P4-0001');
    });

    test('an unknown event variant decodes as notSet', () {
      // Forward compatibility: a newer engine sending a variant this build does
      // not know must parse cleanly so the controller can ignore it, not throw.
      // Field 255, wire type 2 (length-delimited), three bytes of payload.
      final bytes = Uint8List.fromList(const [0xFA, 0x0F, 0x03, 1, 2, 3]);
      final decoded = pb.PvEvent.fromBuffer(bytes);
      expect(decoded.whichEvent(), pb.PvEvent_Event.notSet);
      // And it must survive a re-encode, preserving the unknown field.
      expect(decoded.writeToBuffer(), bytes);
    });
  });
}
