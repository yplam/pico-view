// Drives a real request through the native engine and back.
//
// No device is involved: an unknown panel model is rejected by the engine
// before it touches USB. That is enough to exercise the whole asynchronous
// path — encode, `pv_request`, the engine's own thread, the `PvEvent` posted to
// the SendPort, and the id that matches the answer to the `Completer` waiting
// on it. If correlation broke, these would hang rather than fail.

import 'package:flutter_test/flutter_test.dart';
import 'package:pico_view/pico_view.dart';

void main() {
  test('a rejected open answers on the SendPort instead of blocking', () async {
    final controller = PicoViewController()..init();
    addTearDown(controller.disposeSync);

    await expectLater(
      controller.open(const PicoViewConfig(model: 'no-such-panel')),
      throwsA(
        isA<PicoViewException>().having(
          (e) => e.message,
          'message',
          contains('no-such-panel'),
        ),
      ),
    );
    expect(controller.isOpen, isFalse);
  });

  test('concurrent requests each get their own answer', () async {
    // The engine's worker is serial, so these queue behind one another. What
    // matters is that three outstanding ids resolve to three distinct answers
    // rather than one response completing the wrong future.
    final controller = PicoViewController()..init();
    addTearDown(controller.disposeSync);

    final results = await Future.wait([
      controller.open(const PicoViewConfig(model: 'bad-a')).then(
        (_) => 'opened',
        onError: (Object e) => (e as PicoViewException).message,
      ),
      controller.open(const PicoViewConfig(model: 'bad-b')).then(
        (_) => 'opened',
        onError: (Object e) => (e as PicoViewException).message,
      ),
      controller.open(const PicoViewConfig(model: 'bad-c')).then(
        (_) => 'opened',
        onError: (Object e) => (e as PicoViewException).message,
      ),
    ]);

    expect(results[0], contains('bad-a'));
    expect(results[1], contains('bad-b'));
    expect(results[2], contains('bad-c'));
  });

  test('a disposed controller fails what is still outstanding', () async {
    // Nothing will answer once the port is closed, so the pending requests
    // have to be failed rather than left to time out.
    final controller = PicoViewController()..init();
    final pending = controller.open(const PicoViewConfig(model: 'bad-d'));
    controller.disposeSync();

    await expectLater(pending, throwsA(isA<PicoViewException>()));
  });
}
