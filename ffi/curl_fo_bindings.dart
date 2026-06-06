// curl_fo Dart FFI bindings for Flutter.
//
// Usage:
//   final lib = CurlFoLibrary.open();
//   final ctx = lib.ctxCreate();
//   final code = lib.easyPerform(ctx, curlHandle);
//
// ignore_for_file: non_constant_identifier_names

import 'dart:ffi';
import 'dart:io';

final class CurlFoBindings extends Struct {
  external Pointer<Void> cfg;
}

typedef _CfCtxCreateNative = Pointer<Void> Function();
typedef _CfCtxCreate = Pointer<Void> Function();

typedef _CfCtxDestroyNative = Void Function(Pointer<Void>);
typedef _CfCtxDestroy = void Function(Pointer<Void>);

typedef _CfEasyPerformNative = Int32 Function(Pointer<Void>, Pointer<Void>);
typedef _CfEasyPerform = int Function(Pointer<Void>, Pointer<Void>);

typedef _CfShouldFailoverNative = Int32 Function(Int32, Int32);
typedef _CfShouldFailover = int Function(int, int);

class CurlFoLibrary {
  CurlFoLibrary(this._lib);

  final DynamicLibrary _lib;

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

  Pointer<Void> ctxCreate() {
    final fn = _lib.lookupFunction<_CfCtxCreateNative, _CfCtxCreate>(
        'cf_ctx_create_default');
    return fn();
  }

  void ctxDestroy(Pointer<Void> ctx) {
    final fn = _lib
        .lookupFunction<_CfCtxDestroyNative, _CfCtxDestroy>('cf_ctx_destroy');
    fn(ctx);
  }

  int easyPerform(Pointer<Void> ctx, Pointer<Void> curl) {
    final fn = _lib.lookupFunction<_CfEasyPerformNative, _CfEasyPerform>(
        'cf_easy_perform');
    return fn(ctx, curl);
  }

  int shouldFailover(int curlCode, int httpCode) {
    final fn = _lib.lookupFunction<_CfShouldFailoverNative, _CfShouldFailover>(
        'cf_should_failover');
    return fn(curlCode, httpCode);
  }
}
