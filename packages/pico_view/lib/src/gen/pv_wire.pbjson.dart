// This is a generated file - do not edit.
//
// Generated from pv_wire.proto.

// @dart = 3.3

// ignore_for_file: annotate_overrides, camel_case_types, comment_references
// ignore_for_file: constant_identifier_names
// ignore_for_file: curly_braces_in_flow_control_structures
// ignore_for_file: deprecated_member_use_from_same_package, library_prefixes
// ignore_for_file: non_constant_identifier_names, prefer_relative_imports
// ignore_for_file: unused_import

import 'dart:convert' as $convert;
import 'dart:core' as $core;
import 'dart:typed_data' as $typed_data;

@$core.Deprecated('Use statusDescriptor instead')
const Status$json = {
  '1': 'Status',
  '2': [
    {'1': 'STATUS_UNSPECIFIED', '2': 0},
    {'1': 'STATUS_OK', '2': 1},
    {'1': 'STATUS_ERROR', '2': 2},
    {'1': 'STATUS_UNSUPPORTED', '2': 3},
  ],
};

/// Descriptor for `Status`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List statusDescriptor = $convert.base64Decode(
    'CgZTdGF0dXMSFgoSU1RBVFVTX1VOU1BFQ0lGSUVEEAASDQoJU1RBVFVTX09LEAESEAoMU1RBVF'
    'VTX0VSUk9SEAISFgoSU1RBVFVTX1VOU1VQUE9SVEVEEAM=');

@$core.Deprecated('Use panelModelDescriptor instead')
const PanelModel$json = {
  '1': 'PanelModel',
  '2': [
    {'1': 'PANEL_MODEL_UNSPECIFIED', '2': 0},
    {'1': 'PANEL_MODEL_ST77916', '2': 1},
    {'1': 'PANEL_MODEL_ST7789', '2': 2},
  ],
};

/// Descriptor for `PanelModel`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List panelModelDescriptor = $convert.base64Decode(
    'CgpQYW5lbE1vZGVsEhsKF1BBTkVMX01PREVMX1VOU1BFQ0lGSUVEEAASFwoTUEFORUxfTU9ERU'
    'xfU1Q3NzkxNhABEhYKElBBTkVMX01PREVMX1NUNzc4ORAC');

@$core.Deprecated('Use panelShapeDescriptor instead')
const PanelShape$json = {
  '1': 'PanelShape',
  '2': [
    {'1': 'PANEL_SHAPE_UNSPECIFIED', '2': 0},
    {'1': 'PANEL_SHAPE_RECT', '2': 1},
    {'1': 'PANEL_SHAPE_ROUND', '2': 2},
  ],
};

/// Descriptor for `PanelShape`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List panelShapeDescriptor = $convert.base64Decode(
    'CgpQYW5lbFNoYXBlEhsKF1BBTkVMX1NIQVBFX1VOU1BFQ0lGSUVEEAASFAoQUEFORUxfU0hBUE'
    'VfUkVDVBABEhUKEVBBTkVMX1NIQVBFX1JPVU5EEAI=');

@$core.Deprecated('Use touchPhaseDescriptor instead')
const TouchPhase$json = {
  '1': 'TouchPhase',
  '2': [
    {'1': 'TOUCH_PHASE_UNSPECIFIED', '2': 0},
    {'1': 'TOUCH_PHASE_DOWN', '2': 1},
    {'1': 'TOUCH_PHASE_MOVE', '2': 2},
    {'1': 'TOUCH_PHASE_UP', '2': 3},
  ],
};

/// Descriptor for `TouchPhase`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List touchPhaseDescriptor = $convert.base64Decode(
    'CgpUb3VjaFBoYXNlEhsKF1RPVUNIX1BIQVNFX1VOU1BFQ0lGSUVEEAASFAoQVE9VQ0hfUEhBU0'
    'VfRE9XThABEhQKEFRPVUNIX1BIQVNFX01PVkUQAhISCg5UT1VDSF9QSEFTRV9VUBAD');

@$core.Deprecated('Use otaStateDescriptor instead')
const OtaState$json = {
  '1': 'OtaState',
  '2': [
    {'1': 'OTA_STATE_UNSPECIFIED', '2': 0},
    {'1': 'OTA_STATE_RECEIVING', '2': 1},
    {'1': 'OTA_STATE_VERIFYING', '2': 2},
    {'1': 'OTA_STATE_DONE', '2': 3},
    {'1': 'OTA_STATE_FAILED', '2': 4},
  ],
};

