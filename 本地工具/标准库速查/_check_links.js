// 用真实的 data 文件（不靠我自己数正则）验证：CARD_REL 的每个 key 与目标，
// 都能在页面 ALL 列表里找到（找不到 = 点了没反应的死链）。
const fs = require('fs');
const vm = require('vm');
const path = require('path');
const HERE = __dirname;
const files = ['data_std_lib', 'data_extra', 'data_full', 'data_tables', 'data_tables2', 'data_usage',
  'data_cn', 'data_rel', 'data_demo', 'data_flow', 'data_guide', 'data_datasheet', 'data_doc',
  'data_build', 'data_build_b1', 'data_build_b2', 'data_build_qt'];
const ctx = { console };
vm.createContext(ctx);
for (const f of files) {
  const p = path.join(HERE, f + '.js');
  if (!fs.existsSync(p)) { console.log('缺失', f); continue; }
  vm.runInContext(fs.readFileSync(p, 'utf8'), ctx, { filename: f + '.js' });
}
// 注意：这些 data 文件用的是顶层 const，vm 的全局对象上看不到它们，
// 必须在同一个 context 里再 eval 一次名字把它们取出来。
ctx.__out = {};
vm.runInContext(`__out.ALL = [].concat(STD_LIB, STD_EXTRA, STD_FULL, STD_TABLES,
  (typeof STD_TABLES2 !== "undefined" ? STD_TABLES2 : []),
  STD_FLOW, STD_GUIDE, STD_DATASHEET, STD_STRUCTS || [],
  (typeof STD_BUILD_GUIDE !== "undefined" ? STD_BUILD_GUIDE : [])); __out.REL = CARD_REL;`,
  ctx, { filename: 'collect.js' });
const ALL = ctx.__out.ALL;
const names = new Set(ALL.map(x => x && x.n).filter(Boolean));
console.log('页面 ALL 条目数:', ALL.length, ' 去重名:', names.size);
const rel = ctx.__out.REL || {};
let keys = 0, pairs = 0, dead = [];
for (const k of Object.keys(rel)) {
  keys++;
  if (!names.has(k)) dead.push('key 不存在: ' + k);
  const arr = rel[k] || [];
  for (const pr of arr) {
    pairs++;
    if (!names.has(pr[0])) dead.push('目标不存在: ' + k + ' -> ' + pr[0]);
  }
}
console.log('CARD_REL key:', keys, ' 联动对:', pairs);
console.log('死链:', dead.length);
for (const d of dead) console.log('   ', d);
// 卡片覆盖
const flows = (ctx.__out.ALL || []).filter(x => x && x.t === 'flow').map(x => x.n);
const guides = (ctx.__out.ALL || []).filter(x => x && x.t === 'guide').map(x => x.n);
const missing = [...flows, ...guides].filter(n => !(n in rel));
console.log('卡片中还没有 CARD_REL key 的:', missing.length);
for (const m of missing) console.log('   ', m);
