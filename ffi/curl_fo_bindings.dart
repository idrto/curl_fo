// curl_fo Dart FFI bindings for Flutter.
//
// Usage:
//   final lib = CurlFoLibrary.open();
//   final cfg = lib.configCreate();
//   lib.configSetVerbose(cfg, 1);
//   final ctx = lib.ctxCreate(cfg);
//   final code = lib.easyPerform(ctx, curlHandle);
//
// ignore_for_file: non_constant_identifier_names

import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

typedef _CfConfigCreateNative = Pointer<Void> Function();
typedef _CfConfigCreate = Pointer<Void> Function();

typedef _CfConfigDestroyNative = Void Function(Pointer<Void>);
typedef _CfConfigDestroy = void Function(Pointer<Void>);

typedef _CfConfigSetVerboseNative = Void Function(Pointer<Void>, Int32);
typedef _CfConfigSetVerbose = void Function(Pointer<Void>, int);

typedef _CfConfigSetFailoverGatewayNative = Void Function(Pointer<Void>, Int32);
typedef _CfConfigSetFailoverGateway = void Function(Pointer<Void>, int);

typedef _CfConfigSetTopIpsNative = Void Function(Pointer<Void>, IntPtr);
typedef _CfConfigSetTopIps = void Function(Pointer<Void>, int);

typedef _CfConfigSetConnectTimeoutMsNative = Void Function(Pointer<Void>, Int32);
typedef _CfConfigSetConnectTimeoutMs = void Function(Pointer<Void>, int);

typedef _CfCtxCreateNative = Pointer<Void> Function(Pointer<Void>);
typedef _CfCtxCreate = Pointer<Void> Function(Pointer<Void>);

typedef _CfCtxCreateDefaultNative = Pointer<Void> Function();
typedef _CfCtxCreateDefault = Pointer<Void> Function();

typedef _CfCtxDestroyNative = Void Function(Pointer<Void>);
typedef _CfCtxDestroy = void Function(Pointer<Void>);

typedef _CfEasyPerformNative = Int32 Function(Pointer<Void>, Pointer<Void>);
typedef _CfEasyPerform = int Function(Pointer<Void>, Pointer<Void>);

typedef _CfRequestNative = Int32 Function(Pointer<Void>, Pointer<Utf8>, Int32);
typedef _CfRequest = int Function(Pointer<Void>, Pointer<Utf8>, int);

typedef _CfShouldFailoverNative = Int32 Function(Int32, Int32);
typedef _CfShouldFailover = int Function(int, int);

typedef _CfWsConnectNative = Pointer<Void> Function(Pointer<Void>, Pointer<Utf8>);
typedef _CfWsConnect = Pointer<Void> Function(Pointer<Void>, Pointer<Utf8>);

typedef _CfWsCloseNative = Void Function(Pointer<Void>);
typedef _CfWsClose = void Function(Pointer<Void>);

class CurlFoLibrary {
  CurlFoLibrary(this._lib);

  final DynamicLibrary _lib;

  static const int methodGet = 1;
  static const int methodHead = 2;
  static const int methodOther = 3;

  static CurlFoLibrary open() {
    final DynamicLibrary lib;
    if (Platform.isWindows) {
      lib = DynamicLibrary.open('curl_fo.dll');
    } else if (Platform.isMacOS) {
      lib = DynamicLibrary.open('libcurl_fo.dylib');
    } else {
      lib = DynamicLibrary.open('libcurl_fo.so');
    }
    return CurlFoLibrary(lib);
  }

  Pointer<Void> configCreate() {
    final fn = _lib.lookupFunction<_CfConfigCreateNative, _CfConfigCreate>(
        'cf_config_create');
    return fn();
  }

  void configDestroy(Pointer<Void> cfg) {
    final fn = _lib.lookupFunction<_CfConfigDestroyNative, _CfConfigDestroy>(
        'cf_config_destroy');
    fn(cfg);
  }

  void configSetVerbose(Pointer<Void> cfg, int on) {
    final fn = _lib.lookupFunction<_CfConfigSetVerboseNative,
        _CfConfigSetVerbose>('cf_config_set_verbose');
    fn(cfg, on);
  }

  void configSetFailoverGateway(Pointer<Void> cfg, int on) {
    final fn = _lib.lookupFunction<_CfConfigSetFailoverGatewayNative,
        _CfConfigSetFailoverGateway>('cf_config_set_failover_gateway');
    fn(cfg, on);
  }

  void configSetTopIps(Pointer<Void> cfg, int n) {
    final fn = _lib.lookupFunction<_CfConfigSetTopIpsNative, _CfConfigSetTopIps>(
        'cf_config_set_top_ips');
    fn(cfg, n);
  }

  void configSetConnectTimeoutMs(Pointer<Void> cfg, int ms) {
    final fn = _lib.lookupFunction<_CfConfigSetConnectTimeoutMsNative,
        _CfConfigSetConnectTimeoutMs>('cf_config_set_connect_timeout_ms');
    fn(cfg, ms);
  }

  Pointer<Void> ctxCreate(Pointer<Void> cfg) {
    final fn =
        _lib.lookupFunction<_CfCtxCreateNative, _CfCtxCreate>('cf_ctx_create');
    return fn(cfg);
  }

  Pointer<Void> ctxCreateDefault() {
    final fn = _lib.lookupFunction<_CfCtxCreateDefaultNative,
        _CfCtxCreateDefault>('cf_ctx_create_default');
    return fn();
  }

  void ctxDestroy(Pointer<Void> ctx) {
    final fn =
        _lib.lookupFunction<_CfCtxDestroyNative, _CfCtxDestroy>('cf_ctx_destroy');
    fn(ctx);
  }

  int easyPerform(Pointer<Void> ctx, Pointer<Void> curl) {
    final fn = _lib.lookupFunction<_CfEasyPerformNative, _CfEasyPerform>(
        'cf_easy_perform');
    return fn(ctx, curl);
  }

  int request(Pointer<Void> ctx, String url, int method) {
    final fn =
        _lib.lookupFunction<_CfRequestNative, _CfRequest>('cf_request');
    final urlPtr = url.toNativeUtf8();
    try {
      return fn(ctx, urlPtr, method);
    } finally {
      malloc.free(urlPtr);
    }
  }

  int shouldFailover(int curlCode, int httpCode) {
    final fn = _lib.lookupFunction<_CfShouldFailoverNative, _CfShouldFailover>(
        'cf_should_failover');
    return fn(curlCode, httpCode);
  }

  Pointer<Void> wsConnect(Pointer<Void> ctx, String url) {
    final fn =
        _lib.lookupFunction<_CfWsConnectNative, _CfWsConnect>('cf_ws_connect');
    final urlPtr = url.toNativeUtf8();
    try {
      return fn(ctx, urlPtr);
    } finally {
      malloc.free(urlPtr);
    }
  }

  void wsClose(Pointer<Void> ws) {
    final fn =
        _lib.lookupFunction<_CfWsCloseNative, _CfWsClose>('cf_ws_close');
    fn(ws);
  }
}