/// Descriptor for `OtaState`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List otaStateDescriptor = $convert.base64Decode(
    'CghPdGFTdGF0ZRIZChVPVEFfU1RBVEVfVU5TUEVDSUZJRUQQABIXChNPVEFfU1RBVEVfUkVDRU'
    'lWSU5HEAESFwoTT1RBX1NUQVRFX1ZFUklGWUlORxACEhIKDk9UQV9TVEFURV9ET05FEAMSFAoQ'
    'T1RBX1NUQVRFX0ZBSUxFRBAE');

@$core.Deprecated('Use authStatusDescriptor instead')
const AuthStatus$json = {
  '1': 'AuthStatus',
  '2': [
    {'1': 'AUTH_STATUS_UNSPECIFIED', '2': 0},
    {'1': 'AUTH_STATUS_OK', '2': 1},
    {'1': 'AUTH_STATUS_UNPROVISIONED', '2': 2},
    {'1': 'AUTH_STATUS_SIGNING_ERROR', '2': 3},
    {'1': 'AUTH_STATUS_MALFORMED_CHALLENGE', '2': 4},
  ],
};

/// Descriptor for `AuthStatus`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List authStatusDescriptor = $convert.base64Decode(
    'CgpBdXRoU3RhdHVzEhsKF0FVVEhfU1RBVFVTX1VOU1BFQ0lGSUVEEAASEgoOQVVUSF9TVEFUVV'
    'NfT0sQARIdChlBVVRIX1NUQVRVU19VTlBST1ZJU0lPTkVEEAISHQoZQVVUSF9TVEFUVVNfU0lH'
    'TklOR19FUlJPUhADEiMKH0FVVEhfU1RBVFVTX01BTEZPUk1FRF9DSEFMTEVOR0UQBA==');

@$core.Deprecated('Use hostToDeviceDescriptor instead')
const HostToDevice$json = {
  '1': 'HostToDevice',
  '2': [
    {
      '1': 'hello',
      '3': 1,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.Hello',
      '9': 0,
      '10': 'hello'
    },
    {
      '1': 'config',
      '3': 2,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.Config',
      '9': 0,
      '10': 'config'
    },
    {
      '1': 'ota_begin',
      '3': 3,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.OtaBegin',
      '9': 0,
      '10': 'otaBegin'
    },
    {
      '1': 'ota_data',
      '3': 4,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.OtaData',
      '9': 0,
      '10': 'otaData'
    },
    {
      '1': 'ota_end',
      '3': 5,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.OtaEnd',
      '9': 0,
      '10': 'otaEnd'
    },
    {
      '1': 'ota_abort',
      '3': 6,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.OtaAbort',
      '9': 0,
      '10': 'otaAbort'
    },
    {
      '1': 'auth_challenge',
      '3': 7,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.AuthChallenge',
      '9': 0,
      '10': 'authChallenge'
    },
    {
      '1': 'get_device_info',
      '3': 16,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.GetDeviceInfo',
      '9': 0,
      '10': 'getDeviceInfo'
    },
    {
      '1': 'set_param',
      '3': 17,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.SetParam',
      '9': 0,
      '10': 'setParam'
    },
    {
      '1': 'haptics',
      '3': 18,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.Haptics',
      '9': 0,
      '10': 'haptics'
    },
    {
      '1': 'keepalive',
      '3': 19,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.Keepalive',
      '9': 0,
      '10': 'keepalive'
    },
  ],
  '8': [
    {'1': 'msg'},
  ],
};

