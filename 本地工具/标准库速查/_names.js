const fs = require('fs'), vm = require('vm'), path = require('path');
const files = ['data_std_lib', 'data_extra', 'data_full', 'data_tables', 'data_tables2', 'data_usage',
  'data_cn', 'data_demo', 'data_datasheet', 'data_flow', 'data_guide',
  'data_build', 'data_build_b1', 'data_build_b2', 'data_build_qt'];
const ctx = { console }; vm.createContext(ctx);
for (const f of files) { const p = path.join(__dirname, f + '.js'); if (fs.existsSync(p)) vm.runInContext(fs.readFileSync(p, 'utf8'), ctx, { filename: f + '.js' }); }
ctx.__o = {};
vm.runInContext(`__o.ALL=[].concat(STD_LIB,STD_EXTRA,STD_FULL,STD_TABLES,
  (typeof STD_TABLES2!=="undefined"?STD_TABLES2:[]),
  STD_FLOW,STD_GUIDE,STD_DATASHEET,STD_STRUCTS||[],
  (typeof STD_BUILD_GUIDE!=="undefined"?STD_BUILD_GUIDE:[]));`, ctx, { filename: 'c.js' });
const ns = [...new Set(ctx.__o.ALL.map(x => x && x.n).filter(Boolean))].sort();
fs.writeFileSync(path.join(__dirname, '_names.txt'), ns.join('\n'), 'utf8');
console.log('names:', ns.length);
