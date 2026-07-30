// This is a generated file - do not edit.
//
// Generated from pv_ffi.proto.

// @dart = 3.3

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names
// ignore_for_file: curly_braces_in_flow_control_structures
// ignore_for_file: deprecated_member_use_from_same_package, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_relative_imports

import 'dart:core' as $core;

import 'package:protobuf/protobuf.dart' as $pb;

import 'pv_ffi.pbenum.dart';
import 'pv_wire.pb.dart' as $0;

export 'package:protobuf/protobuf.dart' show GeneratedMessageGenericExtensions;

export 'pv_ffi.pbenum.dart';

enum PvRequest_Req {
  openDevice,
  closeDevice,
  otaStart,
  getDeviceInfo,
  setParam,
  haptics,
  notSet
}

class PvRequest extends $pb.GeneratedMessage {
  factory PvRequest({
    OpenDevice? openDevice,
    CloseDevice? closeDevice,
    OtaStart? otaStart,
    $0.GetDeviceInfo? getDeviceInfo,
    $0.SetParam? setParam,
    $0.Haptics? haptics,
  }) {
    final result = create();
    if (openDevice != null) result.openDevice = openDevice;
    if (closeDevice != null) result.closeDevice = closeDevice;
    if (otaStart != null) result.otaStart = otaStart;
    if (getDeviceInfo != null) result.getDeviceInfo = getDeviceInfo;
    if (setParam != null) result.setParam = setParam;
    if (haptics != null) result.haptics = haptics;
    return result;
  }

  PvRequest._();

  factory PvRequest.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory PvRequest.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static const $core.Map<$core.int, PvRequest_Req> _PvRequest_ReqByTag = {
    1: PvRequest_Req.openDevice,
    2: PvRequest_Req.closeDevice,
    3: PvRequest_Req.otaStart,
    16: PvRequest_Req.getDeviceInfo,
    17: PvRequest_Req.setParam,
    18: PvRequest_Req.haptics,
    0: PvRequest_Req.notSet
  };
  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'PvRequest',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'picoview.ffi'),
      createEmptyInstance: create)
    ..oo(0, [1, 2, 3, 16, 17, 18])
    ..aOM<OpenDevice>(1, _omitFieldNames ? '' : 'openDevice',
        subBuilder: OpenDevice.create)
    ..aOM<CloseDevice>(2, _omitFieldNames ? '' : 'closeDevice',
        subBuilder: CloseDevice.create)
    ..aOM<OtaStart>(3, _omitFieldNames ? '' : 'otaStart',
        subBuilder: OtaStart.create)
    ..aOM<$0.GetDeviceInfo>(16, _omitFieldNames ? '' : 'getDeviceInfo',
        subBuilder: $0.GetDeviceInfo.create)
    ..aOM<$0.SetParam>(17, _omitFieldNames ? '' : 'setParam',
        subBuilder: $0.SetParam.create)
    ..aOM<$0.Haptics>(18, _omitFieldNames ? '' : 'haptics',
        subBuilder: $0.Haptics.create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  PvRequest clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  PvRequest copyWith(void Function(PvRequest) updates) =>
      super.copyWith((message) => updates(message as PvRequest)) as PvRequest;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PvRequest create() => PvRequest._();
  @$core.override
  PvRequest createEmptyInstance() => create();
  @$core.pragma('dart2js:noInline')
  static PvRequest getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PvRequest>(create);
  static PvRequest? _defaultInstance;

  @$pb.TagNumber(1)
  @$pb.TagNumber(2)
  @$pb.TagNumber(3)
  @$pb.TagNumber(16)
  @$pb.TagNumber(17)
  @$pb.TagNumber(18)
  PvRequest_Req whichReq() => _PvRequest_ReqByTag[$_whichOneof(0)]!;
  @$pb.TagNumber(1)
  @$pb.TagNumber(2)
  @$pb.TagNumber(3)
  @$pb.TagNumber(16)
  @$pb.TagNumber(17)
  @$pb.TagNumber(18)
  void clearReq() => $_clearField($_whichOneof(0));

  @$pb.TagNumber(1)
  OpenDevice get openDevice => $_getN(0);
  @$pb.TagNumber(1)
  set openDevice(OpenDevice value) => $_setField(1, value);
  @$pb.TagNumber(1)
  $core.bool hasOpenDevice() => $_has(0);
  @$pb.TagNumber(1)
  void clearOpenDevice() => $_clearField(1);
  @$pb.TagNumber(1)
  OpenDevice ensureOpenDevice() => $_ensure(0);

  @$pb.TagNumber(2)
  CloseDevice get closeDevice => $_getN(1);
  @$pb.TagNumber(2)
  set closeDevice(CloseDevice value) => $_setField(2, value);
  @$pb.TagNumber(2)
  $core.bool hasCloseDevice() => $_has(1);
  @$pb.TagNumber(2)
  void clearCloseDevice() => $_clearField(2);
  @$pb.TagNumber(2)
  CloseDevice ensureCloseDevice() => $_ensure(1);

  @$pb.TagNumber(3)
  OtaStart get otaStart => $_getN(2);
  @$pb.TagNumber(3)
  set otaStart(OtaStart value) => $_setField(3, value);
  @$pb.TagNumber(3)
  $core.bool hasOtaStart() => $_has(2);
  @$pb.TagNumber(3)
  void clearOtaStart() => $_clearField(3);
  @$pb.TagNumber(3)
  OtaStart ensureOtaStart() => $_ensure(2);

  /// Forwarded to the device.
  @$pb.TagNumber(16)
  $0.GetDeviceInfo get getDeviceInfo => $_getN(3);
  @$pb.TagNumber(16)
  set getDeviceInfo($0.GetDeviceInfo value) => $_setField(16, value);
  @$pb.TagNumber(16)
  $core.bool hasGetDeviceInfo() => $_has(3);
  @$pb.TagNumber(16)
  void clearGetDeviceInfo() => $_clearField(16);
  @$pb.TagNumber(16)
  $0.GetDeviceInfo ensureGetDeviceInfo() => $_ensure(3);

  @$pb.TagNumber(17)
  $0.SetParam get setParam => $_getN(4);
  @$pb.TagNumber(17)
  set setParam($0.SetParam value) => $_setField(17, value);
  @$pb.TagNumber(17)
  $core.bool hasSetParam() => $_has(4);
  @$pb.TagNumber(17)
  void clearSetParam() => $_clearField(17);
  @$pb.TagNumber(17)
  $0.SetParam ensureSetParam() => $_ensure(4);

  @$pb.TagNumber(18)
  $0.Haptics get haptics => $_getN(5);
  @$pb.TagNumber(18)
  set haptics($0.Haptics value) => $_setField(18, value);
  @$pb.TagNumber(18)
  $core.bool hasHaptics() => $_has(5);
  @$pb.TagNumber(18)
  void clearHaptics() => $_clearField(18);
  @$pb.TagNumber(18)
  $0.Haptics ensureHaptics() => $_ensure(5);
}

