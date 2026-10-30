// Trades as discs: time across, price up, colored by side, sized by amount.
// One instance per trade; layouts must match GpuTrade and Uniforms in TradesPass.cpp.
// A second pipeline rings the highlighted trade, with the same placement.

// How markers look: colours from the theme, sizes from Gpu/Market/Marker.hpp.
struct Markers {
    buyColor: vec3f,
    opacity: f32,
    sellColor: vec3f,
    scale: u32,  // 0 area, 1 log
    // Radii in CSS or window pixels.
    minRadius: f32,
    maxRadius: f32,
    areaRadiusAt1Btc: f32,
    // The log scale grows from minRadius at logLowest BTC to maxRadius at logHighest BTC.
    logLowest: f32,
    logHighest: f32,
    cursorColor: vec3f,  // of the ring around the highlighted trade
};

struct View {
    startHigh: u32,
    startLow: u32,
    lengthUs: f32,
    priceLow: f32,
    priceSpan: f32,
    width: f32,  // pixels
    height: f32,
    pixelRatio: f32,
    markers: Markers,
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

/** Microseconds from the view's start, subtracted as 64-bit integers so they stay exact far from 1970. */
fn sinceStartUs(time: vec2u) -> f32 {
    let borrow = select(0u, 1u, time.y < view.startLow);
    let low = time.y - view.startLow;
    let high = bitcast<i32>(time.x - view.startHigh - borrow);
    return f32(high) * 4294967296.0 + f32(low);
}

/** In physical pixels. Marker::getRadius() computes the same in CSS or window pixels. */
fn markerRadius(amount: f32) -> f32 {
    let markers = view.markers;
    var radius: f32;
    if (markers.scale == 0u) {
        radius = markers.areaRadiusAt1Btc * sqrt(amount);
    } else {
        let along = log2(amount / markers.logLowest) / log2(markers.logHighest / markers.logLowest);
        radius = mix(markers.minRadius, markers.maxRadius, along);
    }
    return clamp(radius, markers.minRadius, markers.maxRadius) * view.pixelRatio;
}

/** A corner of the square around the trade's marker, `margin` pixels wider than it on each side. */
fn placeMarker(trade: Trade, corner: u32, margin: f32) -> Marker {
    var corners = array(vec2f(-1.0, -1.0), vec2f(1.0, -1.0), vec2f(-1.0, 1.0),
                        vec2f(-1.0, 1.0), vec2f(1.0, -1.0), vec2f(1.0, 1.0));
    let centre = vec2f(sinceStartUs(trade.time) / view.lengthUs, (trade.price - view.priceLow) / view.priceSpan) * 2.0 - 1.0;
    let radius = markerRadius(trade.amount);
    let pixel = corners[corner] * (radius + margin);

    var marker: Marker;
    marker.position = vec4f(centre + pixel * 2.0 / vec2f(view.width, view.height), 0.0, 1.0);
    marker.pixel = pixel;
    marker.radius = radius;
    marker.color = select(view.markers.sellColor, view.markers.buyColor, trade.side == 0u);
    return marker;
}

@vertex
fn vertexMain(trade: Trade, @builtin(vertex_index) corner: u32) -> Marker {
    // One pixel more than the radius, for the smoothed edge.
    return placeMarker(trade, corner, 1.0);
}

@fragment
fn fragmentMain(marker: Marker) -> @location(0) vec4f {
    // How much of this pixel the disc covers.
    let coverage = clamp(marker.radius + 0.5 - length(marker.pixel), 0.0, 1.0);
    let alpha = coverage * view.markers.opacity;
    return vec4f(marker.color * alpha, alpha);  // premultiplied
}

// The ring around the highlighted trade, in CSS or window pixels: a gap
// outside its marker, then the ring.
const RingGap = 1.5;
const RingWidth = 2.0;

@vertex
fn ringVertex(trade: Trade, @builtin(vertex_index) corner: u32) -> Marker {
    return placeMarker(trade, corner, (RingGap + RingWidth) * view.pixelRatio + 1.0);
}

@fragment
fn ringFragment(marker: Marker) -> @location(0) vec4f {
    let inner = marker.radius + RingGap * view.pixelRatio;
    let outer = inner + RingWidth * view.pixelRatio;
    let distance = length(marker.pixel);
    // How much of this pixel the ring covers, smoothed at both edges.
    let coverage = clamp(min(distance - inner, outer - distance) + 0.5, 0.0, 1.0);
    return vec4f(view.markers.cursorColor * coverage, coverage);  // premultiplied
}
