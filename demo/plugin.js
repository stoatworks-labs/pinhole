/**
 * Pinhole — browser demo.
 *
 * A pinhole camera and camera obscura looking at the clip, with a Fresnel
 * zone plate in place of the hole as an option (SW Pinhole, PC01, an effect).
 * The one idea, from AGENTS.md: a pinhole does not blur a sharp picture, it
 * makes the picture by Fresnel diffraction. Every point of the clip lights a
 * 24 mm screen with the Fresnel–Lommel pattern of the aperture, one pattern
 * per wavelength, and the picture is the sum: the clip convolved, per colour
 * band, with the band's mean pattern, darkened toward the corners by cos⁴.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **This page does not port the plugin. It runs it.** `pinhole-core.wasm` is
 * the plugin's own C++ compiled UNMODIFIED with emscripten
 * (demo/tools/build-wasm.sh): `source/Pinhole.cpp` — the plugin class, with
 * its constructor, the rebuild key, the RGBA16F pyramid and sum buffers, the
 * tap texture, every uniform it sets and the GL state it saves and puts back —
 * and everything it calls (Optics, Controls, PassBuffer, Shaders, Diag), with
 * the FFGL SDK's CFFGLPlugin, its parameter bookkeeping, FFGLShader and
 * FFGLFBO. The Fresnel patterns and the pyramid's taps are that C++, in
 * double, on this page's thread. Its GL calls go to this page's WebGL2
 * context through emscripten's GL library.
 *
 * What is the page's and not the plugin's:
 *
 *   - **The host.** demo/wasm/glue.cpp constructs the plugin (what
 *     EffectPlugin.cpp's registration does in a bundle), reads its parameter
 *     declarations back through the SDK's host getters — the panel below is
 *     built from them, not from a list typed here — and forwards
 *     SetFloatParameter and ProcessOpenGL with one input texture, as
 *     tools/phtest's Rig does.
 *   - **Four GL entry points** (demo/wasm/gl_shim.cpp). glShaderSource: the
 *     plugin hands GL desktop GLSL 4.10; `shaderSource()` below refuses any
 *     text that is not byte for byte one of this page's assemblies of its copy
 *     (shaders.js, held to source/Shaders.cpp by demo/tools/check_shaders.py),
 *     then applies the kit's `port()` — the version line and the ES precision
 *     defaults, nothing else. glEnable / glDisable / glIsEnabled:
 *     GL_PROGRAM_POINT_SIZE, which the plugin saves and restores every frame,
 *     does not exist in WebGL2.
 *   - **The panel's read-outs.** The number beside a slider is Controls.cpp's
 *     conversion, compiled in (glue.cpp's ph_convert pairs each control with
 *     the function CurrentSettings() applies to it); the units printed after
 *     it, the tooltips and the presets are this page's. The presets are
 *     written in millimetres, nanometres and stops and go to the host's 0..1
 *     through the plugin's own inverse functions (ph_param_from).
 *   - **The line under the picture**: the plugin's own state, through the
 *     accessors its harness reads. It reports; it measures nothing.
 *
 * **Open Zones is a number field.** It is FF_TYPE_INTEGER, holding the count;
 * the kit has no integer control, so (as on polyhedral's page) it is a text
 * row turned into a number field clamped to the declared range.
 *
 * **The About block is not on the panel**, as on every page in the suite.
 */

import { mountDemo } from './vendor/demo.js';
import { port, GLError } from './vendor/gl.js';
import * as S from './shaders.js';
import createPinhole from './pinhole-core.js';

//===========================================================================
// FFGL.h's parameter types, by number: what GetParamType returns.
//===========================================================================

const FF_TYPE_BOOLEAN = 0;
const FF_TYPE_EVENT = 1;
const FF_TYPE_STANDARD = 10;
const FF_TYPE_OPTION = 11;
const FF_TYPE_INTEGER = 13;
const FF_TYPE_TEXT = 100;

//===========================================================================
// The GLSL: the page's checked copy, assembled as the plugin assembles it
// (ASSEMBLY is read out of AssembleVertex() and Assemble() by
// check_shaders.py), one text per stage of each of the four passes.
//===========================================================================

const PAGE_SHADERS = Object.entries(S.ASSEMBLY).map(([name, pieces]) => ({
  name,
  stage: name.endsWith('_vertex') ? 'vertex' : 'fragment',
  text: pieces.map((p) => S[p]).join(''),
}));