/// Descriptor for `HostToDevice`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List hostToDeviceDescriptor = $convert.base64Decode(
    'CgxIb3N0VG9EZXZpY2USLAoFaGVsbG8YASABKAsyFC5waWNvdmlldy53aXJlLkhlbGxvSABSBW'
    'hlbGxvEi8KBmNvbmZpZxgCIAEoCzIVLnBpY292aWV3LndpcmUuQ29uZmlnSABSBmNvbmZpZxI2'
    'CglvdGFfYmVnaW4YAyABKAsyFy5waWNvdmlldy53aXJlLk90YUJlZ2luSABSCG90YUJlZ2luEj'
    'MKCG90YV9kYXRhGAQgASgLMhYucGljb3ZpZXcud2lyZS5PdGFEYXRhSABSB290YURhdGESMAoH'
    'b3RhX2VuZBgFIAEoCzIVLnBpY292aWV3LndpcmUuT3RhRW5kSABSBm90YUVuZBI2CglvdGFfYW'
    'JvcnQYBiABKAsyFy5waWNvdmlldy53aXJlLk90YUFib3J0SABSCG90YUFib3J0EkUKDmF1dGhf'
    'Y2hhbGxlbmdlGAcgASgLMhwucGljb3ZpZXcud2lyZS5BdXRoQ2hhbGxlbmdlSABSDWF1dGhDaG'
    'FsbGVuZ2USRgoPZ2V0X2RldmljZV9pbmZvGBAgASgLMhwucGljb3ZpZXcud2lyZS5HZXREZXZp'
    'Y2VJbmZvSABSDWdldERldmljZUluZm8SNgoJc2V0X3BhcmFtGBEgASgLMhcucGljb3ZpZXcud2'
    'lyZS5TZXRQYXJhbUgAUghzZXRQYXJhbRIyCgdoYXB0aWNzGBIgASgLMhYucGljb3ZpZXcud2ly'
    'ZS5IYXB0aWNzSABSB2hhcHRpY3MSOAoJa2VlcGFsaXZlGBMgASgLMhgucGljb3ZpZXcud2lyZS'
    '5LZWVwYWxpdmVIAFIJa2VlcGFsaXZlQgUKA21zZw==');

@$core.Deprecated('Use deviceToHostDescriptor instead')
const DeviceToHost$json = {
  '1': 'DeviceToHost',
  '2': [
    {
      '1': 'hello_ack',
      '3': 1,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.HelloAck',
      '9': 0,
      '10': 'helloAck'
    },
    {
      '1': 'touch',
      '3': 2,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.Touch',
      '9': 0,
      '10': 'touch'
    },
    {
      '1': 'ota_status',
      '3': 3,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.OtaStatus',
      '9': 0,
      '10': 'otaStatus'
    },
    {
      '1': 'auth_response',
      '3': 4,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.AuthResponse',
      '9': 0,
      '10': 'authResponse'
    },
    {
      '1': 'config_ack',
      '3': 5,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.ConfigAck',
      '9': 0,
      '10': 'configAck'
    },
    {
      '1': 'device_info',
      '3': 16,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.DeviceInfo',
      '9': 0,
      '10': 'deviceInfo'
    },
    {
      '1': 'param_ack',
      '3': 17,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.ParamAck',
      '9': 0,
      '10': 'paramAck'
    },
  ],
  '8': [
    {'1': 'msg'},
  ],
};

/// Descriptor for `DeviceToHost`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List deviceToHostDescriptor = $convert.base64Decode(
    'CgxEZXZpY2VUb0hvc3QSNgoJaGVsbG9fYWNrGAEgASgLMhcucGljb3ZpZXcud2lyZS5IZWxsb0'
    'Fja0gAUghoZWxsb0FjaxIsCgV0b3VjaBgCIAEoCzIULnBpY292aWV3LndpcmUuVG91Y2hIAFIF'
    'dG91Y2gSOQoKb3RhX3N0YXR1cxgDIAEoCzIYLnBpY292aWV3LndpcmUuT3RhU3RhdHVzSABSCW'
    '90YVN0YXR1cxJCCg1hdXRoX3Jlc3BvbnNlGAQgASgLMhsucGljb3ZpZXcud2lyZS5BdXRoUmVz'
    'cG9uc2VIAFIMYXV0aFJlc3BvbnNlEjkKCmNvbmZpZ19hY2sYBSABKAsyGC5waWNvdmlldy53aX'
    'JlLkNvbmZpZ0Fja0gAUgljb25maWdBY2sSPAoLZGV2aWNlX2luZm8YECABKAsyGS5waWNvdmll'
    'dy53aXJlLkRldmljZUluZm9IAFIKZGV2aWNlSW5mbxI2CglwYXJhbV9hY2sYESABKAsyFy5waW'
    'Nvdmlldy53aXJlLlBhcmFtQWNrSABSCHBhcmFtQWNrQgUKA21zZw==');

@$core.Deprecated('Use helloDescriptor instead')
const Hello$json = {
  '1': 'Hello',
  '2': [
    {'1': 'proto_version', '3': 1, '4': 1, '5': 13, '10': 'protoVersion'},
  ],
};

