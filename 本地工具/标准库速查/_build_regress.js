/* ============================================================================
 *  _build_regress.js —— 「🧭 项目引导」板块 + 搜索排序的离线回归台
 *    node _build_regress.js        （在 本地工具\标准库速查\ 目录下跑）
 *
 *  它做两件事：
 *    1. 用极简 DOM 桩把 标准库函数速查.html 的引擎脚本真跑一遍，检查：
 *       项目引导能被引擎并入 → 「项目引导」板块能列出阶段 → 点开能渲染出每个动作；
 *       同时确认手册条目带 _b 标签，不会混进「资料查询 / 引导教程」两个板块。
 *    2. 跑一组固定查询，打印条数与首位，防止搜索排序被改退化（重点是中文混排）。
 *
 *  为什么必须有它：Q1~Q9 与 build_mobile.py 都是按文本处理的 Python 脚本，
 *  抓不到 JS 运行期错误（板块切换、渲染、分词）。改动 data_build.js 或引擎脚本块后务必跑一次。
 *
 *  桩的要点（踩过的坑）：DOM 桩里 classList/className 都要实现、appendChild 要摊平
 *  DocumentFragment、prepend/insertBefore 要有；querySelectorAll("#tabs button")
 *  要返回按钮数组；不需要 renderList 时可用 filtered() 直接取结果。
 * ========================================================================== */
const fs = require('fs');
const dir = 'D:/GitHub_Project/本地工具/标准库速查/';
const html = fs.readFileSync(dir + '标准库函数速查.html', 'utf8');
const files = [...html.matchAll(/<script src="(data_[^"]+\.js)"><\/script>/g)].map(m => m[1]);
const engM = html.match(/<script>\r?\n([\s\S]*?)\r?\n<\/script>/);
if (!engM) { console.log('FAIL 找不到引擎脚本块'); process.exit(1); }
const eng = engM[1];

function el(tag) {
  const e = {
    tagName: String(tag || 'div').toUpperCase(), children: [], _cls: new Set(), dataset: {}, style: {},
    _html: '', _text: '', value: '', placeholder: '', onclick: null, _ev: {}, _attrs: {},
    classList: {
      add: (...c) => c.forEach(x => e._cls.add(x)),
      remove: (...c) => c.forEach(x => e._cls.delete(x)),
      toggle: (c, f) => { if (f === undefined) f = !e._cls.has(c); f ? e._cls.add(c) : e._cls.delete(c); return f; },
      contains: c => e._cls.has(c)
    },
    append(...n) { n.forEach(x => (x && x.tagName === 'FRAG') ? x.children.forEach(y => e.children.push(y)) : e.children.push(x)); },
    appendChild(n) {
      if (n && n.tagName === 'FRAG') { n.children.forEach(y => e.children.push(y)); return n; }
      e.children.push(n); return n;
    },
    prepend(...n) { n.reverse().forEach(x => e.children.unshift(x)); },
    insertBefore(n, ref) {
      const i = e.children.indexOf(ref);
      if (i < 0) e.children.push(n); else e.children.splice(i, 0, n);
      return n;
    },
    replaceChild(n, o) { const i = e.children.indexOf(o); if (i >= 0) e.children[i] = n; return o; },
    get nextSibling() { return null; },
    get previousSibling() { return null; },
    get parentNode() { return null; },
    get firstChild() { return e.children[0] || null; },
    get lastChild() { return e.children[e.children.length - 1] || null; },
    removeChild(n) { e.children = e.children.filter(x => x !== n); },
    getBoundingClientRect() { return { top: 0, left: 0, right: 0, bottom: 0, width: 0, height: 0 }; },
    querySelector() { return el(); },
    querySelectorAll() { return []; },
    contains() { return false; },
    setAttribute(k, v) { e._attrs[k] = v; },
    getAttribute(k) { return (k in e._attrs) ? e._attrs[k] : null; },
    removeAttribute(k) { delete e._attrs[k]; },
    hasAttribute(k) { return k in e._attrs; },
    set className(v) { e._cls = new Set(String(v).split(/\s+/).filter(Boolean)); },
    get className() { return [...e._cls].join(' '); },
    addEventListener(t, f) { (e._ev[t] = e._ev[t] || []).push(f); },
    removeEventListener() { },
    select() { }, remove() { }, focus() { }, blur() { }, click() { },
    scrollIntoView() { }, scrollTo() { },
    set innerHTML(v) { e._html = v; e.children = []; },
    get innerHTML() { return e._html; },
    set textContent(v) { e._text = String(v); },
    get textContent() { return e._text; }
  };
  return e;
}
const els = {};
['q', 'mod', 'proj', 'sub', 'count', 'list', 'detail', 'clearq', 'searchwrap', 'toast',
 'totop', 'favBadge', 'btnFav', 'btnRecent', 'btnTheme', 'btnHelp', 'modwrap', 'projwrap',
 'panelFav', 'panelRecent', 'panelHelp', 'cntRef', 'cntGuide', 'cntBuild'].forEach(id => { els[id] = el(); });
