import 'dart:io';

import 'package:code_assets/code_assets.dart';
import 'package:crypto/crypto.dart';
import 'package:hooks/hooks.dart';

// This package links the `pico-view` engine as a PREBUILT library. The engine
// source lives in this same repo under `crates/`, but the built libraries are
// not committed.
//
// So the hook downloads them. For the build target it resolves the release
// asset, fetches it from this repo's release page, verifies it against the
// SHA-256 pinned in `native/engine.lock`, and caches it.
//
// To link an engine built from `crates/` instead, run `./build.sh` at the repo
// root — it writes the `native/engine.local` override this hook reads first.

/// The asset id the `@ffi.Native` lookups in the generated bindings resolve
/// against. Must match `lib/src/pico_view_bindings_generated.dart`.
const _assetName = 'src/pico_view_bindings_generated.dart';

/// Pins the release tag and the per-target digests.
const _lockPath = 'native/engine.lock';

/// Escape hatch for engine development.
const _localPath = 'native/engine.local';

void main(List<String> args) async {
  await build(args, (input, output) async {
    if (!input.config.buildCodeAssets) return;

    final code = input.config.code;
    final lockUri = input.packageRoot.resolve(_lockPath);
    final localUri = input.packageRoot.resolve(_localPath);

    // A directory dependency hashes its child names, so creating or deleting
    // `engine.local` re-runs this hook.
    output.dependencies.add(input.packageRoot.resolve('native/'));

    final Uri libUri;
    final override = _localOverride(localUri);
    if (override != null) {
      final file = File.fromUri(override.lib);
      if (!file.existsSync()) {
        throw ArgumentError(
          '${override.origin} points at a file that does not exist: '
              '${file.path}',
        );
      }
      libUri = file.absolute.uri;
      // Relink when the local engine is rebuilt, or when the file naming it is
      // edited to name a different one.
      output.dependencies.add(libUri);
      output.dependencies.add(override.sourceFile);
    } else {
      final lock = _EngineLock.parse(File.fromUri(lockUri), lockUri);
      final asset = _assetFor(code.targetOS, code.targetArchitecture);
      libUri = await _resolve(
        lock: lock,
        asset: asset,
        // The hook invoker serializes concurrent invocations on this directory,
        // and nothing else writes to it, so it is safe as a download cache. It
        // survives across builds, unlike `input.outputDirectory`.
        cacheDir: input.outputDirectoryShared,
      );
      // A new tag or digest must force a re-resolve.
      output.dependencies.add(lockUri);
    }

    output.assets.code.add(
      CodeAsset(
        package: input.packageName,
        name: _assetName,
        linkMode: DynamicLoadingBundled(),
        file: libUri,
      ),
    );
  });
}

/// A locally built engine to link in place of the pinned release.
class _Override {
  const _Override({
    required this.lib,
    required this.origin,
    required this.sourceFile,
  });

  /// Resolved location of the cdylib.
  final Uri lib;

  /// Where the path came from, for error messages.
  final String origin;

  /// The file naming [lib], declared as a dependency so editing it relinks.
  final Uri sourceFile;
}

/// Reads the gitignored [_localPath], if present. The first non-empty,
/// non-comment line is a path to a cdylib, resolved against that file's own
/// directory so a relative path works from any cwd.
_Override? _localOverride(Uri localUri) {
  final file = File.fromUri(localUri);
  if (!file.existsSync()) return null;

  for (final raw in file.readAsLinesSync()) {
    final line = raw.trim();
    if (line.isEmpty || line.startsWith('#')) continue;
    return _Override(
      // `Uri.file` escapes spaces and, on Windows, flips separators. A relative
      // path resolves against `localUri`; an absolute one replaces it outright.
      lib: localUri.resolveUri(Uri.file(line, windows: Platform.isWindows)),
      origin: _localPath,
      sourceFile: localUri,
    );
  }
  throw StateError(
    '$_localPath holds no path. Write the path to a locally built cdylib '
        'into it, or delete it to use the release pinned in $_lockPath.',
  );
}

