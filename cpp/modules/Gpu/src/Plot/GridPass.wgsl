// Grid lines, one pixel wide, on whole pixels so they stay sharp.
// One instance per line; the layout must match GpuLine in GridPass.cpp.

struct Plot {
    sizePx: vec2f,
    lineColor: vec3f,
};
@group(0) @binding(0) var<uniform> plot: Plot;

struct Line {
    @location(0) position: f32,  // fraction of the plot: from the left if vertical, from the bottom if not
    @location(1) vertical: u32,
};

@vertex
fn vertexMain(line: Line, @builtin(vertex_index) corner: u32) -> @builtin(position) vec4f {
    var corners = array(vec2f(0.0, 0.0), vec2f(1.0, 0.0), vec2f(0.0, 1.0),
                        vec2f(0.0, 1.0), vec2f(1.0, 0.0), vec2f(1.0, 1.0));
    let unit = corners[corner];
    // Pixels from the plot's bottom left corner.
    var positionPx: vec2f;
    if (line.vertical == 1u) {
        positionPx = vec2f(floor(line.position * plot.sizePx.x) + unit.x, unit.y * plot.sizePx.y);
    } else {
        positionPx = vec2f(unit.x * plot.sizePx.x, floor(line.position * plot.sizePx.y) + unit.y);
    }
    return vec4f(positionPx / plot.sizePx * 2.0 - 1.0, 0.0, 1.0);
}

@fragment
fn fragmentMain() -> @location(0) vec4f {
    return vec4f(plot.lineColor, 1.0);
}