/** Which stages the compiled plugin handed over, and whether each was ours. */
const shaderCheck = { matched: new Set(), refused: 0 };

/**
 * Called by gl_shim.cpp's glShaderSource with the text the compiled plugin
 * handed to GL. Anything but one of this page's own assemblies is refused (an
 * empty source, so the plugin's InitGL fails and says so) rather than compiled.
 */
function shaderSource(stage, text) {
  const ours = PAGE_SHADERS.find((s) => s.stage === stage && s.text === text);
  if (!ours) {
    shaderCheck.refused += 1;
    return '';
  }
  shaderCheck.matched.add(ours.name);
  return port(ours.text, { stage });
}

//===========================================================================
// The module.
//===========================================================================

const printed = [];
let core;
try {
  core = await createPinhole({
    pinholeShaderSource: shaderSource,
    print: (text) => { printed.push(text); console.log(text); },
    printErr: (text) => { printed.push(text); console.warn(text); },
  });
} catch (error) {
  const root = document.querySelector('#demo');
  root.textContent = `The demo could not start: this browser would not run its WebAssembly (${error.message}). The plugin, its source and its downloads are at https://github.com/stoatworks-labs/pinhole.`;
  throw error;
}

const text = (pointer) => core.UTF8ToString(pointer);

//===========================================================================
// The declarations, read from the plugin's constructor through the SDK.
//===========================================================================

function declarations() {
  const h = core._ph_new();
  const list = [];
  const n = core._ph_param_count(h);
  for (let i = 0; i < n; i += 1) {
    const type = core._ph_param_type(h, i);
    const d = { index: i, name: text(core._ph_param_name(h, i)), type, group: text(core._ph_param_group(h, i)) };
    d.default = type === FF_TYPE_TEXT ? null : core._ph_param_default(h, i);
    if (type === FF_TYPE_OPTION) {
      d.elements = [];
      d.values = [];
      for (let e = 0; e < core._ph_param_element_count(h, i); e += 1) {
        d.elements.push(text(core._ph_param_element_name(h, i, e)));
        d.values.push(core._ph_param_element_value(h, i, e));
      }
    }
    if (type === FF_TYPE_INTEGER) {
      d.min = core._ph_param_range_min(h, i);
      d.max = core._ph_param_range_max(h, i);
    }
    list.push(d);
  }
  const inputs = [core._ph_min_inputs(h), core._ph_max_inputs(h)];
  core._ph_delete(h);
  return { list, inputs };
}

const DECLARED = declarations();
if (DECLARED.inputs[0] !== 1 || DECLARED.inputs[1] !== 1) {
  throw new Error(`SW Pinhole declares ${DECLARED.inputs.join('–')} inputs; this page hands it exactly one`);
}

//===========================================================================
// The read-outs: Controls.cpp's number, this page's unit.
//===========================================================================

const sig = (n) => (Math.abs(n) >= 100 ? n.toFixed(0) : Math.abs(n) >= 10 ? n.toFixed(1) : Math.abs(n) >= 1 ? n.toFixed(2) : n.toFixed(3));
const UNITS = {
  Diameter: (m) => `${sig(m * 1e3)} mm`,
  'Zone Focus': (m) => `${(m * 1e9).toFixed(0)} nm`,
  Distance: (m) => `${sig(m * 1e3)} mm`,
  Exposure: (ev) => `${ev >= 0 ? '+' : '−'}${Math.abs(ev).toFixed(2)} EV`,
};

function readout(d) {
  return (v) => {
    const x = core._ph_convert(d.index, Math.fround(v));
    if (Number.isNaN(x)) return v.toFixed(2);
    return (UNITS[d.name] ?? ((y) => y.toFixed(3)))(x);
  };
}

