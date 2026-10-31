// Summaries of the trades in each column of the plot, drawn instead of their
// markers when they're too crowded: a bar from the lowest to the highest
// price traded in the column, its color blending from sell to buy by the
// buys' share of the amount, more opaque the more was traded.
// One instance per column; layouts must match GpuColumn and Uniforms in ColumnsPass.cpp.

struct View {
    across: f32,  // columns splitting the plot, some without trades
    priceLow: f32,
    priceSpan: f32,
    plotWidthPx: f32,
    plotHeightPx: f32,
    pixelRatio: f32,
    minOpacity: f32,  // of a column with almost nothing traded
    minHeightCssPx: f32,  // of a bar, so one of a single price still shows
    highlight: u32,  // the column to draw in the cursor color; none if past the last
    buyColor: vec3f,
    sellColor: vec3f,
    cursorColor: vec3f,
};
@group(0) @binding(0) var<uniform> view: View;

struct Column {
    @location(0) index: u32,  // of the columns across, from the plot's left
    @location(1) price: vec2f,  // lowest, highest
    @location(2) shares: vec2f,  // buys' share of the amount, and the amount relative to the largest column's
};

struct Bar {
    @builtin(position) position: vec4f,
    @location(0) @interpolate(flat) color: vec4f,  // premultiplied
};

@vertex
fn vertexMain(column: Column, @builtin(vertex_index) corner: u32, @builtin(instance_index) instance: u32) -> Bar {
    var corners = array(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0),
                        vec2f(0.0, 1.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0));
    let unit = corners[corner];
    // Pixels from the plot's bottom left corner.
    let xPx = (f32(column.index) + unit.x) * view.plotWidthPx / view.across;
    var lowPx = (column.price.x - view.priceLow) / view.priceSpan * view.plotHeightPx;
    var highPx = (column.price.y - view.priceLow) / view.priceSpan * view.plotHeightPx;
    let growPx = max(0.0, view.minHeightCssPx * view.pixelRatio - (highPx - lowPx)) / 2.0;
    lowPx -= growPx;
    highPx += growPx;
    let yPx = mix(lowPx, highPx, unit.y);

    var color = mix(view.sellColor, view.buyColor, column.shares.x);
    var alpha = mix(view.minOpacity, 1.0, column.shares.y);
    if (instance == view.highlight) {
        color = view.cursorColor;
        alpha = 1.0;
    }
    var bar: Bar;
    bar.position = vec4f(vec2f(xPx, yPx) / vec2f(view.plotWidthPx, view.plotHeightPx) * 2.0 - 1.0, 0.0, 1.0);
    bar.color = vec4f(color * alpha, alpha);
    return bar;
}

@fragment
fn fragmentMain(bar: Bar) -> @location(0) vec4f {
    return bar.color;
}