/// Descriptor for `Hello`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List helloDescriptor = $convert.base64Decode(
    'CgVIZWxsbxIjCg1wcm90b192ZXJzaW9uGAEgASgNUgxwcm90b1ZlcnNpb24=');

@$core.Deprecated('Use capabilitiesDescriptor instead')
const Capabilities$json = {
  '1': 'Capabilities',
  '2': [
    {'1': 'set_param', '3': 1, '4': 1, '5': 8, '10': 'setParam'},
    {'1': 'haptics', '3': 2, '4': 1, '5': 8, '10': 'haptics'},
    {'1': 'auth', '3': 3, '4': 1, '5': 8, '10': 'auth'},
  ],
};

/// Descriptor for `Capabilities`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List capabilitiesDescriptor = $convert.base64Decode(
    'CgxDYXBhYmlsaXRpZXMSGwoJc2V0X3BhcmFtGAEgASgIUghzZXRQYXJhbRIYCgdoYXB0aWNzGA'
    'IgASgIUgdoYXB0aWNzEhIKBGF1dGgYAyABKAhSBGF1dGg=');

@$core.Deprecated('Use helloAckDescriptor instead')
const HelloAck$json = {
  '1': 'HelloAck',
  '2': [
    {'1': 'proto_version', '3': 1, '4': 1, '5': 13, '10': 'protoVersion'},
    {
      '1': 'caps',
      '3': 2,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.Capabilities',
      '10': 'caps'
    },
    {'1': 'fw_version', '3': 3, '4': 1, '5': 9, '10': 'fwVersion'},
  ],
};

/// Descriptor for `HelloAck`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List helloAckDescriptor = $convert.base64Decode(
    'CghIZWxsb0FjaxIjCg1wcm90b192ZXJzaW9uGAEgASgNUgxwcm90b1ZlcnNpb24SLwoEY2Fwcx'
    'gCIAEoCzIbLnBpY292aWV3LndpcmUuQ2FwYWJpbGl0aWVzUgRjYXBzEh0KCmZ3X3ZlcnNpb24Y'
    'AyABKAlSCWZ3VmVyc2lvbg==');

@$core.Deprecated('Use configDescriptor instead')
const Config$json = {
  '1': 'Config',
  '2': [
    {
      '1': 'model',
      '3': 1,
      '4': 1,
      '5': 14,
      '6': '.picoview.wire.PanelModel',
      '10': 'model'
    },
    {'1': 'width', '3': 2, '4': 1, '5': 13, '10': 'width'},
    {'1': 'height', '3': 3, '4': 1, '5': 13, '10': 'height'},
    {'1': 'x_offset', '3': 4, '4': 1, '5': 13, '10': 'xOffset'},
    {'1': 'y_offset', '3': 5, '4': 1, '5': 13, '10': 'yOffset'},
    {'1': 'rotation_deg', '3': 6, '4': 1, '5': 13, '10': 'rotationDeg'},
    {'1': 'invert', '3': 7, '4': 1, '5': 8, '10': 'invert'},
    {'1': 'touch_addr', '3': 8, '4': 1, '5': 13, '10': 'touchAddr'},
    {'1': 'touch_swap_xy', '3': 9, '4': 1, '5': 8, '10': 'touchSwapXy'},
    {'1': 'touch_flip_x', '3': 10, '4': 1, '5': 8, '10': 'touchFlipX'},
    {'1': 'touch_flip_y', '3': 11, '4': 1, '5': 8, '10': 'touchFlipY'},
  ],
};

/// Descriptor for `Config`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List configDescriptor = $convert.base64Decode(
    'CgZDb25maWcSLwoFbW9kZWwYASABKA4yGS5waWNvdmlldy53aXJlLlBhbmVsTW9kZWxSBW1vZG'
    'VsEhQKBXdpZHRoGAIgASgNUgV3aWR0aBIWCgZoZWlnaHQYAyABKA1SBmhlaWdodBIZCgh4X29m'
    'ZnNldBgEIAEoDVIHeE9mZnNldBIZCgh5X29mZnNldBgFIAEoDVIHeU9mZnNldBIhCgxyb3RhdG'
    'lvbl9kZWcYBiABKA1SC3JvdGF0aW9uRGVnEhYKBmludmVydBgHIAEoCFIGaW52ZXJ0Eh0KCnRv'
    'dWNoX2FkZHIYCCABKA1SCXRvdWNoQWRkchIiCg10b3VjaF9zd2FwX3h5GAkgASgIUgt0b3VjaF'
    'N3YXBYeRIgCgx0b3VjaF9mbGlwX3gYCiABKAhSCnRvdWNoRmxpcFgSIAoMdG91Y2hfZmxpcF95'
    'GAsgASgIUgp0b3VjaEZsaXBZ');