/** Tooltips: this page's words, from the README's description of each control. */
const HINTS = {
  Aperture: 'Pinhole: a round hole in a thin plate. Zone Plate: a Fresnel zone plate of Open Zones open rings, made to focus Zone Focus on the screen. Diameter is the pinhole’s; Open Zones and Zone Focus are the plate’s, and each does nothing under the other.',
  Diameter: 'The hole, 0.1–2 mm, geometrically. The default, 0.306 mm, is the sharpest hole for a 50 mm box in green light (Mielenz’s optimum, 1.844 √(λf)). Smaller spreads light by diffraction (red further than blue), larger by the hole’s own shadow.',
  'Open Zones': 'The plate’s open rings, 1–16 (2n − 1 zones, the centre and the edge open). FF_TYPE_INTEGER in the plugin: a number field here, clamped to its declared range. Zone Plate only.',
  'Zone Focus': 'The wavelength the plate brings to focus on the screen, 400–700 nm: that colour comes sharp, the others land round it as soft coloured light. Zone Plate only.',
  Distance: 'The hole (or plate) to the screen, 10–500 mm, geometrically. The screen is 24 mm tall, so a short box is a wide view with dark corners (cos⁴) and a long one a narrow, evenly lit view.',
  Exposure: 'The picture’s exposure, −4 to +4 stops. The default, +0.2 EV, is what an averaging meter sets on Resolume’s demo clips (the blur keeps the light; only the dark corners lose it).',
  View: 'Upright, as a print would show it; Obscura, turned 180°, as on the wall of a camera obscura.',
  Edge: 'What lies past the frame’s edge: Mirror, the scene reflected (it carries on); Black, a picture on a black wall. Only a wide blur reaches far enough past the edge to show it.',
  Mix: 'The picture against the clip, in code values. At 0 the clip comes through untouched. The output’s alpha is mix(α, 1, Mix): the screen is lit everywhere.',
};

//===========================================================================
// Kit parameters from the declarations. The id is the name as Arena
// addresses it (lower case, spaces removed).
//===========================================================================

const KIT_TYPES = {
  [FF_TYPE_STANDARD]: 'standard',
  [FF_TYPE_OPTION]: 'option',
  [FF_TYPE_BOOLEAN]: 'boolean',
  [FF_TYPE_EVENT]: 'boolean',
  [FF_TYPE_INTEGER]: 'text',
};

/** The kit reads these from the query string itself; a parameter must not be one. */
const RESERVED_QUERY = new Set(['embed', 'size', 'clip', 'bg']);
const addressOf = (name) => {
  const address = name.toLowerCase().replace(/\s+/g, '');
  return RESERVED_QUERY.has(address) ? `param-${address}` : address;
};

const onPanel = (d) => d.group !== 'About';

function kitDefault(d) {
  switch (d.type) {
    case FF_TYPE_OPTION: {
      const i = d.values.findIndex((v) => v === d.default);
      return i >= 0 ? i : Math.round(d.default);
    }
    case FF_TYPE_INTEGER: return String(Math.round(d.default));
    case FF_TYPE_BOOLEAN:
    case FF_TYPE_EVENT: return d.default > 0.5 ? 1 : 0;
    default: return d.default;
  }
}

const PARAMS = DECLARED.list.filter(onPanel).map((d) => {
  const type = KIT_TYPES[d.type];
  if (type === undefined) throw new Error(`no kit control for FFGL type ${d.type} (${d.name})`);
  const p = { id: addressOf(d.name), name: d.name, type, group: d.group, hint: HINTS[d.name], ff: d, default: kitDefault(d) };
  if (d.type === FF_TYPE_OPTION) p.elements = d.elements;
  if (d.type === FF_TYPE_STANDARD) p.display = readout(d);
  return p;
});

/** The value the host would send for a kit control, or null for none. */
function hostValue(p, v) {
  const d = p.ff;
  switch (d.type) {
    case FF_TYPE_OPTION: return Math.fround(d.values[Math.round(v)] ?? 0);
    case FF_TYPE_BOOLEAN: return v > 0.5 ? 1 : 0;
    case FF_TYPE_EVENT: return null;
    case FF_TYPE_INTEGER: {
      const n = Number.parseInt(String(v).trim(), 10);
      if (!Number.isFinite(n)) return null;
      return Math.min(d.max, Math.max(d.min, n));
    }
    default: return Math.fround(v);
  }
}

//===========================================================================
// The presets: whole states on top of the constructor's defaults, by the
// plugin's own names. A number in { units } is in the plugin's units (metres,
// stops) and is converted by the plugin's own inverse (ph_param_from); an
// option is its element's name; Open Zones is its count; a bare number is the
// host's 0..1. They are the page's choices, mostly the README's and
// AGENTS.md's measured cases.
//===========================================================================

