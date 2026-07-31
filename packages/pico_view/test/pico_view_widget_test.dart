// Exercises the PicoView widget with no device attached. The controller is
// never opened, so `flushRgba` returns false before reaching FFI and no native
// library is loaded.

import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:pico_view/pico_view.dart';

void main() {
  late PicoViewController controller;

  setUp(() => controller = PicoViewController());
  tearDown(() => controller.dispose());

  testWidgets('lays the child out at the panel resolution', (tester) async {
    await tester.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: Center(
          child: PicoView(
            controller: controller,
            child: const SizedBox.expand(child: ColoredBox(color: Colors.red)),
          ),
        ),
      ),
    );

    // Not opened, so the default config's geometry is used.
    expect(tester.getSize(find.byType(PicoView)), const Size(360, 360));
    expect(tester.takeException(), isNull);
  });

  testWidgets('renders on-screen with no device open', (tester) async {
    await tester.pumpWidget(
      MaterialApp(
        home: PicoView(controller: controller, child: const Text('hello')),
      ),
    );
    await tester.pump(const Duration(milliseconds: 100));

    expect(find.text('hello'), findsOneWidget);
    expect(controller.isOpen, isFalse);
    expect(tester.takeException(), isNull);
  });

  testWidgets('an animating child keeps pumping frames without a device', (
    tester,
  ) async {
    await tester.pumpWidget(
      MaterialApp(
        home: PicoView(
          controller: controller,
          maxFps: 60,
          child: const CircularProgressIndicator(),
        ),
      ),
    );
    // Several capture windows worth of frames: the loop must survive every
    // flushRgba being rejected.
    for (var i = 0; i < 10; i++) {
      await tester.pump(const Duration(milliseconds: 20));
    }
    expect(tester.takeException(), isNull);
  });

  testWidgets('drops the touch subscription when enableTouch flips', (
    tester,
  ) async {
    Widget build({required bool enableTouch}) => MaterialApp(
      home: PicoView(
        controller: controller,
        enableTouch: enableTouch,
        child: const Text('x'),
      ),
    );

    await tester.pumpWidget(build(enableTouch: true));
    await tester.pumpWidget(build(enableTouch: false));
    await tester.pumpWidget(build(enableTouch: true));
    expect(tester.takeException(), isNull);
  });

  testWidgets('device calls are no-ops while closed', (tester) async {
    // They short-circuit before touching FFI, so these complete without a
    // native library ever being loaded.
    expect(await controller.setBrightness(200), isFalse);
    expect(await controller.playHaptic(47), isFalse);
    expect(await controller.stopHaptic(), isFalse);
    expect(controller.linkState, PicoLinkState.disconnected);
    expect(controller.firmwareVersion, isNull);
  });

  testWidgets('getDeviceInfo while closed reports it rather than hanging', (
    tester,
  ) async {
    // No device, so this must reject up front — never wait on a response that
    // the engine was never asked for.
    await expectLater(
      controller.getDeviceInfo(),
      throwsA(isA<PicoViewException>()),
    );
  });
}
