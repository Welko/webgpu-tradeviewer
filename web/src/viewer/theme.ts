import type { Color, Theme } from './viewerModule';

/** A computed CSS colour, which browsers give as rgb() or rgba() for hex and rgb() values. */
function parseColor(css: string, property: string): Color {
  const match = /^rgba?\(\s*([\d.]+)[\s,]+([\d.]+)[\s,]+([\d.]+)/.exec(css);
  if (!match) {
    throw new Error(`${property} isn't a colour the viewer can read: "${css}". Use hex or rgb().`);
  }
  return { red: Number(match[1]) / 255, green: Number(match[2]) / 255, blue: Number(match[3]) / 255 };
}

/**
 * The viewer's colours from the CSS custom properties that apply to `element`
 * (--plot-background, --plot-grid, --buy, --sell, --cursor, --marker-opacity),
 * so the stylesheet styles the plot along with the page. `element` must be able to
 * hold a child, so not the canvas itself.
 */
export function readTheme(element: Element): Theme {
  // Custom properties compute to whatever was written; a property that takes
  // colours turns them into rgb().
  const probe = document.createElement('span');
  probe.style.display = 'none';
  element.append(probe);
  const style = getComputedStyle(element);
  const color = (property: string) => {
    if (style.getPropertyValue(property).trim() === '') {
      throw new Error(`The viewer's colour ${property} isn't set in CSS`);
    }
    probe.style.color = `var(${property})`;
    return parseColor(getComputedStyle(probe).color, property);
  };
  try {
    const opacity = Number.parseFloat(style.getPropertyValue('--marker-opacity'));
    return {
      background: color('--plot-background'),
      grid: color('--plot-grid'),
      buy: color('--buy'),
      sell: color('--sell'),
      cursor: color('--cursor'),
      markerOpacity: Number.isFinite(opacity) ? opacity : 1,
    };
  } finally {
    probe.remove();
  }
}