@$core.Deprecated('Use configAckDescriptor instead')
const ConfigAck$json = {
  '1': 'ConfigAck',
  '2': [
    {
      '1': 'status',
      '3': 1,
      '4': 1,
      '5': 14,
      '6': '.picoview.wire.Status',
      '10': 'status'
    },
    {'1': 'detail', '3': 2, '4': 1, '5': 9, '10': 'detail'},
  ],
};

/// Descriptor for `ConfigAck`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List configAckDescriptor = $convert.base64Decode(
    'CglDb25maWdBY2sSLQoGc3RhdHVzGAEgASgOMhUucGljb3ZpZXcud2lyZS5TdGF0dXNSBnN0YX'
    'R1cxIWCgZkZXRhaWwYAiABKAlSBmRldGFpbA==');

@$core.Deprecated('Use touchDescriptor instead')
const Touch$json = {
  '1': 'Touch',
  '2': [
    {
      '1': 'phase',
      '3': 1,
      '4': 1,
      '5': 14,
      '6': '.picoview.wire.TouchPhase',
      '10': 'phase'
    },
    {'1': 'x', '3': 2, '4': 1, '5': 13, '10': 'x'},
    {'1': 'y', '3': 3, '4': 1, '5': 13, '10': 'y'},
  ],
};

/// Descriptor for `Touch`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List touchDescriptor = $convert.base64Decode(
    'CgVUb3VjaBIvCgVwaGFzZRgBIAEoDjIZLnBpY292aWV3LndpcmUuVG91Y2hQaGFzZVIFcGhhc2'
    'USDAoBeBgCIAEoDVIBeBIMCgF5GAMgASgNUgF5');

@$core.Deprecated('Use otaBeginDescriptor instead')
const OtaBegin$json = {
  '1': 'OtaBegin',
  '2': [
    {'1': 'image_size', '3': 1, '4': 1, '5': 13, '10': 'imageSize'},
    {'1': 'sha256', '3': 2, '4': 1, '5': 12, '10': 'sha256'},
    {'1': 'version', '3': 3, '4': 1, '5': 9, '10': 'version'},
  ],
};

/// Descriptor for `OtaBegin`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List otaBeginDescriptor = $convert.base64Decode(
    'CghPdGFCZWdpbhIdCgppbWFnZV9zaXplGAEgASgNUglpbWFnZVNpemUSFgoGc2hhMjU2GAIgAS'
    'gMUgZzaGEyNTYSGAoHdmVyc2lvbhgDIAEoCVIHdmVyc2lvbg==');

@$core.Deprecated('Use otaDataDescriptor instead')
const OtaData$json = {
  '1': 'OtaData',
  '2': [
    {'1': 'seq', '3': 1, '4': 1, '5': 13, '10': 'seq'},
    {'1': 'data', '3': 2, '4': 1, '5': 12, '10': 'data'},
  ],
};

/// Descriptor for `OtaData`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List otaDataDescriptor = $convert.base64Decode(
    'CgdPdGFEYXRhEhAKA3NlcRgBIAEoDVIDc2VxEhIKBGRhdGEYAiABKAxSBGRhdGE=');

@$core.Deprecated('Use otaEndDescriptor instead')
const OtaEnd$json = {
  '1': 'OtaEnd',
};

/// Descriptor for `OtaEnd`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List otaEndDescriptor =
    $convert.base64Decode('CgZPdGFFbmQ=');

@$core.Deprecated('Use otaAbortDescriptor instead')
const OtaAbort$json = {
  '1': 'OtaAbort',
};

/// Descriptor for `OtaAbort`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List otaAbortDescriptor =
    $convert.base64Decode('CghPdGFBYm9ydA==');

@$core.Deprecated('Use otaStatusDescriptor instead')
const OtaStatus$json = {
  '1': 'OtaStatus',
  '2': [
    {
      '1': 'state',
      '3': 1,
      '4': 1,
      '5': 14,
      '6': '.picoview.wire.OtaState',
      '10': 'state'
    },
    {'1': 'pct', '3': 2, '4': 1, '5': 13, '10': 'pct'},
    {'1': 'err', '3': 3, '4': 1, '5': 17, '10': 'err'},
  ],
};

