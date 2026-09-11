(() => {
  'use strict';
  const escape = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const format = value => value == null || !Number.isFinite(Number(value)) ? '—' : Number(value).toLocaleString(undefined, {maximumFractionDigits: Math.abs(value) < 1 ? 4 : Math.abs(value) < 100 ? 3 : 1});
  const bytes = value => value == null ? '—' : value >= 1073741824 ? `${format(value / 1073741824)} GiB` : value >= 1048576 ? `${format(value / 1048576)} MiB` : `${format(value / 1024)} KiB`;
  const family = name => name.split('/')[0];
  const label = text => text.replace(/^20\d{6}[-/]?/, '').replace(/[-_]/g, ' ').replace(/\b(zen5|v2|gnr)\b/g, m => ({zen5:'Zen 5',v2:'V2',gnr:'GNR'}[m]));
  function csv(text) {
    const rows = []; let row = [], cell = '', quoted = false;
    text = text.replace(/^\uFEFF/, '');
    for (let i=0; i<text.length; i++) {
      const c=text[i];
      if (quoted) { if (c==='"' && text[i+1]==='"') {cell+='"';i++;} else if(c==='"') quoted=false; else cell+=c; }
      else if(c==='"') quoted=true;
      else if(c===',') {row.push(cell);cell='';}
      else if(c==='\n'||c==='\r') {if(c==='\r'&&text[i+1]==='\n')i++;row.push(cell);rows.push(row);row=[];cell='';}
      else cell+=c;
    }
    if(cell!==''||row.length){row.push(cell);rows.push(row);}
    return {fields:rows.shift()||[],rows};
  }
  function snapshotData(data) {
    return JSON.stringify(data).replace(/</g,'\\u003c').replace(/\u2028/g,'\\u2028').replace(/\u2029/g,'\\u2029');
  }
  function compareRuns(current, baseline, unit) {
    const key = r => JSON.stringify([r.case,r.input]);
    const old = new Map(), ambiguous = new Set();
    for (const r of baseline.measurements) {const k=key(r);if(old.has(k))ambiguous.add(k);old.set(k,r);}
    const out=[];
    for(const r of current.measurements){
      const b=old.get(key(r)), a=r.metrics.find(m=>m.unit===unit), m=b?.metrics.find(m=>m.unit===unit);
      if(ambiguous.has(key(r))||!a||!m||m.median<=0||a.median<=0)continue;
      out.push({case:r.case,control:`${baseline.label} · ${r.case}`,candidate:a,baseline:m,
        ratio:a.median/m.median,table:r.table,basis:JSON.stringify(r.counters)!==JSON.stringify(b.counters)?'Recorded workload counters differ':'Same case and unit across selected runs',input:r.input});
    }
    return out;
  }
  if (typeof module !== 'undefined' && module.exports) module.exports={csv,compareRuns,snapshotData,escape,format};
  if (typeof document === 'undefined') return;

  const $ = id => document.getElementById(id);
  const params = new URLSearchParams(location.hash.slice(1));
  const state={run:null,other:null,tab:params.get('tab')||'comparisons',q:params.get('q')||'',family:params.get('family')||'',sort:params.get('sort')||'case',unit:params.get('unit')||'',base:params.get('base')||'',page:0,table:params.get('table')||''};
  const pageSize=60, cache=new Map(); let catalog, generation=0, shown=[];
  function download(text,name,type='application/json') {const url=URL.createObjectURL(new Blob([text],{type}));const a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);}
  function syncHash(){
    const p=new URLSearchParams({run:state.run.id,tab:state.tab});
    for(const k of ['q','family','sort','unit','base','table'])if(state[k])p.set(k,state[k]);
    history.replaceState(null,'','#'+p.toString());
  }
  async function getRun(id){
    if(window.SIXDB_OFFLINE?.run.id===id)return window.SIXDB_OFFLINE.run;
    if(!cache.has(id)){
      const entry=catalog.runs.find(r=>r.id===id);if(!entry)throw Error('The selected run is not in this snapshot.');
      cache.set(id,fetch(entry.data).then(r=>{if(!r.ok)throw Error('Could not load retained evidence.');return r.json();}));
    }
    return cache.get(id);
  }
  function fill(select,options,value){select.innerHTML=options.map(([v,t])=>`<option value="${escape(v)}">${escape(t)}</option>`).join('');select.value=options.some(([v])=>v===value)?value:options[0]?.[0]||'';}
  function renderRuns(){
    const study=$('study').value, q=$('run-search').value.toLowerCase();
    const runs=catalog.runs.filter(r=>(!study||r.study===study)&&`${r.label} ${r.machine} ${r.study}`.toLowerCase().includes(q));
    $('run-count').textContent=`${runs.length} retained run${runs.length===1?'':'s'}`;
    $('runs').innerHTML=runs.map(r=>`<button class="run-option" aria-current="${state.run?.id===r.id}" data-run="${escape(r.id)}"><span class="study-name">${escape(r.study)}</span><strong>${escape(label(r.label))}</strong><small>${escape(r.machine)} · ${r.comparisons?`${r.comparisons} comparisons`:r.measurements?`${r.measurements} measurements`:`${r.tables} tables`}</small></button>`).join('')||'<div class="sidebar-controls">No matching runs.</div>';
    $('runs').querySelectorAll('[data-run]').forEach(b=>b.onclick=()=>selectRun(b.dataset.run));
  }
  async function selectRun(id,initial=false){
    const token=++generation;$('status').hidden=false;$('status').textContent='Loading retained evidence…';
    try{
      const run=await getRun(id);if(token!==generation)return;state.run=run;state.page=0;
      if(!initial){state.q='';state.family='';state.unit='';state.base='';state.table='';state.other=null;}
      if(!['comparisons','measurements','build','tables','context'].includes(state.tab))state.tab='comparisons';
      if(state.tab==='comparisons'&&!run.comparisons.length&&!state.base)state.tab=run.measurements.length?'measurements':run.build&&Object.keys(run.build).length?'build':'tables';
      if(state.base){try{state.other=await getRun(state.base);}catch{state.base='';state.other=null;}}
      if(token!==generation)return;
      $('breadcrumbs').textContent=run.study+' / '+run.label;
      $('run-title').textContent=label(run.label);
      const integrity={verified:'Retained hashes match',snapshot:'Snapshot hashes only',mismatch:'Retained hash mismatch'};
      $('integrity').textContent=integrity[run.integrity.status];$('integrity').className='integrity '+run.integrity.status;
      const conf=run.context.configuration;
      $('conditions').innerHTML=`<span class="condition">${escape(run.machine)}</span>${run.profile?`<span class="tag">${escape(run.profile)}</span>`:''}${conf.actual_capacity||conf.capacity?`<span class="tag">${escape(conf.actual_capacity||conf.capacity)}</span>`:''}<span class="condition mono">${escape((run.context.recorded.source?.digest||run.context.recorded.source_digest||run.context.run_facts.source_digest||'Source identity in context').slice(0,20))}</span>`;
      $('tab-comparisons').querySelector('span').textContent=run.comparisons.length||'';
      $('tab-measurements').querySelector('span').textContent=run.measurements.length||'';
      $('tab-tables').querySelector('span').textContent=run.files.length;
      $('run-view').hidden=false;$('status').hidden=true;$('export').disabled=!!window.SIXDB_OFFLINE;
      if(window.SIXDB_OFFLINE)$('export').textContent='Portable snapshot';
      renderRuns();render();
    }catch(error){$('status').textContent=error.message;$('run-view').hidden=true;}
  }
  function metrics(){return [...new Set(state.run.measurements.flatMap(r=>r.metrics.map(m=>m.unit)))];}
  function rowsForView(){
    if(state.tab==='comparisons')return state.other?compareRuns(state.run,state.other,state.unit):state.run.comparisons;
    return state.run.measurements.map(r=>({...r,candidate:r.metrics.find(m=>m.unit===state.unit)})).filter(r=>r.candidate);
  }
  function controls(){
    const tab=state.tab, hasCases=['comparisons','measurements'].includes(tab);
    $('controls').hidden=!hasCases&&tab!=='tables';
    $('family-label').hidden=!hasCases;$('sort-label').hidden=!hasCases;
    $('baseline-label').hidden=tab!=='comparisons';$('table-label').hidden=tab!=='tables';
    $('unit-label').hidden=!(tab==='measurements'||(tab==='comparisons'&&state.other));
    $('case-search').value=state.q;
    fill($('unit'),metrics().map(u=>[u,u]),state.unit);state.unit=$('unit').value;
    const fs=[...new Set(rowsForView().map(r=>family(r.case)))].sort();
    fill($('family'),[['','All families'],...fs.map(f=>[f,f])],state.family);state.family=$('family').value;
    $('sort').value=state.sort;
    fill($('baseline'),[['','Recorded controls'],...catalog.runs.filter(r=>r.id!==state.run.id&&r.study===state.run.study&&r.measurements).map(r=>[r.id,`${label(r.label)} · ${r.machine}`])],state.base);
    fill($('table'),state.run.files.map(f=>[f.name,`${f.name} · ${f.rows.toLocaleString()} rows`]),state.table);state.table=$('table').value;
  }
  function paginate(rows,renderer){
    const pages=Math.max(1,Math.ceil(rows.length/pageSize));state.page=Math.min(state.page,pages-1);
    const start=state.page*pageSize;shown=rows.slice(start,start+pageSize);$('content').innerHTML=renderer(shown);
    $('pagination').innerHTML=`<span>${rows.length?`${(start+1).toLocaleString()}–${Math.min(start+pageSize,rows.length).toLocaleString()} of ${rows.length.toLocaleString()}`:'0'} records</span><div class="page-buttons"><button id="previous" ${state.page===0?'disabled':''}>Previous</button><button id="next" ${state.page>=pages-1?'disabled':''}>Next</button></div>`;
    $('previous').onclick=()=>{state.page--;renderContent();};$('next').onclick=()=>{state.page++;renderContent();};
  }
  function ratioMark(ratio){
    const direction=ratio<1?'fast':'slow', amount=Math.min(50,Math.abs(Math.log2(ratio))*25);
    return `<div class="ratio-value"><span class="${direction}">${format(ratio)}×</span><small>${ratio<1?`${format((1-ratio)*100)}% less time`:`${format((ratio-1)*100)}% more time`}</small></div><div class="ratio-track" aria-hidden="true"><i class="ratio-mark ${direction}" style="left:${ratio<1?50-amount:50}%;width:${amount}%"></i></div>`;
  }
  function renderCases(){
    const paired=state.tab==='comparisons';
    let rows=rowsForView().filter(r=>(!state.family||family(r.case)===state.family)&&`${r.case} ${r.control||''} ${r.input||''}`.toLowerCase().includes(state.q.toLowerCase()));
    rows.sort((a,b)=>state.sort==='slower'?(b.ratio??0)-(a.ratio??0):state.sort==='faster'?(a.ratio??0)-(b.ratio??0):state.sort==='cost'?(b.candidate?.median??0)-(a.candidate?.median??0):a.case.localeCompare(b.case));
    $('view-note').innerHTML=paired?(state.other?`<strong>Current / ${escape(label(state.other.label))}.</strong> Same case names and selected units; inspect both runs’ context before attributing a difference.${state.other.machine!==state.run.machine?' Hardware differs.':''}${state.other.profile!==state.run.profile?' Feature profiles differ.':''}`:'<strong>Candidate / recorded control.</strong> Lower is faster. Controls are chosen by the study; ranges are observed repetitions, not confidence intervals. Click a case to inspect it.'):`<strong>${escape(state.unit||'No recognized measurement unit')}.</strong> Raw repetitions and counters are available per case. An item’s meaning belongs to the workload; no cross-family normalization is applied.`;
    if(!rows.length){$('content').innerHTML=`<div class="empty"><strong>${state.q||state.family?'No matching cases':'No recognized '+(paired?'comparisons':'measurements')+' in this view'}</strong>${paired?'Select another run for comparison, or open Measurements or Tables.':'The original records are still available under Tables.'}</div>`;$('pagination').innerHTML='';return;}
    paginate(rows,items=>`<div class="table-wrap"><table><thead><tr><th>Case${paired?' / control':''}</th>${paired?'<th class="num">Control</th>':''}<th class="num">${paired?'Candidate':'Median'}</th><th>${paired?'Time ratio <span class="ratio-legend"><span>¼×</span><span>1×</span><span>4×</span></span>':'Observed range'}</th></tr></thead><tbody>${items.map((r,i)=>`<tr class="inspect" tabindex="0" data-detail="${i}" aria-label="Inspect ${escape(r.case)}"><td class="case"><span class="case-name">${escape(r.case)}</span><span class="control-name">${escape(paired?r.control:r.input||r.table)}</span></td>${paired?`<td class="num">${format(r.baseline?.median)}<div class="secondary">${escape(r.baseline?.unit||r.candidate?.unit||'')}</div></td>`:''}<td class="num">${format(r.candidate?.median)}<div class="secondary">${escape(r.candidate?.unit||'')}</div></td><td class="${paired?'ratio-cell':'num'}">${paired?ratioMark(r.ratio):`${format(r.candidate?.min)}–${format(r.candidate?.max)}<div class="secondary">${r.candidate?.samples.length||0} retained repetitions</div>`}</td></tr>`).join('')}</tbody></table></div>`);
    $('content').querySelectorAll('[data-detail]').forEach(tr=>{const open=()=>openDetail(shown[Number(tr.dataset.detail)]);tr.onclick=open;tr.onkeydown=e=>{if(e.key==='Enter'||e.key===' '){e.preventDefault();open();}};});
  }
  function renderTables(){
    const file=state.run.files.find(f=>f.name===state.table);
    if(!file){$('content').innerHTML='<div class="empty">No retained CSV tables in this run.</div>';return;}
    const parsed=csv(file.text), q=state.q.toLowerCase(), rows=parsed.rows.filter(r=>!q||r.some(v=>v.toLowerCase().includes(q)));
    $('view-note').innerHTML=`<div class="source-line"><span>${file.rows.toLocaleString()} original rows · ${file.columns} columns · ${bytes(file.bytes)}</span><button id="download-table">↓ Original CSV</button></div><code>SHA-256 ${escape(file.sha256)}</code>`;
    $('download-table').onclick=()=>download(file.text,file.name,'text/csv;charset=utf-8');
    paginate(rows,items=>`<div class="table-wrap"><table class="raw-table"><thead><tr>${parsed.fields.map(f=>`<th>${escape(f)}</th>`).join('')}</tr></thead><tbody>${items.map(r=>`<tr>${r.map(v=>`<td>${escape(v)}</td>`).join('')}</tr>`).join('')}</tbody></table></div>`);
  }
  function renderBuild(){
    const b=state.run.build, build=b['build-summary'], cost=b['compile-cost'], sizes=b['code-size'];
    if(!Object.keys(b).length){$('content').innerHTML='<div class="empty"><strong>Build costs weren’t retained here.</strong>This is separate from missing or failed measurements.</div>';return;}
    $('view-note').textContent='Build and compile measurements are separate from runtime results. Process-max RSS is not aggregate memory; debug-heavy checks and benchmark binaries are not deployment-size estimates.';
    let html='';
    if(build){const duration=build.build?.elapsed_seconds??build.build?.seconds??build.build?.wall_seconds, rss=build.build?.max_process_rss_kib;html+=`<div class="stats"><div class="stat"><span>Build command</span><strong>${format(duration)} s</strong></div><div class="stat"><span>Max process RSS</span><strong>${bytes(rss==null?null:Number(rss)*1024)}</strong></div><div class="stat"><span>Completed Ninja edges</span><strong>${build.completed_edges?.length??'—'}</strong></div></div><p class="view-note">${escape(build.build_state||'Fresh/incremental scope not recorded')}. ${escape(build.limits||'')}</p>`;}
    if(cost){const max=Math.max(...cost.compiles.map(r=>r.wall_seconds));html+=`<h2>Isolated serial compilations</h2><p class="view-note">${format(cost.serial_compile_wall_seconds)} s across these selected TUs. Linking and helper bookkeeping are excluded.</p><div class="table-wrap"><table><thead><tr><th>Translation unit</th><th class="num">Wall time</th><th class="num">Process-max RSS</th><th>Status</th></tr></thead><tbody>${cost.compiles.map(r=>`<tr><td class="case mono">${escape(r.source.replace(/^.*\/(ikea2\/|ikea\/)/,'$1'))}<div class="bar-cost"><i style="width:${max?r.wall_seconds/max*100:0}%"></i></div></td><td class="num">${format(r.wall_seconds)} s</td><td class="num">${bytes(r.max_process_rss_kib*1024)}</td><td>${escape(r.status)}</td></tr>`).join('')}</tbody></table></div>`;}
    if(Array.isArray(sizes)){html+=`<h2>Code and debug information</h2><div class="table-wrap"><table><thead><tr><th>Artifact</th><th class="num">Code</th><th class="num">Read-only data</th><th class="num">Debug</th><th class="num">Stripped file</th></tr></thead><tbody>${sizes.map(r=>`<tr><td class="case mono">${escape(r.artifact)}</td><td class="num">${bytes(r.section_bytes?.code)}</td><td class="num">${bytes(r.section_bytes?.read_only_data)}</td><td class="num">${bytes(r.section_bytes?.debug)}</td><td class="num">${bytes(r.stripped_file_bytes)}</td></tr>`).join('')}</tbody></table></div>`;}
    if(build?.completed_edges?.length){html+=`<h2>Longest completed build edges</h2><div class="table-wrap"><table><thead><tr><th>Output</th><th class="num">Edge duration</th></tr></thead><tbody>${[...build.completed_edges].sort((a,b)=>b.seconds-a.seconds).slice(0,15).map(r=>`<tr><td class="case mono">${escape(r.output)}</td><td class="num">${format(r.seconds)} s</td></tr>`).join('')}</tbody></table></div><p class="view-note">Showing up to 15 edges. Overlapping durations do not sum to elapsed build time; failed edges have no completed duration.</p>`;}
    html+=`<details class="context-block"><summary>All retained build records</summary><pre>${escape(JSON.stringify(b,null,2))}</pre></details>`;$('content').innerHTML=html;
  }
  function renderContext(){
    const run=state.run;
    $('view-note').textContent='Snapshot integrity describes retained bytes, not experimental validity or production readiness.';
    let html=`<div class="context-block"><h2>Evidence identity</h2><p class="mono">${escape(run.id)}</p><p>${escape($('integrity').textContent)} · ${run.integrity.checked_members} recorded member hashes checked.</p>${run.integrity.failures.length?`<pre class="error">${escape(JSON.stringify(run.integrity.failures,null,2))}</pre>`:''}<p class="secondary">Original provenance SHA-256</p><code>${escape(run.integrity.provenance_sha256||'Not retained')}</code></div>`;
    if(run.notes)html+=`<div class="context-block"><h2>Study’s retained note</h2><div class="research-note">${escape(run.notes.replace(/^# .*\n/,''))}</div></div>`;
    html+=`<div class="context-block"><h2>Measurement conditions</h2><pre>${escape(JSON.stringify(run.context,null,2))}</pre></div>`;
    if(state.other)html+=`<details class="context-block"><summary>Comparison run: ${escape(state.other.label)}</summary><pre>${escape(JSON.stringify(state.other.context,null,2))}</pre></details>`;
    html+=`<div class="context-block"><h2>Recover the full run</h2>${run.artifact?.sha256?`<p>The retained artifact identifies the full bundle. Recovery uses the repository’s artifact helper and authorized S3 access.</p><button id="download-artifact">↓ Artifact reference</button><pre>${escape(JSON.stringify(run.artifact,null,2))}</pre>`:'<p>No artifact reference was retained beside this evidence.</p>'}</div><div class="context-block"><h2>Files in this snapshot</h2><div class="table-wrap"><table><thead><tr><th>Original table</th><th class="num">Rows</th><th class="num">Size</th></tr></thead><tbody>${run.files.map(f=>`<tr><td><button class="download-link" data-file="${escape(f.name)}">${escape(f.name)}</button></td><td class="num">${f.rows.toLocaleString()}</td><td class="num">${bytes(f.bytes)}</td></tr>`).join('')}</tbody></table></div></div>`;
    if(catalog.errors.length)html+=`<details class="context-block"><summary>${catalog.errors.length} directories could not be exported</summary><pre>${escape(JSON.stringify(catalog.errors,null,2))}</pre></details>`;
    $('content').innerHTML=html;
    if($('download-artifact'))$('download-artifact').onclick=()=>download(JSON.stringify(run.artifact,null,2),'artifact.json');
    $('content').querySelectorAll('[data-file]').forEach(b=>b.onclick=()=>{state.tab='tables';state.table=b.dataset.file;state.q='';state.page=0;render();});
  }
  function renderContent(){
    $('pagination').innerHTML='';$('view-note').textContent='';
    if(state.tab==='comparisons'||state.tab==='measurements')renderCases();
    else if(state.tab==='tables')renderTables();else if(state.tab==='build')renderBuild();else renderContext();
    syncHash();
  }
  function render(){
    document.querySelectorAll('[data-tab]').forEach(b=>{b.setAttribute('aria-selected',String(b.dataset.tab===state.tab));b.tabIndex=b.dataset.tab===state.tab?0:-1;});
    $('panel').setAttribute('aria-labelledby','tab-'+state.tab);controls();renderContent();
  }
  function openDetail(row){
    $('detail-title').textContent='Retained case';
    const groups=[...(row.baseline?[['Control',row.baseline]]:[]),['Candidate',row.candidate]].filter(([,m])=>m);
    const all=groups.flatMap(([,m])=>[m.min,m.max,...m.samples]).filter(Number.isFinite);
    const lo=Math.min(...all),hi=Math.max(...all),spread=(hi-lo)||Math.abs(hi)*.1||1,axisLo=lo-spread*.08,axisHi=hi+spread*.08;
    const x=n=>100+(n-axisLo)/(axisHi-axisLo)*580;
    const dots=groups.map(([name,m],j)=>{const y=44+j*57;return `<text x="0" y="${y+4}">${name}</text><line x1="${x(m.min)}" y1="${y}" x2="${x(m.max)}" y2="${y}" stroke-width="3"/>${(m.samples.length?m.samples:[m.median]).map((n,i)=>`<circle class="${j===0&&row.baseline?'baseline-sample':'sample'}" cx="${x(n)}" cy="${y+((i%3)-1)*5}" r="4" tabindex="0"><title>${name}, ${m.samples.length?'retained repetition '+(i+1):'median only'}: ${format(n)} ${escape(m.unit)}</title></circle>`).join('')}`;}).join('');
    $('detail-content').innerHTML=`<p class="detail-name">${escape(row.case)}</p>${row.control?`<p class="secondary">Control: <span class="mono">${escape(row.control)}</span></p>`:''}<div class="detail-metrics">${groups.map(([name,m])=>`<div><div class="secondary">${name} median</div><div class="value">${format(m.median)} <small>${escape(m.unit)}</small></div><p>${format(m.min)}–${format(m.max)} observed range · ${m.samples.length} retained repetitions</p></div>`).join('')}</div><svg class="samples-chart" viewBox="0 0 700 145" role="img" aria-label="Retained repetition values and observed ranges"><title>Individual retained repetitions</title>${dots}<line x1="100" y1="124" x2="680" y2="124"/>${[axisLo,(axisLo+axisHi)/2,axisHi].map(n=>`<text x="${x(n)}" y="142" text-anchor="middle">${format(n)}</text>`).join('')}</svg><p class="view-note">Observed samples and extrema, not confidence intervals. Where repetitions are absent, the mark shows only the retained median. ${escape(row.basis||'')}</p><div class="samples-list">${groups.map(([name,m])=>`<div><h3>${name} repetitions</h3><pre>${m.samples.length?m.samples.map((v,i)=>`${i+1}  ${format(v)}`).join('\n'):'Not retained in this table.'}</pre></div>`).join('')}</div>${row.counters?`<h3>Recorded workload counters</h3><pre>${escape(JSON.stringify(row.counters,null,2))}</pre>`:''}<p class="secondary">Source table: ${escape(row.table)}</p><button id="detail-table">Open source table</button>`;
    $('detail-table').onclick=()=>{$('detail').close();state.tab='tables';state.table=row.table;state.q=row.case;state.page=0;render();};
    $('detail').showModal();
  }
  async function exportRun(){
    const button=$('export');button.disabled=true;button.textContent='Preparing snapshot…';
    try{
      const response=await fetch('portable-template.html');if(!response.ok)throw Error('Portable template could not be loaded.');
      const data={catalog:{...catalog,runs:catalog.runs.filter(r=>r.id===state.run.id),errors:[]},run:state.run};
      const encoded=snapshotData(data);
      const html=(await response.text()).replace('/*__SIXDB_SNAPSHOT__*/','window.SIXDB_OFFLINE='+encoded+';');
      download(html,'sixdb-'+state.run.label.replace(/[^a-z0-9-]/gi,'-')+'.html','text/html;charset=utf-8');
    }catch(error){$('view-note').textContent=error.message;}finally{button.disabled=false;button.textContent='↓ Export this run';}
  }
  async function init(){
    try{
      catalog=window.SIXDB_OFFLINE?.catalog||await fetch('catalog.json').then(r=>{if(!r.ok)throw Error('Result catalog unavailable. Regenerate this snapshot.');return r.json();});
      $('snapshot').textContent=`${catalog.runs.length} runs · snapshot ${new Date(catalog.generated).toLocaleDateString(undefined,{day:'numeric',month:'short',year:'numeric'})}`;
      fill($('study'),[['','All investigations'],...[...new Set(catalog.runs.map(r=>r.study))].sort().map(s=>[s,s])],'');
      $('study').onchange=renderRuns;$('run-search').oninput=renderRuns;
      $('case-search').oninput=()=>{state.q=$('case-search').value;state.page=0;renderContent();};
      for(const key of ['family','sort','unit','table'])$(key).onchange=()=>{state[key]=$(key).value;state.page=0;renderContent();};
      $('baseline').onchange=async()=>{const id=$('baseline').value, token=generation;state.base=id;state.page=0;try{const other=id?await getRun(id):null;if(token!==generation||id!==state.base)return;state.other=other;render();}catch(error){if(token===generation&&id===state.base){state.base='';state.other=null;render();$('view-note').textContent=error.message;}}};
      document.querySelectorAll('[data-tab]').forEach(b=>{b.onclick=()=>{state.tab=b.dataset.tab;state.page=0;render();};b.onkeydown=e=>{if(!['ArrowLeft','ArrowRight','Home','End'].includes(e.key))return;e.preventDefault();const tabs=[...document.querySelectorAll('[data-tab]')],i=tabs.indexOf(b);const n=e.key==='Home'?0:e.key==='End'?tabs.length-1:(i+(e.key==='ArrowRight'?1:-1)+tabs.length)%tabs.length;tabs[n].click();tabs[n].focus();};});
      $('close-detail').onclick=()=>$('detail').close();$('export').onclick=exportRun;
      const first=catalog.runs.find(r=>r.id===params.get('run'))||catalog.runs.find(r=>r.comparisons)||catalog.runs[0];
      if(!first){$('status').textContent='No retained evidence was found. Add an evidence directory or broaden the export selection.';return;}
      await selectRun(first.id,true);
    }catch(error){$('status').textContent=error.message;}
  }
  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',init);else init();
})();