const u = (units) => ({ units });
const PRESET_UNITS = [
  ['The defaults: the optimum hole for a 50 mm box in green light (0.306 mm)', {}],
  ['A big hole close up: 2 mm at 10 mm (a disc’s blur, dark corners)', { Diameter: u(2e-3), Distance: u(10e-3) }],
  ['A tiny hole far back: 0.1 mm at 500 mm (diffraction: red softer than blue)', { Diameter: u(0.1e-3), Distance: u(500e-3) }],
  ['A zone plate: four open zones focusing 550 nm', { Aperture: 'Zone Plate' }],
  ['A zone plate made for blue: eight open zones at 450 nm', { Aperture: 'Zone Plate', 'Open Zones': 8, 'Zone Focus': u(450e-9) }],
  ['A camera obscura’s wall: turned 180°, a 1 mm hole at 200 mm', { View: 'Obscura', Diameter: u(1e-3), Distance: u(200e-3) }],
  ['A picture on a black wall: Edge Black, a 1.1 mm hole', { Edge: 'Black', Diameter: u(1.1e-3) }],
];

function kitPresets() {
  const out = {};
  for (const [title, spec] of PRESET_UNITS) {
    const values = {};
    for (const [name, value] of Object.entries(spec)) {
      const p = PARAMS.find((x) => x.name === name);
      if (!p) throw new Error(`preset “${title}”: no control called ${name}`);
      if (p.type === 'option') {
        const slot = p.elements.indexOf(value);
        if (slot < 0) throw new Error(`preset “${title}”: ${name} has no element ${value}`);
        values[p.id] = slot;
      } else if (p.ff.type === FF_TYPE_INTEGER) {
        if (!Number.isInteger(value) || value < p.ff.min || value > p.ff.max) throw new Error(`preset “${title}”: ${name} ${value} is outside ${p.ff.min}–${p.ff.max}`);
        values[p.id] = String(value);
      } else {
        const host = typeof value === 'object' ? core._ph_param_from(p.ff.index, value.units) : value;
        if (!Number.isFinite(host)) throw new Error(`preset “${title}”: ${name} has no conversion`);
        values[p.id] = host;
      }
    }
    out[title] = values;
  }
  return out;
}

//===========================================================================
// The renderer: one plugin instance, driven as a host drives it.
//===========================================================================

let renderer = null;

function createRenderer(gl) {
  // emscripten's GL library, pointed at the kit's context. Nothing is turned
  // on behind the kit's back: no extensions are enabled on its behalf (the
  // kit has enabled EXT_color_buffer_float, needFloat, or refused to start:
  // the plugin renders into RGBA16F).
  const handle = core.GL.registerContext(gl, { majorVersion: 2, minorVersion: 0, enableExtensionsByDefault: false });
  core.GL.makeContextCurrent(handle);
  core._ph_prepare_log();

  let instance = 0;
  let pushed = new Map();
  let clipName = 0;
  let clipTexture = null;
  let frames = 0;
  let raster = [0, 0];
  let lastRebuild = 0;///< ms, the last frame that rebuilt (LastRebuildMs() is 0 on a frame that did not)

  /** A kit texture under a GL name the plugin can bind, in emscripten's tables. */
  function glName(texture) {
    if (texture === clipTexture) return clipName;
    if (clipName) core.GL.textures[clipName] = null;
    clipName = core.GL.getNewId(core.GL.textures);
    core.GL.textures[clipName] = texture;
    texture.name = clipName;
    clipTexture = texture;
    return clipName;
  }

  function start(width, height) {
    instance = core._ph_new();
    pushed = new Map();
    if (!core._ph_init_gl(instance, width, height)) {
      const why = shaderCheck.refused > 0
        ? 'the GLSL the compiled plugin handed to GL is not this page’s checked copy of source/Shaders.cpp, so it was not compiled'
        : `the plugin’s InitGL failed${printed.length ? `: ${printed.slice(-3).join(' ')}` : ''}`;
      throw new GLError(`SW Pinhole could not start: ${why}.`);
    }
  }

  function push(params) {
    for (const p of PARAMS) {
      const value = hostValue(p, params.get(p.id));
      if (value === null || pushed.get(p.id) === value) continue;
      pushed.set(p.id, value);
      core._ph_set_float(instance, p.ff.index, value);
    }
  }

  return {
    render({ input, params, width, height }) {
      core.GL.makeContextCurrent(handle);
      if (!instance) start(width, height);
      push(params);
      if (!core._ph_process(instance, glName(input.texture), input.width, input.height)) {
        throw new GLError('The plugin’s ProcessOpenGL failed.');
      }
      // For anything checking the page from outside: frames drawn and the
      // plugin's own count of kernel rebuilds.
      frames += 1;
      raster = [input.width, input.height];
      if (core._ph_last_rebuild_ms(instance) > 0) lastRebuild = core._ph_last_rebuild_ms(instance);
      gl.canvas.dataset.frames = String(frames);
      gl.canvas.dataset.rebuilds = String(core._ph_rebuilds(instance));
    },
    stats() {
      if (!instance || !frames) return null;
      const [width, height] = raster;
      const setting = (i) => core._ph_setting(instance, i);
      return {
        aperture: setting(0),
        diameter: setting(1),
        zones: setting(2),
        focus: setting(3),
        distance: setting(4),
        u550: core._ph_fresnel_u(instance, 550e-9),
        levels: core._ph_kernel_levels(instance),
        taps: core._ph_kernel_taps(instance),
        maxTaps: core._ph_kernel_max_taps(instance),
        support: core._ph_kernel_support(instance),
        kept: [0, 1, 2].map((c) => core._ph_profile_kept(instance, c)),
        rebuildMs: lastRebuild,
        rebuilds: core._ph_rebuilds(instance),
        pixel: core._ph_frame_height() / height,
        width,
        height,
      };
    },
    log() {
      try {
        return core.FS.readFile(text(core._ph_log_path()), { encoding: 'utf8' });
      } catch {
        return '';
      }
    },
    shaders: shaderCheck,
  };
}

