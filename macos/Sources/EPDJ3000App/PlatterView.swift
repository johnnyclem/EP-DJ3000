// The platter, drawn. Angle 0 is 12 o'clock and positive is clockwise as seen from above, the same as the jog stream.

import SwiftUI

@MainActor
struct PlatterView: View {
    /// Radians, clockwise. Unbounded is fine.
    var angle: Double
    var touched: Bool
    /// Wedge brightness per touch sector, 0...1. Empty draws no ring.
    var sectorLevels: [Double] = []

    var body: some View {
        Canvas { context, size in
            let center = CGPoint(x: size.width / 2, y: size.height / 2)
            let radius = min(size.width, size.height) / 2 - 4
            guard radius > 10 else { return }

            // Record body and rim.
            context.fill(circle(center, radius), with: .color(Color(white: 0.10)))
            context.stroke(circle(center, radius), with: .color(touched ? .orange : Color.gray.opacity(0.5)),
                           lineWidth: touched ? 4 : 2)

            // Touch sectors sit on the outer band.
            if !sectorLevels.isEmpty {
                let n = Double(sectorLevels.count)
                for (i, level) in sectorLevels.enumerated() {
                    let a0 = Double(i) / n * 2 * Double.pi + 0.02
                    let a1 = Double(i + 1) / n * 2 * Double.pi - 0.02
                    let wedgePath = wedge(center: center, inner: radius * 0.80, outer: radius * 0.97, from: a0, to: a1)
                    context.fill(wedgePath, with: .color(Color.gray.opacity(0.18)))
                    context.fill(wedgePath, with: .color(Color.orange.opacity(max(0, min(1, level)))))
                }
            }

            // Grooves.
            for i in 0..<5 {
                let r = radius * (0.38 + 0.085 * CGFloat(i))
                context.stroke(circle(center, r), with: .color(Color.white.opacity(0.05)), lineWidth: 1)
            }

            // Everything that turns with the platter.
            var turning = context
            turning.translateBy(x: center.x, y: center.y)
            turning.rotate(by: Angle.radians(angle))
            var marker = Path()
            marker.move(to: CGPoint(x: 0, y: -radius * 0.55))
            marker.addLine(to: CGPoint(x: 0, y: -radius * 0.78))
            turning.stroke(marker, with: .color(.orange), style: StrokeStyle(lineWidth: 6, lineCap: .round))
            let label = circle(.zero, radius * 0.28)
            turning.fill(label, with: .color(Color.orange.opacity(0.9)))
            var dot = Path()
            dot.addEllipse(in: CGRect(x: -3, y: -radius * 0.23, width: 6, height: 6))
            turning.fill(dot, with: .color(.black))
            var spindle = Path()
            spindle.addEllipse(in: CGRect(x: -4, y: -4, width: 8, height: 8))
            turning.fill(spindle, with: .color(.black))
        }
        .aspectRatio(1, contentMode: .fit)
        .accessibilityLabel("Platter")
    }

    private func circle(_ center: CGPoint, _ radius: CGFloat) -> Path {
        Path(ellipseIn: CGRect(x: center.x - radius, y: center.y - radius, width: radius * 2, height: radius * 2))
    }

    /// A ring segment as a polygon: arc-direction conventions of `addArc` are easy to get backwards in a flipped
    /// coordinate system, explicit points are not.
    private func wedge(center: CGPoint, inner: CGFloat, outer: CGFloat, from a0: Double, to a1: Double) -> Path {
        func point(_ radius: CGFloat, _ a: Double) -> CGPoint {
            CGPoint(x: center.x + radius * CGFloat(sin(a)), y: center.y - radius * CGFloat(cos(a)))
        }
        let steps = 10
        var path = Path()
        for i in 0...steps {
            let a = a0 + (a1 - a0) * Double(i) / Double(steps)
            if i == 0 { path.move(to: point(outer, a)) } else { path.addLine(to: point(outer, a)) }
        }
        for i in stride(from: steps, through: 0, by: -1) {
            path.addLine(to: point(inner, a0 + (a1 - a0) * Double(i) / Double(steps)))
        }
        path.closeSubpath()
        return path
    }
}
