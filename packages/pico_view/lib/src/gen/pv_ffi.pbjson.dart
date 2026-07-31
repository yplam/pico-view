// This is a generated file - do not edit.
//
// Generated from pv_ffi.proto.

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

@$core.Deprecated('Use errorCodeDescriptor instead')
const ErrorCode$json = {
  '1': 'ErrorCode',
  '2': [
    {'1': 'ERROR_CODE_UNSPECIFIED', '2': 0},
    {'1': 'ERROR_CODE_BAD_REQUEST', '2': 1},
    {'1': 'ERROR_CODE_ALREADY_OPEN', '2': 2},
    {'1': 'ERROR_CODE_NOT_OPEN', '2': 3},
    {'1': 'ERROR_CODE_DEVICE', '2': 4},
    {'1': 'ERROR_CODE_ENQUEUE', '2': 5},
    {'1': 'ERROR_CODE_UNSUPPORTED', '2': 6},
    {'1': 'ERROR_CODE_INTERNAL', '2': 7},
    {'1': 'ERROR_CODE_TIMEOUT', '2': 8},
  ],
};

/// Descriptor for `ErrorCode`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List errorCodeDescriptor = $convert.base64Decode(
    'CglFcnJvckNvZGUSGgoWRVJST1JfQ09ERV9VTlNQRUNJRklFRBAAEhoKFkVSUk9SX0NPREVfQk'
    'FEX1JFUVVFU1QQARIbChdFUlJPUl9DT0RFX0FMUkVBRFlfT1BFThACEhcKE0VSUk9SX0NPREVf'
    'Tk9UX09QRU4QAxIVChFFUlJPUl9DT0RFX0RFVklDRRAEEhYKEkVSUk9SX0NPREVfRU5RVUVVRR'
    'AFEhoKFkVSUk9SX0NPREVfVU5TVVBQT1JURUQQBhIXChNFUlJPUl9DT0RFX0lOVEVSTkFMEAcS'
    'FgoSRVJST1JfQ09ERV9USU1FT1VUEAg=');

@$core.Deprecated('Use linkStateDescriptor instead')
const LinkState$json = {
  '1': 'LinkState',
  '2': [
    {'1': 'LINK_STATE_UNSPECIFIED', '2': 0},
    {'1': 'LINK_STATE_CONNECTED', '2': 1},
    {'1': 'LINK_STATE_DISCONNECTED', '2': 2},
  ],
};

/// Descriptor for `LinkState`. Decode as a `google.protobuf.EnumDescriptorProto`.
final $typed_data.Uint8List linkStateDescriptor = $convert.base64Decode(
    'CglMaW5rU3RhdGUSGgoWTElOS19TVEFURV9VTlNQRUNJRklFRBAAEhgKFExJTktfU1RBVEVfQ0'
    '9OTkVDVEVEEAESGwoXTElOS19TVEFURV9ESVNDT05ORUNURUQQAg==');

@$core.Deprecated('Use pvRequestDescriptor instead')
const PvRequest$json = {
  '1': 'PvRequest',
  '2': [
    {'1': 'id', '3': 1, '4': 1, '5': 13, '10': 'id'},
    {'1': 'timeout_ms', '3': 2, '4': 1, '5': 13, '10': 'timeoutMs'},
    {
      '1': 'open_device',
      '3': 3,
      '4': 1,
      '5': 11,
      '6': '.picoview.ffi.OpenDevice',
      '9': 0,
      '10': 'openDevice'
    },
    {
      '1': 'close_device',
      '3': 4,
      '4': 1,
      '5': 11,
      '6': '.picoview.ffi.CloseDevice',
      '9': 0,
      '10': 'closeDevice'
    },
    {
      '1': 'ota_start',
      '3': 5,
      '4': 1,
      '5': 11,
      '6': '.picoview.ffi.OtaStart',
      '9': 0,
      '10': 'otaStart'
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
  ],
  '8': [
    {'1': 'req'},
  ],
};

/// Descriptor for `PvRequest`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List pvRequestDescriptor = $convert.base64Decode(
    'CglQdlJlcXVlc3QSDgoCaWQYASABKA1SAmlkEh0KCnRpbWVvdXRfbXMYAiABKA1SCXRpbWVvdX'
    'RNcxI7CgtvcGVuX2RldmljZRgDIAEoCzIYLnBpY292aWV3LmZmaS5PcGVuRGV2aWNlSABSCm9w'
    'ZW5EZXZpY2USPgoMY2xvc2VfZGV2aWNlGAQgASgLMhkucGljb3ZpZXcuZmZpLkNsb3NlRGV2aW'
    'NlSABSC2Nsb3NlRGV2aWNlEjUKCW90YV9zdGFydBgFIAEoCzIWLnBpY292aWV3LmZmaS5PdGFT'
    'dGFydEgAUghvdGFTdGFydBJGCg9nZXRfZGV2aWNlX2luZm8YECABKAsyHC5waWNvdmlldy53aX'
    'JlLkdldERldmljZUluZm9IAFINZ2V0RGV2aWNlSW5mbxI2CglzZXRfcGFyYW0YESABKAsyFy5w'
    'aWNvdmlldy53aXJlLlNldFBhcmFtSABSCHNldFBhcmFtEjIKB2hhcHRpY3MYEiABKAsyFi5waW'
    'Nvdmlldy53aXJlLkhhcHRpY3NIAFIHaGFwdGljc0IFCgNyZXE=');

