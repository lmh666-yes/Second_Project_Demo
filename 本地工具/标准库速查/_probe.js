/* 引擎探针（完整 DOM 桩，能跑 boot） */
const fs = require('fs'), vm = require('vm'), path = require('path');
const dir = __dirname;
const html = fs.readFileSync(path.join(dir, '标准库函数速查.html'), 'utf8');
const srcs = [...html.matchAll(/<script src="([^"]+)"><\/script>/g)].map(m => m[1]);
const code = html.slice(html.lastIndexOf('<script>')).replace(/^<script>/, '').replace(/<\/script>[\s\S]*$/, '');

function mkEl() {
  const e = {
    style: {}, dataset: {}, children: [], _h: '', textContent: '', value: '', placeholder: '', scrollTop: 0,
    classList: { add() {}, remove() {}, toggle() {}, contains() { return false; } },
    appendChild(c) { this.children.push(c); return c; },
    append(...c) { c.forEach(x => this.appendChild(x)); },
    insertBefore(c) { this.children.push(c); return c; },
    prepend() {}, removeChild() {}, remove() {}, blur() {}, select() {}, focus() {},
    querySelector() { return null; }, querySelectorAll() { return []; },
    closest() { return null; }, setAttribute() {}, getAttribute() { return null; },
    scrollIntoView() {}, scrollTo() {}, click() {},
    set innerHTML(v) { this._h = v; this.children = []; }, get innerHTML() { return this._h; },
    addEventListener() {}, onclick: null,
  };
  return e;
}
const els = {};
const docStub = {
  getElementById: id => els[id] || (els[id] = mkEl()), createElement: () => mkEl(),
  createDocumentFragment: () => mkEl(), querySelectorAll: () => [], querySelector: () => null,
  body: mkEl(), documentElement: mkEl(), addEventListener() {},
  createTextNode: t => ({ nodeType: 3, textContent: t }),
};
const winStub = { matchMedia: () => ({ matches: false, addEventListener() {} }), document: docStub };
const sb = {
  console, document: docStub, window: winStub, navigator: {},
  localStorage: { getItem: () => null, setItem() {}, removeItem() {} },
  location: { hash: '', protocol: 'file:' }, history: { replaceState() {} },
  setTimeout: () => 0, clearTimeout() {}, setInterval: () => 0,
  Event: function () { return {}; }, requestAnimationFrame: () => 0,
};
sb.globalThis = sb;
const ctx = vm.createContext(sb);
for (const f of srcs) vm.runInContext(fs.readFileSync(path.join(dir, f), 'utf8'), ctx, { filename: f });
vm.runInContext(code, ctx, { filename: 'engine' });

const qEl = els['q'], modSel = els['mod'];
global.__ctx = ctx; global.__els = els;

function run(sec, q, mod) {
  vm.runInContext('SEC=' + JSON.stringify(sec) + ';', ctx);
  qEl.value = q; modSel.value = mod || '';
  return vm.runInContext('filtered()', ctx);
}
function related() { return vm.runInContext('relatedTutorials(3).map(function(f){return f.n})', ctx); }

const mode = process.argv[2] || 'search';
const queries = [
  ['ref', '打开DMA中断'], ['ref', '串口 中断'], ['ref', 'USART_Init'], ['ref', '初始化定时器'],
  ['ref', '看门狗'], ['ref', '占空比'], ['ref', '中断优先级'], ['ref', '卡尔曼滤波'],
  ['ref', 'oled显示'], ['ref', 'flash擦除'], ['ref', '初使化'], ['ref', 'CAN_Init'],
  ['ref', '温湿度'], ['ref', '空闲中断'], ['ref', 'gpio'], ['ref', 'dma'], ['ref', 'it'],
  ['ref', '定时器'], ['ref', '串口'], ['ref', 'spi'], ['ref', 'adc'], ['ref', 'rtc'],
  ['ref', 'freertos'], ['ref', 'pid'], ['ref', 'crc'], ['ref', 'modbus'], ['ref', 'lora'],
  ['ref', 'xpt2046'], ['ref', 'mpu6050'], ['ref', 'w25q'], ['ref', 'iic'], ['ref', 'uart'],
  ['ref', '中断'], ['ref', '初始化'], ['ref', '配置'], ['ref', '读取'], ['ref', '发送'], ['ref', '使能'],
  ['ref', '比较值'], ['ref', '捕获'], ['ref', '闹钟'], ['ref', '扇区'], ['ref', '校准'],
  ['ref', '步进电机'], ['ref', '舵机'], ['ref', '触摸屏'], ['ref', '组帧'], ['ref', '双板联调'],
  ['ref', 'printf重定向'], ['ref', '低功耗'], ['ref', '看门够'], ['ref', '卡尔漫'],
  ['ref', '串口DMA'], ['ref', '帧头'], ['ref', '断网缓存'],
  ['guide', 'FreeRTOS'], ['guide', 'RTC 电子钟'], ['guide', '串口'], ['guide', '定时器'], ['guide', 'PID'],
  ['guide', '卡尔曼'], ['guide', 'lora'], ['guide', '触摸'],
];

if (mode === 'search') {
  for (const [sec, q] of queries) {
    let r;
    try { r = run(sec, q); } catch (e) { console.log('[' + sec + '] "' + q + '" ERROR ' + e.message); continue; }
    const top = r.arr.slice(0, 5).map(f => f.n).join(' | ');
    const rel = r.words.length ? related() : [];
    console.log('[' + sec + '] "' + q + '"  命中' + r.arr.length + (r.broad ? ' [宽泛]' : '') + (r.relaxed ? '(宽松)' : '') +
      (rel.length ? '  教程:' + rel.length : '') + '\n     ' + top);
  }
}
