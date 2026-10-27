import type { GpuInfo as WasmGpuInfo } from '../wasm/Viewer.js';

/**
 * The GPU details the viewer reports. The generated type allows any embind
 * string input for each field; values coming out of C++ are always strings.
 */
export type GpuInfo = { [Field in keyof WasmGpuInfo]: string };

/**
 * A one-line label for the GPU, e.g. "nvidia ampere · WebGPU · DiscreteGPU".
 * Browsers usually hide `device` but expose `vendor` and `architecture`; when
 * they hide those too, WebGL's renderer string often still names the GPU.
 */
export function describeGpu(info: GpuInfo): string {
  let name = info.device;
  if (!name) {
    name = [info.vendor, info.architecture].filter(Boolean).join(' ');
  }
  if (!name && info.webglRenderer) {
    name = `${info.webglRenderer} (via WebGL)`;
  }
  if (!name) {
    name = 'Unknown GPU (details hidden)';
  }

  const parts = [name, info.backend];
  if (info.adapter !== 'Unknown') {
    parts.push(info.adapter);
  }
  return parts.join(' · ');
}