@$core.Deprecated('Use openDeviceDescriptor instead')
const OpenDevice$json = {
  '1': 'OpenDevice',
  '2': [
    {'1': 'index', '3': 1, '4': 1, '5': 13, '10': 'index'},
    {'1': 'model', '3': 2, '4': 1, '5': 9, '10': 'model'},
    {'1': 'serial', '3': 3, '4': 1, '5': 9, '10': 'serial'},
  ],
};

/// Descriptor for `OpenDevice`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List openDeviceDescriptor = $convert.base64Decode(
    'CgpPcGVuRGV2aWNlEhQKBWluZGV4GAEgASgNUgVpbmRleBIUCgVtb2RlbBgCIAEoCVIFbW9kZW'
    'wSFgoGc2VyaWFsGAMgASgJUgZzZXJpYWw=');

@$core.Deprecated('Use closeDeviceDescriptor instead')
const CloseDevice$json = {
  '1': 'CloseDevice',
};

/// Descriptor for `CloseDevice`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List closeDeviceDescriptor =
    $convert.base64Decode('CgtDbG9zZURldmljZQ==');

@$core.Deprecated('Use otaStartDescriptor instead')
const OtaStart$json = {
  '1': 'OtaStart',
  '2': [
    {'1': 'image', '3': 1, '4': 1, '5': 12, '10': 'image'},
  ],
};

/// Descriptor for `OtaStart`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List otaStartDescriptor =
    $convert.base64Decode('CghPdGFTdGFydBIUCgVpbWFnZRgBIAEoDFIFaW1hZ2U=');

@$core.Deprecated('Use pvResponseDescriptor instead')
const PvResponse$json = {
  '1': 'PvResponse',
  '2': [
    {'1': 'id', '3': 1, '4': 1, '5': 13, '10': 'id'},
    {
      '1': 'ack',
      '3': 2,
      '4': 1,
      '5': 11,
      '6': '.picoview.ffi.Ack',
      '9': 0,
      '10': 'ack'
    },
    {
      '1': 'error',
      '3': 3,
      '4': 1,
      '5': 11,
      '6': '.picoview.ffi.Error',
      '9': 0,
      '10': 'error'
    },
    {
      '1': 'device_info',
      '3': 4,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.DeviceInfo',
      '9': 0,
      '10': 'deviceInfo'
    },
  ],
  '8': [
    {'1': 'resp'},
  ],
};

/// Descriptor for `PvResponse`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List pvResponseDescriptor = $convert.base64Decode(
    'CgpQdlJlc3BvbnNlEg4KAmlkGAEgASgNUgJpZBIlCgNhY2sYAiABKAsyES5waWNvdmlldy5mZm'
    'kuQWNrSABSA2FjaxIrCgVlcnJvchgDIAEoCzITLnBpY292aWV3LmZmaS5FcnJvckgAUgVlcnJv'
    'chI8CgtkZXZpY2VfaW5mbxgEIAEoCzIZLnBpY292aWV3LndpcmUuRGV2aWNlSW5mb0gAUgpkZX'
    'ZpY2VJbmZvQgYKBHJlc3A=');