//===========================================================================
// What this page does not reproduce. The kit adds its four.
//===========================================================================

const DIFFERENCES = [
  'This page runs the plugin rather than a port of it. pinhole-core.wasm is source/Pinhole.cpp — the plugin class: its constructor, the rebuild key, the RGBA16F pyramid and sum buffers, the tap texture, every uniform and the GL state it saves and puts back — and everything it calls (Optics, Controls, PassBuffer, Shaders, Diag), with the FFGL SDK’s CFFGLPlugin, parameter bookkeeping, FFGLShader and FFGLFBO, all compiled unmodified by emscripten (demo/tools/build-wasm.sh). The Fresnel patterns (the projection of the pupil through G(Y) and one FFT, per wavelength, six to a band) and the pyramid’s taps are that C++, in double. Only the registration file is left out; the page constructs the plugin itself, as the plugin’s harness does. The panel is read back from the plugin’s own declarations through the SDK’s host getters, and the numbers beside the sliders are Controls.cpp’s conversions, compiled in; their units, every tooltip and the presets are this page’s.',
  'The GLSL is the plugin’s: demo/shaders.js is spliced from source/Shaders.cpp, with the order AssembleVertex() and Assemble() join the pieces in, and the repository’s verify script fails if a character differs, or if any uniform a pass declares is not set by the setter its type takes. At start-up the page also requires each of the eight stages the compiled plugin hands to glShaderSource to equal its copy, and refuses to compile anything else. The kit’s port() then changes the version line and adds the ES precision defaults (highp). GL is emscripten’s WebGL2 library on this page’s context, not a GL 4.1 driver, and four entry points are the page’s (demo/wasm/gl_shim.cpp): glShaderSource, for that check and port, and glEnable, glDisable and glIsEnabled, because GL_PROGRAM_POINT_SIZE — state the plugin saves and restores each frame, as a plugin in Resolume must — does not exist in WebGL2. It reads as off here; the plugin draws no points.',
  'The pyramid’s levels and sum buffers are RGBA16F, as in the plugin, and rendering into a half-float buffer needs WebGL2’s EXT_color_buffer_float: a browser without it is refused rather than handed an 8-bit chain. The taps are one RGBA32F texture read texel by texel (texelFetch, nearest), so no float filtering is needed. The format is the plugin’s, but the arithmetic is this browser’s GPU (through ANGLE, on a Mac through Metal), not the Mac’s GL driver; the half floats round the same way only where both follow IEEE 754’s binary16.',
  'The raster is the page’s Composition menu (960 × 540 unless you pick another), or your own image’s size. The plugin’s screen is 24 mm tall at every raster, so a pixel is 24 mm divided by the raster’s height (44.4 µm at 540 lines, 22.2 µm at 1080), the kernel is rebuilt for each height, and the same settings blur the same share of the frame at every size: twice the pixels at twice the lines. The line under the picture says what the plugin built for this raster.',
  'The input is the kit’s texture, unpadded: Width and HardwareWidth are the same, and the page’s viewport is the whole canvas at the origin. The plugin reads its input texel by texel inside Width × Height and draws through the host’s viewport, so neither should matter, but a padded Resolume texture and a viewport off the origin are not exercised here. The output is the page’s 8-bit canvas: the plugin’s float picture is rounded to 8 bits once, at the end.',
  'Alpha: the kit hands the plugin premultiplied colour (its clips and your own image alike are premultiplied on upload), and the plugin weights the clip’s light by α as it should for straight colour, so over a half-transparent edge the light is weighted twice. The page’s clips are all opaque, which is why the kit’s shape-on-transparency clip is not among them; an image of your own with transparency would show the double weighting at its edges. Which of the two Resolume hands an effect is an open question in the plugin’s own AGENTS.md.',
  'The patterns and the taps are built on this page’s thread, in WebAssembly, whenever the aperture, the distance or the raster’s height changes (dragging Diameter or Distance rebuilds every frame; the line under the picture gives the last rebuild’s time, which is this browser’s, not the 10–11 ms measured natively on an M4 Max). WebAssembly is not the Mac’s arm64: it has no fused multiply-add, and its exp, sin, pow and log are musl’s, not Apple’s, so the patterns’ doubles can differ from the plugin built for a Mac in the last digits; the taps reach the GPU as float, as in the plugin.',
  'Compared with the plugin’s harness once, on an Apple M4 Max when this page was built: the same input frame (the clip, read back from this page at Mix 0) through phtest --pipe — the real plugin class in a GL 4.1 context — at the same raster, with the same host values, in nine cases (the defaults on two clips and at 720p, the zone plate at 540 and 720 lines, eight zones focusing 450 nm, a 2 mm hole at 10 mm, a 0.1 mm hole at 500 mm, Obscura with Black edges). Seven came out identical byte for byte; the other two differed in 1 of 1.6 million and 4 of 2.8 million colour values, by one 8-bit code. That was this page through ANGLE on Metal against the harness through Apple’s OpenGL, on the same GPU; another GPU rounds its own way. demo/tools/crosscheck.py in the repository repeats it; nothing repeats it by itself.',
  'Pinhole keeps no time and draws nothing at random: the picture is a function of the clip and the controls alone. The clips move and the plugin follows them; Pause, Step and Restart act on the clip, never on the plugin. Each page frame is one ProcessOpenGL, and a frame here is one of the display’s refreshes, not Resolume’s output frame.',
  'Open Zones is FF_TYPE_INTEGER in the plugin and a number field here, clamped to its declared range (1–16). The About block (a credit line and its buttons) is declared by the plugin and left off this panel.',
  'Nothing here is measured, and the plugin has never been loaded into Resolume. Its harness, phtest, drives the same class headlessly and checks the maths: the pattern builder against Lommel’s tables as Rayleigh printed them (69 of 80 to half their last digit) and Rayleigh’s closed form on the axis, an edge’s spread against a disc’s and a band-mean Airy pattern’s, the optimum against Mielenz’s, cos⁴ against the screen’s geometry, and 17 wrong models and 51 one-character mutants caught. That harness, not this page, is the reason to believe the picture.',
];