/// Open the panel device and start a session. Request/reply: responds `ack` once
/// the device is open and the panel initialized, ERROR_CODE_DEVICE when that
/// fails, or ERROR_CODE_TIMEOUT if the worker did not get to it in time. A
/// LinkEvent(CONNECTED) is posted alongside the `ack`.
class OpenDevice extends $pb.GeneratedMessage {
  factory OpenDevice({
    $core.int? index,
    $core.String? model,
    $core.String? serial,
  }) {
    final result = create();
    if (index != null) result.index = index;
    if (model != null) result.model = model;
    if (serial != null) result.serial = serial;
    return result;
  }

  OpenDevice._();

  factory OpenDevice.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory OpenDevice.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'OpenDevice',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'picoview.ffi'),
      createEmptyInstance: create)
    ..aI(1, _omitFieldNames ? '' : 'index', fieldType: $pb.PbFieldType.OU3)
    ..aOS(2, _omitFieldNames ? '' : 'model')
    ..aOS(3, _omitFieldNames ? '' : 'serial')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  OpenDevice clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  OpenDevice copyWith(void Function(OpenDevice) updates) =>
      super.copyWith((message) => updates(message as OpenDevice)) as OpenDevice;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static OpenDevice create() => OpenDevice._();
  @$core.override
  OpenDevice createEmptyInstance() => create();
  @$core.pragma('dart2js:noInline')
  static OpenDevice getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<OpenDevice>(create);
  static OpenDevice? _defaultInstance;

  /// Nth enumerated device matching the pico-view VID/PID (ignored when `serial` is set).
  @$pb.TagNumber(1)
  $core.int get index => $_getIZ(0);
  @$pb.TagNumber(1)
  set index($core.int value) => $_setUnsignedInt32(0, value);
  @$pb.TagNumber(1)
  $core.bool hasIndex() => $_has(0);
  @$pb.TagNumber(1)
  void clearIndex() => $_clearField(1);

  /// Panel model preset name, e.g. "st77916-round-360"; empty = default.
  @$pb.TagNumber(2)
  $core.String get model => $_getSZ(1);
  @$pb.TagNumber(2)
  set model($core.String value) => $_setString(1, value);
  @$pb.TagNumber(2)
  $core.bool hasModel() => $_has(1);
  @$pb.TagNumber(2)
  void clearModel() => $_clearField(2);

  /// Select by USB serial instead of index; empty = use index.
  @$pb.TagNumber(3)
  $core.String get serial => $_getSZ(2);
  @$pb.TagNumber(3)
  set serial($core.String value) => $_setString(2, value);
  @$pb.TagNumber(3)
  $core.bool hasSerial() => $_has(2);
  @$pb.TagNumber(3)
  void clearSerial() => $_clearField(3);
}