/// Descriptor for `OtaStatus`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List otaStatusDescriptor = $convert.base64Decode(
    'CglPdGFTdGF0dXMSLQoFc3RhdGUYASABKA4yFy5waWNvdmlldy53aXJlLk90YVN0YXRlUgVzdG'
    'F0ZRIQCgNwY3QYAiABKA1SA3BjdBIQCgNlcnIYAyABKBFSA2Vycg==');

@$core.Deprecated('Use keepaliveDescriptor instead')
const Keepalive$json = {
  '1': 'Keepalive',
};

/// Descriptor for `Keepalive`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List keepaliveDescriptor =
    $convert.base64Decode('CglLZWVwYWxpdmU=');

@$core.Deprecated('Use authChallengeDescriptor instead')
const AuthChallenge$json = {
  '1': 'AuthChallenge',
  '2': [
    {'1': 'nonce', '3': 1, '4': 1, '5': 12, '10': 'nonce'},
  ],
};

/// Descriptor for `AuthChallenge`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List authChallengeDescriptor = $convert
    .base64Decode('Cg1BdXRoQ2hhbGxlbmdlEhQKBW5vbmNlGAEgASgMUgVub25jZQ==');

@$core.Deprecated('Use authResponseDescriptor instead')
const AuthResponse$json = {
  '1': 'AuthResponse',
  '2': [
    {
      '1': 'status',
      '3': 1,
      '4': 1,
      '5': 14,
      '6': '.picoview.wire.AuthStatus',
      '10': 'status'
    },
    {'1': 'certificate', '3': 2, '4': 1, '5': 12, '10': 'certificate'},
    {'1': 'signature', '3': 3, '4': 1, '5': 12, '10': 'signature'},
  ],
};

/// Descriptor for `AuthResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List authResponseDescriptor = $convert.base64Decode(
    'CgxBdXRoUmVzcG9uc2USMQoGc3RhdHVzGAEgASgOMhkucGljb3ZpZXcud2lyZS5BdXRoU3RhdH'
    'VzUgZzdGF0dXMSIAoLY2VydGlmaWNhdGUYAiABKAxSC2NlcnRpZmljYXRlEhwKCXNpZ25hdHVy'
    'ZRgDIAEoDFIJc2lnbmF0dXJl');

@$core.Deprecated('Use getDeviceInfoDescriptor instead')
const GetDeviceInfo$json = {
  '1': 'GetDeviceInfo',
};

/// Descriptor for `GetDeviceInfo`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List getDeviceInfoDescriptor =
    $convert.base64Decode('Cg1HZXREZXZpY2VJbmZv');

@$core.Deprecated('Use panelGeometryDescriptor instead')
const PanelGeometry$json = {
  '1': 'PanelGeometry',
  '2': [
    {'1': 'width', '3': 1, '4': 1, '5': 13, '10': 'width'},
    {'1': 'height', '3': 2, '4': 1, '5': 13, '10': 'height'},
    {
      '1': 'shape',
      '3': 3,
      '4': 1,
      '5': 14,
      '6': '.picoview.wire.PanelShape',
      '10': 'shape'
    },
  ],
};

/// Descriptor for `PanelGeometry`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List panelGeometryDescriptor = $convert.base64Decode(
    'Cg1QYW5lbEdlb21ldHJ5EhQKBXdpZHRoGAEgASgNUgV3aWR0aBIWCgZoZWlnaHQYAiABKA1SBm'
    'hlaWdodBIvCgVzaGFwZRgDIAEoDjIZLnBpY292aWV3LndpcmUuUGFuZWxTaGFwZVIFc2hhcGU=');

@$core.Deprecated('Use deviceInfoDescriptor instead')
const DeviceInfo$json = {
  '1': 'DeviceInfo',
  '2': [
    {'1': 'device_id', '3': 1, '4': 1, '5': 9, '10': 'deviceId'},
    {'1': 'serial', '3': 2, '4': 1, '5': 9, '10': 'serial'},
    {'1': 'fw_version', '3': 3, '4': 1, '5': 9, '10': 'fwVersion'},
    {'1': 'proto_version', '3': 4, '4': 1, '5': 13, '10': 'protoVersion'},
    {
      '1': 'panel',
      '3': 5,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.PanelGeometry',
      '10': 'panel'
    },
    {
      '1': 'caps',
      '3': 6,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.Capabilities',
      '10': 'caps'
    },
  ],
};

