import 'package:flutter_test/flutter_test.dart';
import 'package:pico_view/pico_view.dart';

void main() {
  group('PicoViewConfig', () {
    test('resolves geometry for a known model', () {
      const config = PicoViewConfig();
      expect(config.model, kPicoViewDefaultModel);
      expect(config.width, 360);
      expect(config.height, 360);
    });

    test('reports zero geometry for an unknown model', () {
      const config = PicoViewConfig(model: 'no-such-panel');
      expect(config.width, 0);
      expect(config.height, 0);
    });

    test('every registered model has a positive size', () {
      expect(kPicoViewModels, isNotEmpty);
      for (final entry in kPicoViewModels.entries) {
        expect(entry.value.width, greaterThan(0), reason: entry.key);
        expect(entry.value.height, greaterThan(0), reason: entry.key);
      }
      expect(kPicoViewModels, contains(kPicoViewDefaultModel));
    });
  });

  group('PicoOtaEvent', () {
    test('done and failed are terminal, progress states are not', () {
      expect(const PicoOtaEvent('done', 100, 0).isTerminal, isTrue);
      expect(const PicoOtaEvent('failed', 42, 7).isTerminal, isTrue);
      expect(const PicoOtaEvent('receiving', 42, 0).isTerminal, isFalse);
      expect(const PicoOtaEvent('verifying', 100, 0).isTerminal, isFalse);
      expect(const PicoOtaEvent('unknown', 0, 0).isTerminal, isFalse);
    });
  });

  group('exceptions', () {
    test('PicoViewException keeps a null code when none was given', () {
      expect(PicoViewException('boom').code, isNull);
    });
  });
}