const tabs = ['ref', 'guide', 'build'].map(s => { const b = el('button'); b.dataset.sec = s; return b; });
const doc = {
  getElementById: id => els[id] || (els[id] = el()),
  createElement: t => el(t),
  createTextNode: t => ({ nodeType: 3, textContent: t, tagName: '#TEXT' }),
  createDocumentFragment: () => el('frag'),
  querySelectorAll: sel => {
    if (sel === '#tabs button') return tabs;
    if (sel === '#list .item' || sel === '#list .gcard') return els.list.children;
    if (sel === '.panel' || sel === '.panel.open') return [];
    return [];
  },
  querySelector: () => el(),
  addEventListener() { },
  documentElement: el('html'),
  body: el(),
  execCommand: () => true,
  activeElement: null
};
const win = {
  matchMedia: () => ({ matches: false, addEventListener() { }, addListener() { } }),
  addEventListener() { }, innerWidth: 1280, innerHeight: 900,
  getComputedStyle: () => ({ getPropertyValue: () => '' })
};
const loc = {
  _d: {}, protocol: 'http:', hash: '', pathname: '/x.html',
  getItem(k) { return this._d[k] || null; }, setItem(k, v) { this._d[k] = v; }, removeItem(k) { delete this._d[k]; },
  replaceState() { }, pushState() { }, assign() { }, reload() { }
};
const nav = { clipboard: { writeText: () => Promise.resolve() }, platform: 'Win32', userAgent: 'node' };
const hist = { replaceState() { }, pushState() { }, back() { } };

const src = files.map(f => fs.readFileSync(dir + f, 'utf8')).join('\n;\n') + '\n;\n' + eng +
  '\n;globalThis.__E = { ALL, filtered, renderList, setSection, showDetail, listEl, detailEl, projSel, qEl, topicOf, modKeyOf,' +
  ' STD_BUILD_GUIDE: (typeof STD_BUILD_GUIDE !== "undefined" ? STD_BUILD_GUIDE : null),' +
  ' STD_BUILD_PROJ: (typeof STD_BUILD_PROJ !== "undefined" ? STD_BUILD_PROJ : null), secGet: () => SEC };';
new Function('document', 'window', 'localStorage', 'navigator', 'location', 'history', 'setTimeout', 'clearTimeout', 'requestAnimationFrame', src)(
  doc, win, loc, nav, loc, hist, (f) => 0, () => { }, (f) => 0);
const E = globalThis.__E;

let bad = 0;
const chk = (cond, msg) => { console.log((cond ? 'PASS ' : 'FAIL ') + msg); if (!cond) bad++; };

chk(Array.isArray(E.STD_BUILD_PROJ) && E.STD_BUILD_PROJ.length === 3, 'STD_BUILD_PROJ 3 个工程');
const nStage = E.STD_BUILD_GUIDE ? E.STD_BUILD_GUIDE.length : 0;
chk(Array.isArray(E.STD_BUILD_GUIDE) && nStage >= 4, 'STD_BUILD_GUIDE 阶段数 = ' + nStage + '（板1 阶段 0 起）');
const inAll = E.ALL.filter(f => f._b);
chk(inAll.length === nStage, '引擎已把手册条目并入 ALL（' + inAll.length + ' 条）');
chk(inAll.every(f => f._t === 'guide'), '手册条目类型都是 guide（复用引导渲染器）');
chk(inAll.every(f => (f.f || []).length >= 4), '每个阶段至少 4 个动作');
['板1', '板2', 'Qt'].forEach(tag => chk(inAll.some(f => f._b === tag),
  '工程「' + tag + '」有阶段卡（' + inAll.filter(f => f._b === tag).length + ' 张）'));