/// Stop the worker and close the device. Responds `ack` after teardown completes.
class CloseDevice extends $pb.GeneratedMessage {
  factory CloseDevice() => create();

  CloseDevice._();

  factory CloseDevice.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory CloseDevice.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'CloseDevice',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'picoview.ffi'),
      createEmptyInstance: create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  CloseDevice clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  CloseDevice copyWith(void Function(CloseDevice) updates) =>
      super.copyWith((message) => updates(message as CloseDevice))
          as CloseDevice;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static CloseDevice create() => CloseDevice._();
  @$core.override
  CloseDevice createEmptyInstance() => create();
  @$core.pragma('dart2js:noInline')
  static CloseDevice getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<CloseDevice>(create);
  static CloseDevice? _defaultInstance;
}

/// Stream a signed ESP-IDF app image to the device and commit it. Responds
/// `ack` once enqueued; progress/result arrive as wire.OtaStatus events.
class OtaStart extends $pb.GeneratedMessage {
  factory OtaStart({
    $core.List<$core.int>? image,
  }) {
    final result = create();
    if (image != null) result.image = image;
    return result;
  }

  OtaStart._();

  factory OtaStart.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory OtaStart.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'OtaStart',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'picoview.ffi'),
      createEmptyInstance: create)
    ..a<$core.List<$core.int>>(
        1, _omitFieldNames ? '' : 'image', $pb.PbFieldType.OY)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  OtaStart clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  OtaStart copyWith(void Function(OtaStart) updates) =>
      super.copyWith((message) => updates(message as OtaStart)) as OtaStart;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static OtaStart create() => OtaStart._();
  @$core.override
  OtaStart createEmptyInstance() => create();
  @$core.pragma('dart2js:noInline')
  static OtaStart getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<OtaStart>(create);
  static OtaStart? _defaultInstance;

  @$pb.TagNumber(1)
  $core.List<$core.int> get image => $_getN(0);
  @$pb.TagNumber(1)
  set image($core.List<$core.int> value) => $_setBytes(0, value);
  @$pb.TagNumber(1)
  $core.bool hasImage() => $_has(0);
  @$pb.TagNumber(1)
  void clearImage() => $_clearField(1);
}

enum PvResponse_Resp { ack, error, deviceInfo, notSet }

class PvResponse extends $pb.GeneratedMessage {
  factory PvResponse({
    Ack? ack,
    Error? error,
    $0.DeviceInfo? deviceInfo,
  }) {
    final result = create();
    if (ack != null) result.ack = ack;
    if (error != null) result.error = error;
    if (deviceInfo != null) result.deviceInfo = deviceInfo;
    return result;
  }

  PvResponse._();

