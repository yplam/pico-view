// Mirrors a clock to a pico-view panel.
//
// The same subtree renders on-screen and on the physical panel, and tapping the
// panel switches the face — the touch arrives as a real pointer event in this
// widget tree. With no device attached everything still runs on-screen; frames
// are simply dropped.

import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:pico_view/pico_view.dart';

void main() => runApp(const ExampleApp());

class ExampleApp extends StatelessWidget {
  const ExampleApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'pico_view example',
      theme: ThemeData.dark(),
      home: const ClockPage(),
    );
  }
}

class ClockPage extends StatefulWidget {
  const ClockPage({super.key});

  @override
  State<ClockPage> createState() => _ClockPageState();
}

class _ClockPageState extends State<ClockPage> {
  final PicoViewController _controller = PicoViewController();

  PicoLinkState _link = PicoLinkState.disconnected;
  StreamSubscription<PicoLinkState>? _linkSub;

  String? _openError;

  @override
  void initState() {
    super.initState();
    _controller.init();
    _linkSub = _controller.linkStates.listen((state) {
      if (mounted) setState(() => _link = state);
    });
    unawaited(_open());
  }

  Future<void> _open() async {
    try {
      await _controller.open(const PicoViewConfig());
    } on PicoViewException catch (e) {
      if (mounted) setState(() => _openError = e.message);
    }
  }

  @override
  void dispose() {
    _linkSub?.cancel();
    _controller.disposeSync();
    super.dispose();
  }

  String get _status => switch (_link) {
    PicoLinkState.connected =>
      'connected'
          '${_controller.firmwareVersion == null ? '' : ' · fw ${_controller.firmwareVersion}'}',
    PicoLinkState.disconnected =>
      _openError ?? 'no panel attached — mirroring to screen only',
  };

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: Center(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            // The child is laid out at the panel's resolution, so what you see
            // here is pixel-for-pixel what the panel shows.
            PicoView(controller: _controller, child: const ClockFace()),
            const SizedBox(height: 24),
            Text(_status, style: Theme.of(context).textTheme.bodySmall),
          ],
        ),
      ),
    );
  }
}

/// A round clock face sized for the 360x360 panel. Tap anywhere — on-screen or
/// on the panel itself — to switch between the analog and digital face.
class ClockFace extends StatefulWidget {
  const ClockFace({super.key});

  @override
  State<ClockFace> createState() => _ClockFaceState();
}

class _ClockFaceState extends State<ClockFace> {
  late final Timer _timer;
  DateTime _now = DateTime.now();
  bool _digital = false;

  @override
  void initState() {
    super.initState();
    _timer = Timer.periodic(const Duration(milliseconds: 100), (_) {
      if (mounted) setState(() => _now = DateTime.now());
    });
  }

  @override
  void dispose() {
    _timer.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: () => setState(() => _digital = !_digital),
      child: Container(
        decoration: const BoxDecoration(
          shape: BoxShape.circle,
          color: Color(0xFF101418),
        ),
        child: Center(
          child: _digital
              ? Text(
                  '${_two(_now.hour)}:${_two(_now.minute)}:${_two(_now.second)}',
                  style: const TextStyle(
                    fontSize: 56,
                    fontWeight: FontWeight.w600,
                    fontFeatures: [FontFeature.tabularFigures()],
                    color: Color(0xFFE6EDF3),
                  ),
                )
              : CustomPaint(size: const Size.square(360), painter: _Dial(_now)),
        ),
      ),
    );
  }

  static String _two(int v) => v.toString().padLeft(2, '0');
}

class _Dial extends CustomPainter {
  _Dial(this.now);

  final DateTime now;

  @override
  void paint(Canvas canvas, Size size) {
    final center = size.center(Offset.zero);
    final radius = size.shortestSide / 2;

    final tick = Paint()
      ..color = const Color(0xFF3D444D)
      ..strokeWidth = 2;
    for (var i = 0; i < 60; i++) {
      final angle = i * math.pi / 30;
      final outer = radius - 14;
      final inner = outer - (i % 5 == 0 ? 14.0 : 6.0);
      final dir = Offset(math.sin(angle), -math.cos(angle));
      canvas.drawLine(center + dir * inner, center + dir * outer, tick);
    }

    void hand(double turns, double length, double width, Color color) {
      final angle = turns * 2 * math.pi;
      final dir = Offset(math.sin(angle), -math.cos(angle));
      canvas.drawLine(
        center - dir * 20,
        center + dir * length,
        Paint()
          ..color = color
          ..strokeWidth = width
          ..strokeCap = StrokeCap.round,
      );
    }

    final seconds = now.second + now.millisecond / 1000;
    final minutes = now.minute + seconds / 60;
    final hours = now.hour % 12 + minutes / 60;

    hand(hours / 12, radius * 0.5, 9, const Color(0xFFE6EDF3));
    hand(minutes / 60, radius * 0.72, 6, const Color(0xFFE6EDF3));
    hand(seconds / 60, radius * 0.78, 2, const Color(0xFFF85149));

    canvas.drawCircle(center, 6, Paint()..color = const Color(0xFFF85149));
  }

  @override
  bool shouldRepaint(_Dial old) => old.now != now;
}