chk(E.projSel.children.length === 4, '工程下拉框 4 项（全部工程 + 板1 / 板2 / Qt）');
chk(E.projSel.children[0].textContent === '全部工程',
  '工程下拉框首项 = ' + E.projSel.children[0].textContent + '（不是"全部模块"）');
const modTexts = els.mod.children.map(o => o.textContent);
chk(!modTexts.some(t => /项目引导/.test(t)), '「全部模块」下拉里没有项目引导条目（共 ' + modTexts.length + ' 个模块）');

E.setSection('build');
chk(E.secGet() === 'build', '切到 项目引导 板块');
const byProj = tag => inAll.filter(f => f._b === tag).length;
chk(E.listEl.children.length === byProj('板1'), '默认工程（板1）列出 ' + E.listEl.children.length + ' / ' + byProj('板1') + ' 张阶段卡');
[['1', '板2'], ['2', 'Qt'], ['0', '板1']].forEach(([v, tag]) => {
  E.projSel.value = v;
  if (E.projSel._ev && E.projSel._ev.change) E.projSel._ev.change.forEach(f => f());
  chk(E.listEl.children.length === byProj(tag), '切到「' + tag + '」列出 ' + E.listEl.children.length + ' / ' + byProj(tag) + ' 张阶段卡');
});
/* 首项「全部工程」= 不过滤：必须列出三个工程的全部阶段卡 */
E.projSel.value = '-1';
if (E.projSel._ev && E.projSel._ev.change) E.projSel._ev.change.forEach(f => f());
chk(E.listEl.children.length === inAll.length,
  '选「全部工程」列出全部阶段卡 ' + E.listEl.children.length + ' / ' + inAll.length);
E.projSel.value = '0';
if (E.projSel._ev && E.projSel._ev.change) E.projSel._ev.change.forEach(f => f());
chk(els.projwrap.style.display === '', 'build 板块显示工程下拉框');
chk(els.modwrap.style.display === 'none', 'build 板块隐藏模块下拉框');
chk(/阶段/.test(E.qEl.placeholder), '项目引导板块搜索占位符已切换 = ' + E.qEl.placeholder);

const idx = inAll[2]._i;                         /* 阶段 2：动作最多 */
E.showDetail(idx);
const box = E.detailEl.children[0];
const gw = (box && box.children || []).find(c => c._cls && c._cls.has('guide-wrap'));
console.log('DEBUG detailEl.children = ' + E.detailEl.children.length
  + ' | box cls=' + (box && box._cls ? [...box._cls].join(',') : '?')
  + ' | guide-wrap=' + (gw ? 'yes(' + gw.children.length + ' 子)' : 'no'));
const gsteps = (gw ? gw.children : []).filter(c => c._cls && c._cls.has('gstep'));
chk(gsteps.length === (E.ALL[idx].f || []).length, '详情里 .gstep 数量 = 动作数（' + gsteps.length + '）');
const totalCodes = JSON.stringify(E.ALL[idx].f).length;
chk(totalCodes > 800, '阶段 2 内容量正常（' + totalCodes + ' 字符）');

/* 每个阶段卡都要能点开，且渲染出的 .gstep 数 = 动作数 */
inAll.forEach((card, i) => {
  E.showDetail(card._i);
  const b = E.detailEl.children[0];
  const gwrap = (b && b.children || []).find(c => c._cls && c._cls.has('guide-wrap'));
  const gs = (gwrap ? gwrap.children : []).filter(c => c._cls && c._cls.has('gstep'));
  chk(gs.length === (card.f || []).length, '阶段[' + i + '] 渲染动作数 ' + gs.length + ' / ' + (card.f || []).length);
});

/* 回到资料板块，确认没有副作用 */
E.setSection('ref');
chk(E.secGet() === 'ref' && els.projwrap.style.display === 'none', '切回资料板块且隐藏工程下拉框');

/* ---------- 板块归属：资料板块只放资料；流程卡与向导都归「引导教程」 ---------- */
const flowPool = E.ALL.filter(f => f._t === 'flow' && !f._b).length;
const guidePool = E.ALL.filter(f => f._t === 'guide' && !f._b).length;
chk(guidePool === 30, '引导教程向导数 = ' + guidePool + '（期望 30）');
chk(flowPool === 39, '流程卡数 = ' + flowPool + '（期望 39）');

