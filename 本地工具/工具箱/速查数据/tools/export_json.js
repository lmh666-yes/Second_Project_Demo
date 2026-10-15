/* ============================================================================
 *  export_json.js —— 把网页版「标准库速查」的数据导出成桌面版可读的 dataset.json
 *
 *  为什么这么做（借壳导出）：
 *    网页里的条目不是各 data_*.js 的简单并集——引擎脚本块里有一段合并逻辑
 *    （pushCard / ALL，负责去重、打 _t 类型标签、拼 _hay 检索语料、挂 u/rel、并入手册条目）。
 *    如果 C++ 侧自己重写一遍合并逻辑，两边迟早不一致。
 *    所以这里照搬 _build_regress.js 已验证的做法：用极简 DOM 桩把引擎脚本块**真跑一遍**，
 *    直接取它合并后的 ALL 与搜索用表，JSON 落盘。
 *
 *  用法：node export_json.js          （产物写到 ../data/dataset.json）
 *
 *  注意：data_*.js 的文件清单与顺序都从 HTML 里抓（不要手写——历史上漏过 data_flow.js，
 *        手机版与 APK 静默丢了整个「初始化流程卡」板块）。
 * ========================================================================== */
const fs = require('fs');
const path = require('path');

const SRC_DIR = path.resolve(__dirname, '..', '..', '..', '标准库速查');
const HTML = path.join(SRC_DIR, '标准库函数速查.html');
const OUT = path.resolve(__dirname, '..', 'data', 'dataset.json');

const html = fs.readFileSync(HTML, 'utf8');
const files = [...html.matchAll(/<script src="(data_[^"]+\.js)"><\/script>/g)].map(m => m[1]);
const engM = html.match(/<script>\r?\n([\s\S]*?)\r?\n<\/script>/);
if (!files.length) { console.error('FAIL 没找到 data_*.js 引用，检查 ' + HTML); process.exit(1); }
if (!engM) { console.error('FAIL 找不到引擎脚本块'); process.exit(1); }
const eng = engM[1];

/* ---------- 极简 DOM 桩（照搬 _build_regress.js 第 26-109 行） ---------- */
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

/* ---------- 拼源码：数据文件（按 HTML 顺序）+ 引擎块 + 收尾导出 ---------- */
const footer = `
;globalThis.__DATA = {
  all: ALL,
  search: {
    concept:      (typeof CONCEPT !== 'undefined') ? CONCEPT : null,
    typo:         (typeof TYPO !== 'undefined') ? TYPO : null,
    action:       (typeof ACTION !== 'undefined') ? [...ACTION] : null,
    broad:        (typeof BROAD !== 'undefined') ? [...BROAD] : null,
    syn:          (typeof SYN !== 'undefined') ? SYN : null,
    alias:        (typeof ALIAS !== 'undefined') ? ALIAS : null,
    chWords:      (typeof CH_WORDS !== 'undefined') ? CH_WORDS : null,
    segDict:      (typeof SEG_DICT !== 'undefined') ? [...SEG_DICT] : null,
    conceptBlock: (typeof CONCEPT_BLOCK !== 'undefined') ? CONCEPT_BLOCK : null,
    genericDown:  (typeof GENERIC_DOWN_TOPICS !== 'undefined') ? [...GENERIC_DOWN_TOPICS] : null,
    genericName:  (typeof GENERIC_NAME !== 'undefined') ? GENERIC_NAME.source : null,
    bigTableRe:   (typeof BIG_TABLE_RE !== 'undefined') ? BIG_TABLE_RE.source : null,
    topicRules:   (typeof TOPIC_RULES !== 'undefined') ? TOPIC_RULES : null
  },
  proj: (typeof STD_BUILD_PROJ !== 'undefined') ? STD_BUILD_PROJ : null
};

/* 把引擎本体挂到全局，供本文件的 --oracle 模式当"标准答案"用：
   C++ 移植得对不对，拿它逐条比（昨天只比 6 个用例，漏掉了表类条目与引导板块）。
   注意：页面里并没有一个叫 E 的对象（那个是 _build_regress.js 自己拼的），
   这里直接取引擎块里的裸名字；filtered() 返回的是对象，数组在 .arr 里。 */
;globalThis.__ENGINE = (typeof filtered === 'function')
  ? { filtered: filtered,
      setSection: (typeof setSection === 'function') ? setSection : null,
      qEl: (typeof qEl !== 'undefined') ? qEl : null }
  : null;

/* 打分明细：与桌面版 --explain 对齐，用来定位"哪条规则移植得不一样"。
   要在 filtered() 跑过之后调用（CUR_* 那组状态由它设好）。 */
;globalThis.__EXPL = (typeof scoreOf === 'function' && typeof evalEntry === 'function'
                      && typeof tokenize === 'function')
  ? function (q) {
      const w = tokenize(q);
      return function (f) {
        const e = evalEntry(f);
        return { n: f.n, tier: (e.covReq >= 0.999 ? 1 : 0),
                 firstPT: (e.hits && e.hits.length) ? e.hits[0].pT : -1,
                 cov: e.cov, covReq: e.covReq, score: scoreOf(f, w, e),
                 ord: f._ord, t: f._t || '(func)', src: f._src || '' };
      };
    } : null;

/* 主题归类：同样要从引擎块里挂出来（外层作用域看不到它） */
;globalThis.__MODKEY = (typeof modKeyOf === 'function') ? modKeyOf : null;`;