  factory PvResponse.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory PvResponse.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static const $core.Map<$core.int, PvResponse_Resp> _PvResponse_RespByTag = {
    1: PvResponse_Resp.ack,
    2: PvResponse_Resp.error,
    3: PvResponse_Resp.deviceInfo,
    0: PvResponse_Resp.notSet
  };
  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'PvResponse',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'picoview.ffi'),
      createEmptyInstance: create)
    ..oo(0, [1, 2, 3])
    ..aOM<Ack>(1, _omitFieldNames ? '' : 'ack', subBuilder: Ack.create)
    ..aOM<Error>(2, _omitFieldNames ? '' : 'error', subBuilder: Error.create)
    ..aOM<$0.DeviceInfo>(3, _omitFieldNames ? '' : 'deviceInfo',
        subBuilder: $0.DeviceInfo.create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  PvResponse clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  PvResponse copyWith(void Function(PvResponse) updates) =>
      super.copyWith((message) => updates(message as PvResponse)) as PvResponse;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PvResponse create() => PvResponse._();
  @$core.override
  PvResponse createEmptyInstance() => create();
  @$core.pragma('dart2js:noInline')
  static PvResponse getDefault() => _defaultInstance ??=
      $pb.GeneratedMessage.$_defaultFor<PvResponse>(create);
  static PvResponse? _defaultInstance;

  @$pb.TagNumber(1)
  @$pb.TagNumber(2)
  @$pb.TagNumber(3)
  PvResponse_Resp whichResp() => _PvResponse_RespByTag[$_whichOneof(0)]!;
  @$pb.TagNumber(1)
  @$pb.TagNumber(2)
  @$pb.TagNumber(3)
  void clearResp() => $_clearField($_whichOneof(0));

  @$pb.TagNumber(1)
  Ack get ack => $_getN(0);
  @$pb.TagNumber(1)
  set ack(Ack value) => $_setField(1, value);
  @$pb.TagNumber(1)
  $core.bool hasAck() => $_has(0);
  @$pb.TagNumber(1)
  void clearAck() => $_clearField(1);
  @$pb.TagNumber(1)
  Ack ensureAck() => $_ensure(0);

  @$pb.TagNumber(2)
  Error get error => $_getN(1);
  @$pb.TagNumber(2)
  set error(Error value) => $_setField(2, value);
  @$pb.TagNumber(2)
  $core.bool hasError() => $_has(1);
  @$pb.TagNumber(2)
  void clearError() => $_clearField(2);
  @$pb.TagNumber(2)
  Error ensureError() => $_ensure(1);

  @$pb.TagNumber(3)
  $0.DeviceInfo get deviceInfo => $_getN(2);
  @$pb.TagNumber(3)
  set deviceInfo($0.DeviceInfo value) => $_setField(3, value);
  @$pb.TagNumber(3)
  $core.bool hasDeviceInfo() => $_has(2);
  @$pb.TagNumber(3)
  void clearDeviceInfo() => $_clearField(3);
  @$pb.TagNumber(3)
  $0.DeviceInfo ensureDeviceInfo() => $_ensure(2);
}

class Ack extends $pb.GeneratedMessage {
  factory Ack() => create();

  Ack._();

  factory Ack.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory Ack.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'Ack',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'picoview.ffi'),
      createEmptyInstance: create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Ack clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Ack copyWith(void Function(Ack) updates) =>
      super.copyWith((message) => updates(message as Ack)) as Ack;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Ack create() => Ack._();
  @$core.override
  Ack createEmptyInstance() => create();
  @$core.pragma('dart2js:noInline')
  static Ack getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Ack>(create);
  static Ack? _defaultInstance;
}

class Error extends $pb.GeneratedMessage {
  factory Error({
    ErrorCode? code,
    $core.String? message,
  }) {
    final result = create();
    if (code != null) result.code = code;
    if (message != null) result.message = message;
    return result;
  }

  Error._();

  factory Error.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory Error.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'Error',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'picoview.ffi'),
      createEmptyInstance: create)
    ..aE<ErrorCode>(1, _omitFieldNames ? '' : 'code',
        enumValues: ErrorCode.values)
    ..aOS(2, _omitFieldNames ? '' : 'message')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Error clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  Error copyWith(void Function(Error) updates) =>
      super.copyWith((message) => updates(message as Error)) as Error;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static Error create() => Error._();
  @$core.override
  Error createEmptyInstance() => create();
  @$core.pragma('dart2js:noInline')
  static Error getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<Error>(create);
  static Error? _defaultInstance;

  @$pb.TagNumber(1)
  ErrorCode get code => $_getN(0);
  @$pb.TagNumber(1)
  set code(ErrorCode value) => $_setField(1, value);
  @$pb.TagNumber(1)
  $core.bool hasCode() => $_has(0);
  @$pb.TagNumber(1)
  void clearCode() => $_clearField(1);

  @$pb.TagNumber(2)
  $core.String get message => $_getSZ(1);
  @$pb.TagNumber(2)
  set message($core.String value) => $_setString(1, value);
  @$pb.TagNumber(2)
  $core.bool hasMessage() => $_has(1);
  @$pb.TagNumber(2)
  void clearMessage() => $_clearField(2);
}