//===========================================================================
// The page.
//===========================================================================

const mounted = mountDemo({
  name: 'Pinhole',
  pluginId: 'PC01',
  kind: 'effect',
  tagline:
    'A pinhole camera and camera obscura looking at your clip, for Resolume. There is no lens: a hole in a thin plate throws the clip onto a 24 mm screen, and the hole does not blur a sharp picture, it makes the picture by Fresnel diffraction. Every point lands as the hole’s diffraction pattern, one per colour band, so the picture is sharpest at one hole size and softer either side, red softer than blue for small holes, with a faint veil round highlights and cos⁴ dark corners. Swap the hole for a Fresnel zone plate and one colour comes sharp in the glow of the light the plate does not focus.',
  repo: 'https://github.com/stoatworks-labs/pinhole',

  blurb:
    'It is the plugin’s own C++ — the plugin class and its Fresnel pattern builder, in double — compiled unmodified to WebAssembly, drawing with its own GLSL in WebGL2, on generated clips in this page. The host is this page.',

  // The pyramid and the sum buffers are RGBA16F render targets, as in the plugin.
  needFloat: true,
  // The plugin writes alpha mix( a, 1, Mix ): 1 over the page's opaque clips.
  showBackdrop: false,

  // All opaque: the kit's 'alpha' clip is left out because the kit's
  // premultiplied colour is not the straight colour the plugin weights by
  // alpha (see the notes at the foot).
  sources: ['scene', 'spot', 'grid', 'detail', 'bars', 'ramp'],

  params: PARAMS,
  presets: kitPresets(),
  differences: DIFFERENCES,

  createRenderer: (gl) => {
    renderer = createRenderer(gl);
    return renderer;
  },
});