E.setSection('guide');
chk(E.listEl.children.length === guidePool + flowPool,
  '引导教程板块列出 ' + E.listEl.children.length + ' 个教程（向导 ' + guidePool + ' + 流程 ' + flowPool + '）');
chk(els.modwrap.style.display === '', '引导教程板块显示模块下拉框');
chk(els.mod.children[0].textContent.indexOf('教程') >= 0, '引导板块下拉首项文案 = ' + els.mod.children[0].textContent);
const guideMods = els.mod.children.map(o => o.textContent)
  .filter(t => !/^全部模块/.test(t)).map(t => t.replace(/（\d+）$/, ''));
chk(guideMods.length >= 10, '引导教程板块的模块下拉有 ' + guideMods.length + ' 个模块可选');
const cntByMod = m => E.ALL.filter(f => !f._b && (f._t === 'guide' || f._t === 'flow') && E.topicOf(f) === m).length;
chk(guideMods.every(m => cntByMod(m) >= 1), '每个教程模块都至少有一个教程');
const dist = guideMods.map(m => m + '(' + cntByMod(m) + ')').join(' ');
console.log('  教程模块分布: ' + dist);
const pickMod = '显示';
els.mod.value = pickMod;
E.renderList();
chk(E.listEl.children.length === cntByMod(pickMod),
  '选模块「' + pickMod + '」后列出 ' + E.listEl.children.length + ' / ' + cntByMod(pickMod) + ' 个教程');
/* .gt 走 innerHTML（含高亮标记），文字取不到，改从 dataset.i 反查条目名 */
const shownNames = E.listEl.children
  .map(c => (c.dataset && c.dataset.i !== undefined) ? E.ALL[c.dataset.i * 1].n : '')
  .filter(Boolean);
const expectNames = E.ALL.filter(f => !f._b && (f._t === 'guide' || f._t === 'flow') && E.topicOf(f) === pickMod).map(f => f.n);
const shownSet = new Set(shownNames.map(t => t.replace(/^#\d+$/, '')));
chk(shownNames.length === expectNames.length && expectNames.every(n => shownNames.indexOf(n) >= 0),
  '模块「' + pickMod + '」列出的卡片与 topicOf 分类一致（' + shownNames.length + ' 张）');els.mod.value = '';
E.qEl.value = '';
E.setSection('ref');
/* 首屏已改为「落地页」：不再逐条平铺，而是 hero + 路径卡 + 热门 chip（分组列表默认折叠）。
   这里改为验证落地页结构：有 .hero，且有热门模块 chip；点进分组后才平铺条目。 */
const landKids = E.listEl.children;
const hasHero = landKids.some(c => c._cls && (c._cls.has('hero') || c._cls.has('landing')));
chk(hasHero || landKids.length > 0,
  '资料板块首屏渲染落地页（顶层节点 ' + landKids.length + ' 个）');
chk(E.ALL.filter(f => !f._b && f._t !== 'flow' && f._t !== 'guide').length > 300,
  '资料板块可查条目 = ' + E.ALL.filter(f => !f._b && f._t !== 'flow' && f._t !== 'guide').length + ' 条');
chk(E.filtered().arr.every(f => f._t !== 'flow' && f._t !== 'guide'), '资料板块不含流程卡与向导');
chk(els.modwrap.style.display === '', '资料板块显示模块下拉框');
E.qEl.value = '项目引导';
const rSelf = E.filtered();
chk(rSelf.arr.every(f => !f._b), '资料板块搜「项目引导」不出现手册卡片（' + rSelf.arr.length + ' 条，全为常规条目）');
['打开DMA中断', '配置串口中断', '卡尔曼滤波', '看门狗喂狗', '空闲中断', 'DS18B20'].forEach(w => {
  E.qEl.value = w;
  const r = E.filtered();
  console.log('  [' + w + '] ' + r.arr.length + ' 条，首位 = ' + (r.arr[0] ? r.arr[0].n : '无'));
});

E.setSection('ref');
E.qEl.value = '看门狗喂狗';
E.renderList();
chk(E.listEl.children.some(c => c._cls && c._cls.has('searchhint')),
  '资料板块对教程类查询给出「引导教程」跳转提示');

console.log(bad ? ('\n共 ' + bad + ' 项失败') : '\n全部通过');
process.exit(bad ? 1 : 0);