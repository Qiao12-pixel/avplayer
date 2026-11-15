// Verify interval, non-overlap, hidden-tab pause and disposal without a browser.
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { transform } from '../apps/web/node_modules/esbuild/lib/main.js';
const source = await readFile(new URL('../apps/web/src/weather.ts', import.meta.url), 'utf8');
const { code } = await transform(source, { loader: 'ts', format: 'esm' });
const { mountWeather } = await import('data:text/javascript;base64,' + Buffer.from(code).toString('base64'));
const listeners = new Map();
const elements = Object.fromEntries(['.weather-city','.weather-condition','.weather-temperature','.weather-icon'].map(key => [key, {
  value: '北京', textContent: '', addEventListener: (name, fn) => listeners.set(key + name, fn),
  removeEventListener: (name) => listeners.delete(key + name),
}]));
let interval, delay, cleared = false, calls = 0, resolveFetch;
globalThis.window = { setInterval(fn, ms) { interval=fn;delay=ms;return 1; }, clearInterval() { cleared=true; }, setTimeout() { return 2; }, clearTimeout() {} };
globalThis.document = { hidden: false, addEventListener(name,fn) { listeners.set(name,fn); }, removeEventListener(name) { listeners.delete(name); } };
globalThis.fetch = () => { calls++;return new Promise(resolve => resolveFetch=resolve); };
const root = { innerHTML: '', querySelector: key => elements[key] };
const dispose = mountWeather(root);
assert.equal(delay,10000);assert.equal(calls,1);
interval();assert.equal(calls,1,'pending query must not overlap');
const flush = () => new Promise(resolve => setImmediate(resolve));
resolveFetch({ ok:true,json:async()=>({success:true,text:'城市: 北京, 北京市, 中国\n天气: 晴 (0)\n温度: 8 °C'}) });await flush();
assert.equal(elements['.weather-city'].textContent,'北京');
assert.equal(elements['.weather-condition'].textContent,'晴');
assert.equal(elements['.weather-temperature'].textContent,'8°C');
assert.equal(elements['.weather-icon'].textContent,'☀');
interval();assert.equal(calls,2,'next interval queries again');
resolveFetch({ ok:true,json:async()=>({success:true,text:'城市: 北京\n天气: 多云 (3)\n温度: 9 °C'}) });await flush();
document.hidden=true;interval();assert.equal(calls,2);
document.hidden=false;listeners.get('visibilitychange')();assert.equal(calls,3);
dispose();assert.ok(cleared);interval();assert.equal(calls,3);
console.log('PASS: 10-second polling, no overlap, hidden pause/resume and disposal');

// Browser permission callback, reverse lookup, then weather for the located city.
let geolocationSuccess;
Object.defineProperty(globalThis, 'navigator', { configurable:true, value:{geolocation:{
  getCurrentPosition(success,_error,options) { geolocationSuccess=success;assert.equal(options.enableHighAccuracy,false); }
}}});
let urls=[];
globalThis.fetch=async(url)=>{urls.push(url);return {ok:true,json:async()=>url.startsWith('/location')
  ? {success:true,city:'上海市'}
  : {success:true,text:`城市: ${url.includes(encodeURIComponent('上海市'))?'上海':'北京'}\n天气: 多云 (3)\n温度: 15 °C`}};};
const release=mountWeather(root);await flush();
await geolocationSuccess({coords:{latitude:31.234567,longitude:121.476543}});await flush();
assert.ok(urls.some(url=>url==='/location?lat=31.2&lon=121.5'));
assert.ok(urls.some(url=>url.includes(encodeURIComponent('上海市'))));
assert.equal(elements['.weather-city'].textContent,'上海');
release();
console.log('PASS: permission callback, coarse coordinates, located-city weather and cleanup');
