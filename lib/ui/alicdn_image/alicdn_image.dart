// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

part of dart.ui;

Future<Codec> AliCDNInstantiateImageCodec(
  String url,
{
  int targetWidth,
  int targetHeight,
  String tag,
  bool sharp,
  int quality,
  int webp,
  int heic,
  int multiframe,
  Map<String, String> extraInfo
}) {
  return _futurize(
    (_Callback<Codec> callback) => _AliCDNInstantiateImageCodec(
      url,
      callback,
      targetWidth ?? _kDoNotResizeDimension,
      targetHeight ?? _kDoNotResizeDimension,
      tag ?? "",
      sharp,
      quality,
      webp,
      heic,
      multiframe,
      extraInfo)
  );
}

String _AliCDNInstantiateImageCodec(
  String url,
  _Callback<Codec> callback,
  int targetWidth,
  int targetHeight,
  String tag,
  bool sharp,
  int quality,
  int webp,
  int heic,
  int multiframe,
  Map<String, String> extraInfo
) native 'AliCDNInstantiateImageCodec';

@pragma('vm:entry-point')
class AliCDNImageFrameCodec extends Codec {
  AliCDNImageFrameCodec(): super._();
  
  void cancel() native 'AliCDNImageFrameCodec_cancel';
}