/// Posted on transitions only (the worker de-duplicates).
class LinkEvent extends $pb.GeneratedMessage {
  factory LinkEvent({
    LinkState? state,
    $core.String? detail,
    $core.bool? verified,
    $core.String? deviceId,
    $core.String? fwVersion,
  }) {
    final result = create();
    if (state != null) result.state = state;
    if (detail != null) result.detail = detail;
    if (verified != null) result.verified = verified;
    if (deviceId != null) result.deviceId = deviceId;
    if (fwVersion != null) result.fwVersion = fwVersion;
    return result;
  }

  LinkEvent._();

  factory LinkEvent.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory LinkEvent.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'LinkEvent',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'picoview.ffi'),
      createEmptyInstance: create)
    ..aE<LinkState>(1, _omitFieldNames ? '' : 'state',
        enumValues: LinkState.values)
    ..aOS(2, _omitFieldNames ? '' : 'detail')
    ..aOB(3, _omitFieldNames ? '' : 'verified')
    ..aOS(4, _omitFieldNames ? '' : 'deviceId')
    ..aOS(5, _omitFieldNames ? '' : 'fwVersion')
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  LinkEvent clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  LinkEvent copyWith(void Function(LinkEvent) updates) =>
      super.copyWith((message) => updates(message as LinkEvent)) as LinkEvent;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static LinkEvent create() => LinkEvent._();
  @$core.override
  LinkEvent createEmptyInstance() => create();
  @$core.pragma('dart2js:noInline')
  static LinkEvent getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<LinkEvent>(create);
  static LinkEvent? _defaultInstance;

  @$pb.TagNumber(1)
  LinkState get state => $_getN(0);
  @$pb.TagNumber(1)
  set state(LinkState value) => $_setField(1, value);
  @$pb.TagNumber(1)
  $core.bool hasState() => $_has(0);
  @$pb.TagNumber(1)
  void clearState() => $_clearField(1);

  /// Reason for DISCONNECTED, for logs and the settings UI.
  @$pb.TagNumber(2)
  $core.String get detail => $_getSZ(1);
  @$pb.TagNumber(2)
  set detail($core.String value) => $_setString(1, value);
  @$pb.TagNumber(2)
  $core.bool hasDetail() => $_has(1);
  @$pb.TagNumber(2)
  void clearDetail() => $_clearField(2);

  /// Genuine-hardware result, meaningful only on CONNECTED: the device presented
  /// a valid vendor-CA-signed certificate and answered the attestation challenge.
  @$pb.TagNumber(3)
  $core.bool get verified => $_getBF(2);
  @$pb.TagNumber(3)
  set verified($core.bool value) => $_setBool(2, value);
  @$pb.TagNumber(3)
  $core.bool hasVerified() => $_has(2);
  @$pb.TagNumber(3)
  void clearVerified() => $_clearField(3);

  /// The attested device id (from the certificate). Meaningful only when
  /// `verified`; empty otherwise.
  @$pb.TagNumber(4)
  $core.String get deviceId => $_getSZ(3);
  @$pb.TagNumber(4)
  set deviceId($core.String value) => $_setString(3, value);
  @$pb.TagNumber(4)
  $core.bool hasDeviceId() => $_has(3);
  @$pb.TagNumber(4)
  void clearDeviceId() => $_clearField(4);

  /// The device's firmware version string (ESP-IDF app version, e.g. "1.4.0"),
  /// as reported in the HELLO_ACK handshake. Meaningful only on CONNECTED.
  @$pb.TagNumber(5)
  $core.String get fwVersion => $_getSZ(4);
  @$pb.TagNumber(5)
  set fwVersion($core.String value) => $_setString(4, value);
  @$pb.TagNumber(5)
  $core.bool hasFwVersion() => $_has(4);
  @$pb.TagNumber(5)
  void clearFwVersion() => $_clearField(5);
}

enum PvEvent_Event { touch, link, ota, notSet }