const src = files.map(f => fs.readFileSync(path.join(SRC_DIR, f), 'utf8')).join('\n;\n')
  + '\n;\n' + eng + '\n' + footer;
console.log('数据文件 ' + files.length + ' 个：' + files.join(', '));

new Function('document', 'window', 'localStorage', 'navigator', 'location', 'history',
             'setTimeout', 'clearTimeout', 'requestAnimationFrame', src)(
  doc, win, loc, nav, loc, hist, (f) => 0, () => { }, (f) => 0);

const D = globalThis.__DATA;
if (!D || !Array.isArray(D.all)) { console.error('FAIL 引擎跑完没拿到 ALL'); process.exit(1); }

/* ---------- --oracle：拿网页引擎当标准答案，逐条打印 ---------- */
const oi = process.argv.indexOf('--oracle');
if (oi >= 0) {
  const sec  = process.argv[oi + 1] || 'ref';
  const qs   = process.argv.slice(oi + 2);
  const eng  = globalThis.__ENGINE;
  if (!eng) { console.error('FAIL 引擎没挂上，没法当 oracle'); process.exit(1); }
  const modEl = doc.getElementById('mod');
  for (const q of qs) {
    if (eng.setSection) eng.setSection(sec);
    if (eng.qEl) eng.qEl.value = q;
    if (modEl) modEl.value = '';               /* 不筛模块，与桌面版 --search 同条件 */
    const r = eng.filtered();
    const arr = (r && r.arr) ? r.arr : r;
    const names = (arr || []).map(f => f.n);
    console.log('[' + sec + '] ' + q + ' | ' + names.length + ' | '
                + (names.length ? names[0] : '无') + ' | ' + names.slice(0, 5).join(' / '));
  }
  process.exit(0);
}

/* ---------- --explain：打分明细，和桌面版 --explain 逐行对 ---------- */
const ei = process.argv.indexOf('--explain');
if (ei >= 0) {
  const sec = process.argv[ei + 1] || 'ref';
  const q   = process.argv[ei + 2] || '';
  const eng = globalThis.__ENGINE;
  const mk  = globalThis.__EXPL;
  if (!eng || !mk) { console.error('FAIL 引擎或明细钩子没挂上'); process.exit(1); }
  if (eng.setSection) eng.setSection(sec);
  if (eng.qEl) eng.qEl.value = q;
  const modEl = doc.getElementById('mod');
  if (modEl) modEl.value = '';
  const r = eng.filtered();                       /* 先跑一遍，置好 CUR_* 状态 */
  const arr = (r && r.arr) ? r.arr : (r || []);
  const one = mk(q);
  console.log('词=' + q + '  板块=' + sec + '  结果数=' + arr.length);
  for (const f of arr.slice(0, 8)) {
    const x = one(f);
    console.log(x.n + ' | tier=' + x.tier + ' firstPT=' + x.firstPT
      + ' cov=' + Number(x.cov).toFixed(3) + ' req=' + Number(x.covReq).toFixed(3)
      + ' score=' + Number(x.score).toFixed(1) + ' ord=' + x.ord
      + ' | ' + x.t + '/' + x.src);
  }
  process.exit(0);
}

/* ---------- --topics：逐条打印主题归类，和桌面版 --topics 对（校 TOPIC_RULES 正则移植） ---------- */
const ti = process.argv.indexOf('--topics');
if (ti >= 0) {
  const mk = globalThis.__MODKEY;
  if (!mk) { console.error('FAIL modKeyOf 没挂上（它只存在于引擎块作用域内）'); process.exit(1); }
  for (const f of D.all)
    console.log(f.n + '\t' + mk(f));
  process.exit(0);
}

