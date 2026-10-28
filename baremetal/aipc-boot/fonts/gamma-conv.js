#!/usr/bin/env node
/*
 * Usage: node gamma-conv.js GAMMA <lv_font_conv arguments>
 *
 * A gamma below 1 raises the partly covered edge pixels, which this panel
 * shows too dark.
 */
'use strict';

const path = require('path');
const base = path.dirname(require.resolve('lv_font_conv/package.json'));
const key = require.resolve(path.join(base, 'lib/collect_font_data'));
const collect = require(key);
const gamma = parseFloat(process.argv[2]);

/* lv_font_conv records process.argv in the header of the font. */
process.argv.splice(2, 1);

require.cache[key].exports = async function (args) {
  const data = await collect(args);

  for (const g of data.glyphs)
    g.pixels = g.pixels.map(line =>
      line.map(p => Math.round(255 * Math.pow(p / 255, gamma))));
  return data;
};

require(path.join(base, 'lib/cli')).run(process.argv.slice(2)).catch(err => {
  console.error(err.stack || err.message);
  process.exit(1);
});