class PvEvent extends $pb.GeneratedMessage {
  factory PvEvent({
    $0.Touch? touch,
    LinkEvent? link,
    $0.OtaStatus? ota,
  }) {
    final result = create();
    if (touch != null) result.touch = touch;
    if (link != null) result.link = link;
    if (ota != null) result.ota = ota;
    return result;
  }

  PvEvent._();

  factory PvEvent.fromBuffer($core.List<$core.int> data,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromBuffer(data, registry);
  factory PvEvent.fromJson($core.String json,
          [$pb.ExtensionRegistry registry = $pb.ExtensionRegistry.EMPTY]) =>
      create()..mergeFromJson(json, registry);

  static const $core.Map<$core.int, PvEvent_Event> _PvEvent_EventByTag = {
    1: PvEvent_Event.touch,
    2: PvEvent_Event.link,
    3: PvEvent_Event.ota,
    0: PvEvent_Event.notSet
  };
  static final $pb.BuilderInfo _i = $pb.BuilderInfo(
      _omitMessageNames ? '' : 'PvEvent',
      package: const $pb.PackageName(_omitMessageNames ? '' : 'picoview.ffi'),
      createEmptyInstance: create)
    ..oo(0, [1, 2, 3])
    ..aOM<$0.Touch>(1, _omitFieldNames ? '' : 'touch',
        subBuilder: $0.Touch.create)
    ..aOM<LinkEvent>(2, _omitFieldNames ? '' : 'link',
        subBuilder: LinkEvent.create)
    ..aOM<$0.OtaStatus>(3, _omitFieldNames ? '' : 'ota',
        subBuilder: $0.OtaStatus.create)
    ..hasRequiredFields = false;

  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  PvEvent clone() => deepCopy();
  @$core.Deprecated('See https://github.com/google/protobuf.dart/issues/998.')
  PvEvent copyWith(void Function(PvEvent) updates) =>
      super.copyWith((message) => updates(message as PvEvent)) as PvEvent;

  @$core.override
  $pb.BuilderInfo get info_ => _i;

  @$core.pragma('dart2js:noInline')
  static PvEvent create() => PvEvent._();
  @$core.override
  PvEvent createEmptyInstance() => create();
  @$core.pragma('dart2js:noInline')
  static PvEvent getDefault() =>
      _defaultInstance ??= $pb.GeneratedMessage.$_defaultFor<PvEvent>(create);
  static PvEvent? _defaultInstance;

  @$pb.TagNumber(1)
  @$pb.TagNumber(2)
  @$pb.TagNumber(3)
  PvEvent_Event whichEvent() => _PvEvent_EventByTag[$_whichOneof(0)]!;
  @$pb.TagNumber(1)
  @$pb.TagNumber(2)
  @$pb.TagNumber(3)
  void clearEvent() => $_clearField($_whichOneof(0));

  @$pb.TagNumber(1)
  $0.Touch get touch => $_getN(0);
  @$pb.TagNumber(1)
  set touch($0.Touch value) => $_setField(1, value);
  @$pb.TagNumber(1)
  $core.bool hasTouch() => $_has(0);
  @$pb.TagNumber(1)
  void clearTouch() => $_clearField(1);
  @$pb.TagNumber(1)
  $0.Touch ensureTouch() => $_ensure(0);

  @$pb.TagNumber(2)
  LinkEvent get link => $_getN(1);
  @$pb.TagNumber(2)
  set link(LinkEvent value) => $_setField(2, value);
  @$pb.TagNumber(2)
  $core.bool hasLink() => $_has(1);
  @$pb.TagNumber(2)
  void clearLink() => $_clearField(2);
  @$pb.TagNumber(2)
  LinkEvent ensureLink() => $_ensure(1);

  @$pb.TagNumber(3)
  $0.OtaStatus get ota => $_getN(2);
  @$pb.TagNumber(3)
  set ota($0.OtaStatus value) => $_setField(3, value);
  @$pb.TagNumber(3)
  $core.bool hasOta() => $_has(2);
  @$pb.TagNumber(3)
  void clearOta() => $_clearField(3);
  @$pb.TagNumber(3)
  $0.OtaStatus ensureOta() => $_ensure(2);
}

const $core.bool _omitFieldNames =
    $core.bool.fromEnvironment('protobuf.omit_field_names');
const $core.bool _omitMessageNames =
    $core.bool.fromEnvironment('protobuf.omit_message_names');