@$core.Deprecated('Use ackDescriptor instead')
const Ack$json = {
  '1': 'Ack',
};

/// Descriptor for `Ack`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List ackDescriptor = $convert.base64Decode('CgNBY2s=');

@$core.Deprecated('Use errorDescriptor instead')
const Error$json = {
  '1': 'Error',
  '2': [
    {
      '1': 'code',
      '3': 1,
      '4': 1,
      '5': 14,
      '6': '.picoview.ffi.ErrorCode',
      '10': 'code'
    },
    {'1': 'message', '3': 2, '4': 1, '5': 9, '10': 'message'},
  ],
};

/// Descriptor for `Error`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List errorDescriptor = $convert.base64Decode(
    'CgVFcnJvchIrCgRjb2RlGAEgASgOMhcucGljb3ZpZXcuZmZpLkVycm9yQ29kZVIEY29kZRIYCg'
    'dtZXNzYWdlGAIgASgJUgdtZXNzYWdl');

@$core.Deprecated('Use linkEventDescriptor instead')
const LinkEvent$json = {
  '1': 'LinkEvent',
  '2': [
    {
      '1': 'state',
      '3': 1,
      '4': 1,
      '5': 14,
      '6': '.picoview.ffi.LinkState',
      '10': 'state'
    },
    {'1': 'detail', '3': 2, '4': 1, '5': 9, '10': 'detail'},
    {'1': 'verified', '3': 3, '4': 1, '5': 8, '10': 'verified'},
    {'1': 'device_id', '3': 4, '4': 1, '5': 9, '10': 'deviceId'},
    {'1': 'fw_version', '3': 5, '4': 1, '5': 9, '10': 'fwVersion'},
  ],
};

/// Descriptor for `LinkEvent`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List linkEventDescriptor = $convert.base64Decode(
    'CglMaW5rRXZlbnQSLQoFc3RhdGUYASABKA4yFy5waWNvdmlldy5mZmkuTGlua1N0YXRlUgVzdG'
    'F0ZRIWCgZkZXRhaWwYAiABKAlSBmRldGFpbBIaCgh2ZXJpZmllZBgDIAEoCFIIdmVyaWZpZWQS'
    'GwoJZGV2aWNlX2lkGAQgASgJUghkZXZpY2VJZBIdCgpmd192ZXJzaW9uGAUgASgJUglmd1Zlcn'
    'Npb24=');

@$core.Deprecated('Use pvEventDescriptor instead')
const PvEvent$json = {
  '1': 'PvEvent',
  '2': [
    {
      '1': 'touch',
      '3': 1,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.Touch',
      '9': 0,
      '10': 'touch'
    },
    {
      '1': 'link',
      '3': 2,
      '4': 1,
      '5': 11,
      '6': '.picoview.ffi.LinkEvent',
      '9': 0,
      '10': 'link'
    },
    {
      '1': 'ota',
      '3': 3,
      '4': 1,
      '5': 11,
      '6': '.picoview.wire.OtaStatus',
      '9': 0,
      '10': 'ota'
    },
    {
      '1': 'response',
      '3': 4,
      '4': 1,
      '5': 11,
      '6': '.picoview.ffi.PvResponse',
      '9': 0,
      '10': 'response'
    },
  ],
  '8': [
    {'1': 'event'},
  ],
};

/// Descriptor for `PvEvent`. Decode as a `google.protobuf.DescriptorProto`.
final $typed_data.Uint8List pvEventDescriptor = $convert.base64Decode(
    'CgdQdkV2ZW50EiwKBXRvdWNoGAEgASgLMhQucGljb3ZpZXcud2lyZS5Ub3VjaEgAUgV0b3VjaB'
    'ItCgRsaW5rGAIgASgLMhcucGljb3ZpZXcuZmZpLkxpbmtFdmVudEgAUgRsaW5rEiwKA290YRgD'
    'IAEoCzIYLnBpY292aWV3LndpcmUuT3RhU3RhdHVzSABSA290YRI2CghyZXNwb25zZRgEIAEoCz'
    'IYLnBpY292aWV3LmZmaS5QdlJlc3BvbnNlSABSCHJlc3BvbnNlQgcKBWV2ZW50');
