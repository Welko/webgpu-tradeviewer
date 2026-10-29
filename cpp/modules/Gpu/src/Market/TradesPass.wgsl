// Trades as discs: time across, price up, colored by side, sized by amount.
// One instance per trade; layouts must match GpuTrade and Uniforms in TradesPass.cpp.

struct View {
    startHigh: u32,
    startLow: u32,
    lengthUs: f32,
    markerScale: u32,  // 0 area, 1 log
    priceLow: f32,
    priceSpan: f32,
    width: f32,  // pixels
    height: f32,
    pixelRatio: f32,
};
@group(0) @binding(0) var<uniform> view: View;

struct Trade {
    @location(0) time: vec2u,  // microseconds since the epoch: upper and lower 32 bits
    @location(1) price: f32,
    @location(2) amount: f32,
    @location(3) side: u32,
};

struct Marker {
    @builtin(position) position: vec4f,
    @location(0) pixel: vec2f,  // from the centre, in pixels
    @location(1) radius: f32,
    @location(2) color: vec3f,
};

const BuyColor = vec3f(0.20, 0.80, 0.45);
const SellColor = vec3f(0.95, 0.30, 0.30);
const Opacity = 0.6;

// Marker radii in CSS or window pixels.
const MinRadius = 1.5;
const MaxRadius = 16.0;
const AreaRadiusAt1Btc = 6.0;
// The log scale spans these amounts, in BTC, from MinRadius to MaxRadius.
const LogLowest = 1e-4;
const LogHighest = 100.0;

/** Microseconds from the view's start, subtracted as 64-bit integers so they stay exact far from 1970. */
fn sinceStartUs(time: vec2u) -> f32 {
    let borrow = select(0u, 1u, time.y < view.startLow);
    let low = time.y - view.startLow;
    let high = bitcast<i32>(time.x - view.startHigh - borrow);
    return f32(high) * 4294967296.0 + f32(low);
}

fn markerRadius(amount: f32) -> f32 {
    var radius: f32;
    if (view.markerScale == 0u) {
        radius = AreaRadiusAt1Btc * sqrt(amount);
    } else {
        radius = mix(MinRadius, MaxRadius, log2(amount / LogLowest) / log2(LogHighest / LogLowest));
    }
    return clamp(radius, MinRadius, MaxRadius) * view.pixelRatio;
}

@vertex
fn vertexMain(trade: Trade, @builtin(vertex_index) corner: u32) -> Marker {
    var corners = array(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0),
                        vec2f(-1.0, 1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0));
    let centre = vec2f(sinceStartUs(trade.time) / view.lengthUs, (trade.price - view.priceLow) / view.priceSpan) * 2.0 - 1.0;
    let radius = markerRadius(trade.amount);
    // One pixel more than the radius, for the smoothed edge.
    let pixel = corners[corner] * (radius + 1.0);

    var marker: Marker;
    marker.position = vec4f(centre + pixel * 2.0 / vec2f(view.width, view.height), 0.0, 1.0);
    marker.pixel = pixel;
    marker.radius = radius;
    marker.color = select(SellColor, BuyColor, trade.side == 0u);
    return marker;
}

@fragment
fn fragmentMain(marker: Marker) -> @location(0) vec4f {
    // How much of this pixel the disc covers.
    let coverage = clamp(marker.radius + 0.5 - length(marker.pixel), 0.0, 1.0);
    let alpha = coverage * Opacity;
    return vec4f(marker.color * alpha, alpha);  // premultiplied
}