//===========================================================================
// Open Zones as a number field, and the line under the picture.
//===========================================================================

if (mounted) {
  const embed = new URLSearchParams(window.location.search).has('embed')
    && new URLSearchParams(window.location.search).get('embed') !== '0';
  if (!embed) {
    for (const p of PARAMS) {
      if (p.ff.type !== FF_TYPE_INTEGER) continue;
      const field = document.getElementById(`p-${p.id}`);
      if (!field) continue;
      field.type = 'number';
      field.min = String(p.ff.min);
      field.max = String(p.ff.max);
      field.step = '1';
      field.inputMode = 'numeric';
    }
    buildStatusLine();
  }
}

//---------------------------------------------------------------------------
// The line under the picture: the plugin's own state, through the accessors
// its harness reads. It reports; it measures nothing.
//---------------------------------------------------------------------------
function buildStatusLine() {
  const stage = document.querySelector('.stage');
  if (!stage) return;
  const line = document.createElement('p');
  line.className = 'stage__status';
  line.id = 'pinhole-status';
  line.setAttribute('aria-live', 'off');
  const log = document.createElement('p');
  log.className = 'stage__status';
  log.id = 'pinhole-log';
  log.setAttribute('aria-live', 'off');
  log.style.fontFamily = 'ui-monospace, SFMono-Regular, Menlo, monospace';
  log.style.fontSize = '12px';
  log.style.whiteSpace = 'pre-wrap';
  log.style.overflowWrap = 'anywhere';
  stage.append(line, log);

  let lastLine = '';
  let lastLog = '';
  setInterval(() => {
    const s = renderer?.stats();
    if (!s) return;
    const plate = s.aperture === 1;
    const what = plate
      ? `A zone plate of ${s.zones} open zones focusing ${(s.focus * 1e9).toFixed(0)} nm at ${sig(s.distance * 1e3)} mm`
      : `A ${sig(s.diameter * 1e3)} mm hole at ${sig(s.distance * 1e3)} mm`;
    const next = `${what}: u = ${(s.u550 / Math.PI).toFixed(2)}π at 550 nm`
      + ` · the kernel: the core and ${s.levels} coarser level${s.levels === 1 ? '' : 's'}, ${s.taps} taps (≤ ${s.maxTaps} a level),`
      + ` reaching ${s.support.toFixed(1)} px (R G B keep ${s.kept.map((k) => (k * 100).toFixed(1)).join(', ')}% of their light there)`
      + ` · built ${s.rebuilds} time${s.rebuilds === 1 ? '' : 's'}${s.rebuildMs > 0 ? `, the last in ${s.rebuildMs.toFixed(0)} ms` : ''}`
      + ` · a pixel is ${(s.pixel * 1e6).toFixed(1)} µm of the 24 mm screen at ${s.width} × ${s.height}`;
    if (next !== lastLine) {
      lastLine = next;
      line.textContent = next;
    }
    const textLog = renderer.log();
    if (textLog === lastLog) return;
    lastLog = textLog;
    const lines = textLog.trim().split('\n').filter(Boolean).map((l) => l.replace(/^\S+ (INFO |WARN |ERROR) pinhole: /, '$1 '));
    log.textContent = lines.length ? `The plugin’s own log (Diag.cpp, in this page’s memory):\n${lines.slice(-2).join('\n')}` : '';
  }, 250);
}

// For checking the page from outside (the crosscheck against phtest reads the
// declarations and the inverse conversions through these).
export { renderer, mounted, PARAMS, DECLARED, core };