/// Returns the cached library for [asset], downloading it from the release page
/// if it is absent or fails its digest check.
Future<Uri> _resolve({
  required _EngineLock lock,
  required String asset,
  required Uri cacheDir,
}) async {
  final expected = lock.digests[asset];
  if (expected == null) {
    throw StateError(
      'No SHA-256 pinned for "$asset" in $_lockPath.\n'
          'Publish a release (push an `engine-v*` tag, which runs '
          '.github/workflows/release.yml), then copy its SHA256SUMS asset into '
          'that file.\n'
          'For local engine development, run ./build.sh to build from `crates/` '
          'and write $_localPath.',
    );
  }

  // Key the cache on the tag so a bumped release does not collide with a stale
  // download of the same asset name.
  final cached = File.fromUri(cacheDir.resolve('${lock.tag}/$asset'));
  if (cached.existsSync() && _sha256(cached) == expected) {
    return cached.absolute.uri;
  }

  final url = Uri.parse(
    'https://github.com/${lock.repo}/releases/download/${lock.tag}/$asset',
  );
  await cached.parent.create(recursive: true);

  // Download to a sibling temp file and rename, so an interrupted build can
  // never leave a truncated library in the cache.
  final temp = File('${cached.path}.$pid.tmp');
  try {
    await _download(url, temp);
    final actual = _sha256(temp);
    if (actual != expected) {
      throw StateError(
        'Digest mismatch for $url\n'
            '  expected $expected (from $_lockPath)\n'
            '  actual   $actual\n'
            'The release asset was replaced, or $_lockPath is stale.',
      );
    }
    await temp.rename(cached.path);
  } finally {
    if (temp.existsSync()) await temp.delete();
  }
  return cached.absolute.uri;
}

Future<void> _download(Uri url, File dest) async {
  final client = HttpClient();
  try {
    client.findProxy = (uri) =>
        HttpClient.findProxyFromEnvironment(
          uri,
          environment: Platform.environment,
        );
    final response = await (await client.getUrl(url)).close();
    if (response.statusCode != HttpStatus.ok) {
      throw HttpException(
        'GET $url failed with HTTP ${response.statusCode}. '
            'Is the release published and the asset name correct?',
        uri: url,
      );
    }
    await response.pipe(dest.openWrite());
  } finally {
    client.close();
  }
}

String _sha256(File file) => sha256.convert(file.readAsBytesSync()).toString();

/// The release asset name for [os]/[arch], matching the matrix in
/// `.github/workflows/release.yml`.
///
/// Prebuilt binaries exist only for Linux x64/arm64, macOS x64/arm64 and
/// Windows x64; anything else throws.
String _assetFor(OS os, Architecture arch) {
  if (os == OS.linux &&
      (arch == Architecture.x64 || arch == Architecture.arm64)) {
    return 'libpico_view-${_triple(os, arch)}.so';
  }
  if (os == OS.macOS &&
      (arch == Architecture.x64 || arch == Architecture.arm64)) {
    return 'libpico_view-${_triple(os, arch)}.dylib';
  }
  if (os == OS.windows && arch == Architecture.x64) {
    return 'pico_view-${_triple(os, arch)}.dll';
  }
  throw UnsupportedError('pico_view has no prebuilt engine for $os/$arch');
}

String _triple(OS os, Architecture arch) {
  final cpu = arch == Architecture.x64 ? 'x86_64' : 'aarch64';
  if (os == OS.linux) return '$cpu-unknown-linux-gnu';
  if (os == OS.macOS) return '$cpu-apple-darwin';
  return '$cpu-pc-windows-msvc';
}

/// The parsed `native/engine.lock`: a release coordinate plus the SHA-256 of
/// each asset, in `sha256sum` output format so a published `SHA256SUMS` can be
/// pasted in verbatim.
class _EngineLock {
  _EngineLock({required this.repo, required this.tag, required this.digests});

  /// `owner/name` of the repo hosting the release assets.
  final String repo;

  /// Release tag, e.g. `v0.3.0`.
  final String tag;

  /// Asset file name -> lowercase hex SHA-256.
  final Map<String, String> digests;

  static _EngineLock parse(File file, Uri uri) {
    if (!file.existsSync()) {
      throw StateError('Missing $_lockPath at $uri');
    }
    String? repo;
    String? tag;
    final digests = <String, String>{};

    for (final raw in file.readAsLinesSync()) {
      final line = raw.trim();
      if (line.isEmpty || line.startsWith('#')) continue;

      // `key = value` settings, vs `<digest>  <name>` checksum lines. A digest
      // is hex, so an `=` can only belong to a setting.
      final eq = line.indexOf('=');
      if (eq != -1) {
        final key = line.substring(0, eq).trim();
        final value = line.substring(eq + 1).trim();
        if (key == 'repo') repo = value;
        if (key == 'tag') tag = value;
        continue;
      }

      final parts = line.split(RegExp(r'\s+'));
      if (parts.length != 2) {
        throw FormatException('Cannot parse $_lockPath line: $raw');
      }
      // sha256sum prefixes binary-mode names with `*`.
      digests[parts[1].replaceFirst(RegExp(r'^\*'), '')] = parts[0];
    }

    if (repo == null || tag == null) {
      throw StateError('$_lockPath must set both `repo` and `tag`.');
    }
    return _EngineLock(repo: repo, tag: tag, digests: digests);
  }
}