/* ---------- --baseline：按现有基线文件里的用例重跑一遍并回写 ----------
   为什么要这么绕：基线的用例清单（哪 28 个词、哪些板块）是手工维护的，
   每次改数据后手打一遍很容易漏项或打错字；而漏项是**静默**的——
   桌面 --selftest 只比对文件里有的行，少一行它就少比一行。
   所以这里直接从现有基线文件读用例，重跑后按同一格式回写。 */
const bi = process.argv.indexOf('--baseline');
if (bi >= 0) {
  const bp = path.resolve(__dirname, '..', 'data', 'search_baseline.txt');
  if (!fs.existsSync(bp)) { console.error('FAIL 找不到基线文件：' + bp); process.exit(1); }
  const cases = [];
  for (const raw of fs.readFileSync(bp, 'utf8').split(/\r?\n/)) {
    const t = raw.trim();
    if (!t || t.startsWith('#')) continue;
    const rb = t.indexOf(']');
    const bar = t.indexOf(' | ');
    if (rb < 0 || bar < rb) continue;
    cases.push({ sec: t.slice(1, rb), q: t.slice(rb + 1, bar).trim() });
  }
  if (!cases.length) { console.error('FAIL 基线文件里没解析出任何用例：' + bp); process.exit(1); }
  const eng = globalThis.__ENGINE;
  if (!eng) { console.error('FAIL 引擎没挂上，没法当 oracle'); process.exit(1); }
  const modEl = doc.getElementById('mod');
  const lines = [];
  for (const c of cases) {
    if (eng.setSection) eng.setSection(c.sec);
    if (eng.qEl) eng.qEl.value = c.q;
    if (modEl) modEl.value = '';
    const r = eng.filtered();
    const arr = (r && r.arr) ? r.arr : r;
    const names = (arr || []).map(f => f.n);
    lines.push('[' + c.sec + '] ' + c.q + ' | ' + names.length + ' | '
      + (names.length ? names[0] : '无') + ' | ' + names.slice(0, 5).join(' / '));
  }
  const hdr = [
    '# 搜索基线：由 tools/export_json.js --oracle 用**网页引擎**跑出来的标准答案。',
    '# 桌面版 --selftest 会逐行重算并比对（含前五顺序）。改了数据或引擎后请重新生成：',
    '#   node 工具箱/速查数据/tools/export_json.js --baseline    （按本文件现有用例重跑并回写）'
  ];
  fs.writeFileSync(bp, hdr.join('\n') + '\n' + lines.join('\n') + '\n', 'utf8');
  console.log('基线已回写：' + bp + '（用例 ' + lines.length + ' 个）');
  process.exit(0);
}

const all = D.all;
const kinds = {};
for (const f of all) {
  const k = (f._t || 'func') + (f._b ? '|手册:' + f._b : '');
  kinds[k] = (kinds[k] || 0) + 1;
}
/* JSON.stringify 会把 RegExp 序列化成空对象 {}，所以正则要显式转成 {__re, f} 再落盘。
   CONCEPT_BLOCK / TOPIC_RULES 里全是正则，不转就丢了。 */
function norm(v) {
  if (v instanceof RegExp) return { __re: v.source, f: v.flags };
  if (Array.isArray(v)) return v.map(norm);
  if (v && typeof v === 'object') {
    const o = {};
    for (const k of Object.keys(v)) o[k] = norm(v[k]);
    return o;
  }
  return v;
}

const packet = {
  meta: {
    generatedAt: new Date().toISOString(),
    sourceHtml: path.basename(HTML),
    dataFiles: files,
    total: all.length,
    kinds: kinds
  },
  all: all,
  search: norm(D.search),
  proj: norm(D.proj)
};

fs.mkdirSync(path.dirname(OUT), { recursive: true });
fs.writeFileSync(OUT, JSON.stringify(packet), 'utf8');
const mb = (fs.statSync(OUT).size / 1048576).toFixed(2);
console.log('条目总数 ' + all.length + '，写出 ' + OUT + '（' + mb + ' MB）');
console.log('分类：' + JSON.stringify(kinds));
console.log('搜索表：' + Object.keys(D.search).map(k => k + (D.search[k] ? '✓' : '✗')).join(' '));
console.log('项目引导工程数：' + (D.proj ? D.proj.length : 'null'));
