const assert = require('node:assert/strict');
const {test} = require('node:test');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../web/js/spruzzini.js'), 'utf8');
const html = fs.readFileSync(path.join(__dirname, '../web/index.html'), 'utf8');
const flush = async () => { for(let i=0;i<4;i++) await new Promise(setImmediate); };
function fixture() {
  const nodes = {};
  for(const [,id] of html.matchAll(/id="([^"]+)"/g)) nodes[id] = {disabled:false,textContent:'',className:'',value:'',checked:false,children:[],handlers:{},addEventListener(event,handler){this.handlers[event]=handler;},showModal(){this.open=true;}};
  nodes.sectors.children = Array.from({length:5},()=>{const strong={textContent:''}; return {querySelector(){return strong;},strong};});
  const state = {application:'nodemcu-spruzzini',version:'8.1.1',csrf_token:'boot-token',cycle_active:false,single_active:false,sector:0,remaining_seconds:0,configured:true,automatic:true,start_hour:6,minutes:10,smart:true,rain_hours_saved:12,outputs:[false,false,false,false,false],storage_ready:true,storage_error:'',clock_valid:true,clock:'30/10/2026 15:00:00',status:'A riposo',last_cycle:2026103006};
  const calls=[], timers=new Map(); let failure=false, nextTimer=1;
  const context = {document:{getElementById(id){return nodes[id];}},Date,Math,Object,Array,Error,URLSearchParams,AbortSignal,console,
    setTimeout(callback){let id=nextTimer++;timers.set(id,callback);return id;},clearTimeout(id){timers.delete(id);},
    async fetch(url,options){calls.push({url,options});if(failure) throw new Error('Connessione persa');return {ok:true,status:200,async json(){if(options.method==='POST')return {message:'Richiesta ricevuta'};return JSON.parse(JSON.stringify(state));}};}};
  vm.runInNewContext(source, context);
  return {nodes,state,calls,timers,offline(value){failure=value;},async refresh(){const callback=[...timers.values()].at(-1);timers.clear();callback();await flush();},submit(id){nodes[id].handlers.submit({preventDefault(){}});},click(id){nodes[id].handlers.click();}};
}
test('Shows actual output states, saved settings and the effective v8 rain window',async()=>{
  const f=fixture(); await flush();assert.equal(f.nodes.start.disabled,false);assert.equal(f.nodes.hour.value,6);assert.equal(f.nodes.rain.value,12);assert.equal(f.nodes.smart.textContent,'Attivo · 6h');
  f.state.cycle_active=true;f.state.sector=2;f.state.outputs[1]=true;f.state.remaining_seconds=61;await f.refresh();
  assert.equal(f.nodes.start.disabled,true);assert.equal(f.nodes.save.disabled,true);assert.equal(f.nodes.stop.disabled,false);assert.equal(f.nodes.sectors.children[1].strong.textContent,'Acceso');assert.equal(f.nodes.remaining.textContent,'1m 1s');
});
test('Manual start requires confirmation and sends only the selected legacy action with CSRF',async()=>{
  const f=fixture();await flush();f.nodes.selection.value='3';f.nodes.minutes.value='7';f.submit('manual');assert.equal(f.calls.length,1);assert.equal(f.nodes.confirm.open,true);
  f.nodes.confirm.returnValue='confirm';f.nodes.confirm.onclose();await flush();
  const call=f.calls.find(c=>c.options.method==='POST');assert.equal(call.url,'/api/spruzzini/command');assert.equal(call.options.headers['X-CSRF-Token'],'boot-token');assert.equal(new URLSearchParams(call.options.body).get('action'),'sector');assert.equal(new URLSearchParams(call.options.body).get('sector'),'3');
});
test('Cancelling confirmation sends nothing; stop does not wait for a dialog',async()=>{
  const f=fixture();await flush();f.nodes.selection.value='all';f.submit('manual');f.nodes.confirm.returnValue='cancel';f.nodes.confirm.onclose();await flush();assert.equal(f.calls.length,1);
  f.click('stop');await flush();assert.equal(new URLSearchParams(f.calls.find(c=>c.options.method==='POST').options.body).get('action'),'stop');
});
test('Disconnected state disables all controls; recovery restores them',async()=>{
  const f=fixture();await flush();f.offline(true);await f.refresh();for(const id of ['start','save','stop','reset'])assert.equal(f.nodes[id].disabled,true);
  f.offline(false);await f.refresh();assert.equal(f.nodes.start.disabled,false);assert.equal(f.nodes.status.textContent,'A riposo');
  assert.equal(f.nodes.feedback.textContent,'');
  f.state.storage_error='File danneggiato';await f.refresh();assert.equal(f.nodes.start.disabled,true);assert.equal(f.nodes.reset.disabled,false);assert.equal(f.nodes.save.disabled,false);
  f.state.storage_error='';f.state.application_ready=false;await f.refresh();assert.equal(f.nodes.start.disabled,true);
});
test('Settings form keeps unsaved edits while polling; reset requires confirmation',async()=>{
  const f=fixture();await flush();f.nodes.hour.value='8';await f.refresh();assert.equal(f.nodes.hour.value,'8');
  f.submit('settings');await flush();const body=new URLSearchParams(f.calls.find(c=>c.options.method==='POST').options.body);assert.equal(body.get('action'),'settings');assert.equal(body.get('hour'),'8');
  const count=f.calls.length;f.click('reset');assert.equal(f.calls.length,count);assert.equal(f.nodes.confirm.open,true);
});