/// Descriptor for `DeviceInfo`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List deviceInfoDescriptor = $convert.base64Decode(
    'CgpEZXZpY2VJbmZvEhsKCWRldmljZV9pZBgBIAEoCVIIZGV2aWNlSWQSFgoGc2VyaWFsGAIgAS'
    'gJUgZzZXJpYWwSHQoKZndfdmVyc2lvbhgDIAEoCVIJZndWZXJzaW9uEiMKDXByb3RvX3ZlcnNp'
    'b24YBCABKA1SDHByb3RvVmVyc2lvbhIyCgVwYW5lbBgFIAEoCzIcLnBpY292aWV3LndpcmUuUG'
    'FuZWxHZW9tZXRyeVIFcGFuZWwSLwoEY2FwcxgGIAEoCzIbLnBpY292aWV3LndpcmUuQ2FwYWJp'
    'bGl0aWVzUgRjYXBz');

@$core.Deprecated('Use setParamDescriptor instead')
const SetParam$json = {
  '1': 'SetParam',
  '2': [
    {'1': 'brightness', '3': 1, '4': 1, '5': 13, '9': 0, '10': 'brightness'},
  ],
  '8': [
    {'1': 'param'},
  ],
};

/// Descriptor for `SetParam`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List setParamDescriptor = $convert.base64Decode(
    'CghTZXRQYXJhbRIgCgpicmlnaHRuZXNzGAEgASgNSABSCmJyaWdodG5lc3NCBwoFcGFyYW0=');

@$core.Deprecated('Use paramAckDescriptor instead')
const ParamAck$json = {
  '1': 'ParamAck',
  '2': [
    {
      '1': 'status',
      '3': 1,
      '4': 1,
      '5': 14,
      '6': '.picoview.wire.Status',
      '10': 'status'
    },
    {'1': 'detail', '3': 2, '4': 1, '5': 9, '10': 'detail'},
  ],
};

/// Descriptor for `ParamAck`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List paramAckDescriptor = $convert.base64Decode(
    'CghQYXJhbUFjaxItCgZzdGF0dXMYASABKA4yFS5waWNvdmlldy53aXJlLlN0YXR1c1IGc3RhdH'
    'VzEhYKBmRldGFpbBgCIAEoCVIGZGV0YWls');

@$core.Deprecated('Use hapticsDescriptor instead')
const Haptics$json = {
  '1': 'Haptics',
  '2': [
    {
      '1': 'play',
      '3': 1,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.HapticsPlay',
      '9': 0,
      '10': 'play'
    },
    {
      '1': 'stop',
      '3': 2,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.HapticsStop',
      '9': 0,
      '10': 'stop'
    },
  ],
  '8': [
    {'1': 'cmd'},
  ],
};

/// Descriptor for `Haptics`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List hapticsDescriptor = $convert.base64Decode(
    'CgdIYXB0aWNzEjAKBHBsYXkYASABKAsyGi5waWNvdmlldy53aXJlLkhhcHRpY3NQbGF5SABSBH'
    'BsYXkSMAoEc3RvcBgCIAEoCzIaLnBpY292aWV3LndpcmUuSGFwdGljc1N0b3BIAFIEc3RvcEIF'
    'CgNjbWQ=');

@$core.Deprecated('Use hapticsPlayDescriptor instead')
const HapticsPlay$json = {
  '1': 'HapticsPlay',
  '2': [
    {'1': 'effect', '3': 1, '4': 1, '5': 13, '10': 'effect'},
    {'1': 'library', '3': 2, '4': 1, '5': 13, '10': 'library'},
  ],
};

/// Descriptor for `HapticsPlay`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List hapticsPlayDescriptor = $convert.base64Decode(
    'CgtIYXB0aWNzUGxheRIWCgZlZmZlY3QYASABKA1SBmVmZmVjdBIYCgdsaWJyYXJ5GAIgASgNUg'
    'dsaWJyYXJ5');

@$core.Deprecated('Use hapticsStopDescriptor instead')
const HapticsStop$json = {
  '1': 'HapticsStop',
};

/// Descriptor for `HapticsStop`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List hapticsStopDescriptor =
    $convert.base64Decode('CgtIYXB0aWNzU3RvcA==');
