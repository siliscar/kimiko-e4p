/* SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 执行实际网页脚本，验证工作台与设备接口。/ Exercise the real workspace script and device API contracts.
 */
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync('components/wifi_transfer/upload.html','utf8');
const source=html.match(/<script>([\s\S]*?)<\/script>/)[1];
const flush=()=>new Promise(resolve=>setImmediate(resolve));
class Element {
  constructor(tag='div') {
    this.tagName=tag;this.children=[];this.dataset={};this.attributes={};this.value='';this.files=[];this.style={};this.className='';
    this.classList={toggle:(name,on)=>{const set=new Set(this.className.split(' ').filter(Boolean));if(on===undefined)on=!set.has(name);on?set.add(name):set.delete(name);this.className=[...set].join(' ');},add:name=>this.classList.toggle(name,true),remove:name=>this.classList.toggle(name,false)};
  }
  set textContent(value){this.text=String(value);this.children=[];}
  get textContent(){return (this.text||'')+this.children.map(c=>c.textContent).join('');}
  set innerHTML(_){throw Error('Never parse file names as HTML');}
  append(...children){this.children.push(...children);}
  replaceChildren(...children){this.text='';this.children=children;}
  setAttribute(key,value){this.attributes[key]=String(value);}
  removeAttribute(key){delete this.attributes[key];}
  get firstElementChild(){return this.children[0];}
  showModal(){this.open=true;}
  close(){this.open=false;}
  focus(){}
  click(){return this.onclick?.();}
  getContext(){return {setTransform(){},fillRect(){},drawImage(){},fillText(){}};}
  getBoundingClientRect(){return {width:260,right:600,bottom:200};}
  setPointerCapture(){}
  toBlob(resolve){assert.equal(this.width,684);assert.equal(this.height,1216);resolve({size:120000});}
}
async function setup(options={}) {
  const nodes=new Map(),all=[],responses=options.responses||[],confirmations=options.confirmations||[true];
  const create=tag=>{const el=new Element(tag);all.push(el);return el;};
  for(const match of html.matchAll(/<([a-z][a-z0-9-]*)\b([^>]*)>/gi)){
    const attrs=match[2],id=attrs.match(/\bid="([^"]+)"/),view=attrs.match(/\bdata-view="([^"]+)"/),folder=attrs.match(/\bdata-folder="([^"]*)"/);
    if(!id&&!view&&!folder)continue;
    const el=create(match[1]);el.className=attrs.match(/\bclass="([^"]+)"/)?.[1]||'';el.hidden=/\bhidden\b/.test(attrs);
    if(id){el.id=id[1];nodes.set(el.id,el);}if(view)el.dataset.view=view[1];if(folder)el.dataset.folder=folder[1];
  }
  const document={
    getElementById(id){assert.ok(nodes.has(id),'Missing HTML id '+id);return nodes.get(id);},
    createElement:create,createElementNS:(_,tag)=>create(tag),addEventListener(){},
    querySelectorAll(selector){
      if(selector==='[data-view]')return all.filter(el=>'view' in el.dataset);
      if(selector==='[data-folder]')return all.filter(el=>'folder' in el.dataset);
      if(selector==='.view')return all.filter(el=>el.className.split(' ').includes('view'));
      if(selector==='[data-file-action]')return all.filter(el=>'data-file-action' in el.attributes);
      return all.filter(el=>el.tagName==='button'||el.tagName==='input');
    }
  };
  const state={gets:[],mutations:[],sent:[],signature:'慢慢阅读',signatures:[],infoReads:0};
  const json=(body,status=200)=>({status,json:async()=>body});
  const fetch=async(url,request={})=>{
    if(url==='/signature'){
      if(options.signatureError)return json({error:'签名读取失败'},500);
      if(request.method==='POST'){state.signature=request.body;state.signatures.push(request.body);}
      return json({signature:state.signature});
    }
    if(url==='/info'){
      ++state.infoReads;
      if(options.infoError||(options.infoErrorAfter&&state.infoReads>=options.infoErrorAfter))throw Error('offline');
      return json({mode:options.mode||'ap',is_flash:!!options.isFlash,free_bytes:options.freeBytes??100000000,file_limit:options.fileLimit||0});
    }
    if(url.startsWith('/files?')){
      state.gets.push(url);const query=new URL('http://pico'+url).searchParams,path=query.get('path')||'',page=Number(query.get('page'));
      if(options.directory)return options.directory(path,page,json);
      const items=path?[{name:'封面.jpg',size:1200000}]:[{name:'books',directory:true,size:0},{name:'readme.txt',size:12}];
      return json({path,total:items.length,pages:1,items});
    }
    if(url==='/files'&&request.method==='POST'){
      state.mutations.push(JSON.parse(request.body));const response=options.mutations?.shift();return json(response?.body||{ok:true},response?.status||200);
    }
    throw Error('Unexpected fetch '+url);
  };
  class XHR{
    constructor(){this.upload={};}
    open(method,url){assert.equal(method,'PUT');this.url=url;}
    abort(){this.onabort();}
    send(file){
      state.sent.push({url:this.url,file});this.upload.onprogress({loaded:file.size,total:file.size});
      const response=responses.shift()||{status:200,body:{ok:true}};
      queueMicrotask(()=>{if(response.network)return this.onerror();this.status=response.status;this.responseText=response.invalidJson?'invalid':JSON.stringify(response.body);this.onload();});
    }
  }
  class ImageMock{naturalWidth=1200;naturalHeight=1600;set src(value){this.url=value;queueMicrotask(()=>this.onload());}}
  const context=vm.createContext({document,fetch,XMLHttpRequest:XHR,TextEncoder,AbortController,setTimeout,clearTimeout,
    location:{host:'192.168.4.1',hash:''},innerWidth:1440,innerHeight:900,window:{addEventListener(){},scrollTo(){}},Image:ImageMock,
    confirm:()=>confirmations.length?confirmations.shift():true,URL:{createObjectURL:()=> 'blob:test',revokeObjectURL(){}},console});
  // 查询参数使用标准 URL；页面内仅需要对象 URL 接口。/ Parse queries with standard URL; the page needs only object URLs.
  vm.runInContext(source,context);await flush();await flush();
  return {state,context,get:id=>nodes.get(id),run:code=>vm.runInContext(code,context)};
}
const file=(name,size=1200000)=>({name,size});
async function edit(t,action,name){
  t.context.openEditor(action,t.run('directoryItems[0]'));if(name!==undefined)t.get('editorName').value=name;await t.context.submitEditor();
}
(async()=>{
  for(const root of ['main','components'])for(const relative of fs.readdirSync(root,{recursive:true})){
    const path=root+'/'+relative;
    if(!/\.(?:c|h|cpp|html|json|md|txt)$/.test(path)||!fs.statSync(path).isFile())continue;
    let text=fs.readFileSync(path,'utf8').replaceAll('LicenseRef-Kiikoread-Personal-Study-1.0','');
    if(path==='main/settings/settings.c')text=text.replace('strcmp(name, "kiikoread")','');
    assert.doesNotMatch(text,/kiikoread/i,'Unexpected old firmware brand in '+path);
  }
  const board=fs.readFileSync('components/board_support/include/board_hardware.h','utf8');
  for(const name of ['BOARD_PRODUCT_NAME','BOARD_DEVICE_NAME'])assert.match(board,new RegExp('#define '+name+' "kimiko"'));
  const transferHeader=fs.readFileSync('components/wifi_transfer/include/wifi_transfer.h','utf8');
  const password=transferHeader.match(/#define WIFI_TRANSFER_PASSWORD "([^"]+)"/)[1];
  assert.equal(password,'kimiko1234');assert.ok(password.length>=8&&password.length<=63);
  const transfer=fs.readFileSync('components/wifi_transfer/wifi_transfer.c','utf8');
  assert.match(transfer,/"kimiko-%02X%02X", mac\[4\], mac\[5\]/);
  assert.match(transfer,/strcpy\(\(char \*\)wifi\.ap\.password, WIFI_TRANSFER_PASSWORD\)/);
  assert.match(html,/<title>kimiko · WiFi 传书<\/title>/);
  assert.match(html,/<span>kimiko<span class="brand-note">WiFi 传书/);
  assert.doesNotMatch(html,/kiikoread/i);
  assert.match(html,/浏览 TF 卡/);assert.doesNotMatch(html,/图片对比|编辑书架名称/);
  assert.doesNotMatch(html,/(?:src|href)="https?:/);
  let t=await setup();
  assert.match(t.get('storage').textContent,/TF 卡已就绪/);assert.equal(t.get('entries').children.length,2);
  t.get('entries').children[0].children[0].children[2].onclick();await flush();await flush();
  assert.equal(t.get('path').textContent,'/books');assert.match(t.state.gets.at(-1),/path=books/);
  await edit(t,'rename','新封面.jpg');
  t.get('newFolder').onclick();t.get('editorName').value='新目录';await t.context.submitEditor();
  await edit(t,'copy','封面副本.jpg');await edit(t,'delete');
  assert.deepEqual(t.state.mutations,[
    {action:'rename',path:'books/封面.jpg',target:'books/新封面.jpg'},
    {action:'mkdir',path:'books',target:'books/新目录'},
    {action:'copy',path:'books/封面.jpg',target:'books/封面副本.jpg'},
    {action:'delete',path:'books/封面.jpg'}]);
  assert.ok(t.get('filePicker').accept.split(',').includes('.mobi'));
  for(const [kind,name,endpoint]of[['book','海边的信.epub','/upload'],['book','测试书.MOBI','/upload'],['font','思源宋体.otf','/font-upload'],['picture','封面.jpeg','/image-upload']]){
    await t.context.sendFiles([file(name)],kind,{book:'books',font:'fonts',picture:'pictures'}[kind]);
    assert.ok(t.state.sent.at(-1).url.startsWith(endpoint+'?name='));assert.match(t.get('uploadResult').textContent,/已保存 1 个文件/);
  }
  const longMobi='字'.repeat(70)+'Book.mobi';
  await t.context.sendFiles([file(longMobi)],'file','books');
  assert.equal(new URL('http://kimiko'+t.state.sent.at(-1).url).searchParams.get('path'),'books/'+longMobi);
  for(const mode of ['ap','sta']){
    t=await setup({mode});assert.equal(t.get('signatureText').value,'慢慢阅读');
    for(const value of ['我的 Pico','']){
      t.get('signatureText').value=value;await t.get('signatureSave').onclick();assert.equal(t.state.signature,value);
      assert.equal(t.get('signatureResult').textContent,value?'签名已保存，kimiko 状态栏已更新':'签名已清除');
    }
    for(const value of ['字'.repeat(32),'换行\n签名','\u007f']){t.get('signatureText').value=value;await t.get('signatureSave').onclick();}
    assert.equal(t.state.signatures.length,2);
    await t.context.sendFiles([file('README',0),file('升级 包%+#.bin',32),file('.env',17)],'file','');
    assert.deepEqual(t.state.sent.map(s=>new URL('http://pico'+s.url).searchParams.get('path')),['README','升级 包%+#.bin','.env']);
    await t.context.navigate('books/子目录');await t.context.sendFiles([file('没有扩展名',100)],'file','books/子目录');
    assert.equal(new URL('http://pico'+t.state.sent.at(-1).url).searchParams.get('path'),'books/子目录/没有扩展名');
  }
  t=await setup({responses:[{status:409,body:{error:'重名'}},{status:200,body:{ok:true}}]});
  await t.context.sendFiles([file('同名.png')],'picture','pictures');assert.match(t.state.sent[1].url,/overwrite=1/);
  t=await setup({responses:[{status:409,body:{error:'重名'}}],confirmations:[false]});
  await t.context.sendFiles([file('同名.txt')],'file','');assert.equal(t.state.sent.length,1);assert.equal(t.run('records[0].status'),'skipped');
  t=await setup({responses:[{status:409,body:{error:'重名'}},{status:200,body:{ok:true}}]});
  await t.context.sendFiles([file('升级.bin')],'file','');assert.match(t.state.sent[1].url,/^\/file-upload\?path=.+&overwrite=1$/);
  t=await setup({freeBytes:100});await t.context.sendFiles([file('太大.epub',101),file('空.epub',0),file('不支持.pdf',1)],'book','books');assert.equal(t.state.sent.length,0);
  t=await setup();
  await t.context.sendFiles([file('大字体.ttf',33554433)],'font','fonts');await t.context.sendFiles([file('大图片.png',20971521)],'picture','pictures');
  await t.context.sendFiles([file('a'.repeat(239))],'file','');assert.equal(t.state.sent.length,0);
  t=await setup({fileLimit:100});await t.context.sendFiles([file('超限.txt',101)],'book','books');assert.equal(t.state.sent.length,0);
  t=await setup();await t.context.navigate('books');t.get('genericPicker').files=[file('其他格式.bin',0)];await t.get('genericPicker').onchange();
  assert.match(t.state.sent[0].url,/^\/file-upload\?path=books%2F/);assert.equal(t.state.sent[0].file.size,0);
  t=await setup({responses:[{network:true}]});await t.context.sendFiles([file('一.txt'),file('二.txt')],'file','');
  assert.equal(t.state.sent.length,1);assert.equal(t.run('records[1].status'),'error');assert.equal(t.run('busy'),false);
  assert.equal(t.get('chooseFiles').disabled,true);assert.equal(t.get('refresh').disabled,false);
  await t.context.refresh();await t.context.sendFiles([file('重试.txt')],'file','');assert.equal(t.state.sent.length,2);
  t=await setup({responses:[{status:507,body:{error:'空间不足'}},{status:200,body:{ok:true}}]});
  await t.context.sendFiles([file('重试原目录.bin')],'file','books/原目录');await t.context.navigate('pictures');
  await t.context.sendFiles([t.run('records[0].file')],'file','books/原目录',t.run('records[0]'));
  assert.equal(t.state.sent[0].url,t.state.sent[1].url);assert.equal(t.run('records.length'),1);assert.equal(t.run('records[0].status'),'complete');
  t=await setup({infoErrorAfter:3});await t.context.sendFiles([file('已保存.txt')],'file','');
  assert.equal(t.run('records[0].status'),'complete');assert.match(t.get('uploadResult').textContent,/已保存 1 个文件/);
  t=await setup({isFlash:true});assert.equal(t.get('chooseFiles').disabled,true);assert.equal(t.state.gets.length,0);assert.equal(t.get('signatureSave').disabled,false);
  t=await setup({signatureError:true});assert.equal(t.get('signatureSave').disabled,true);assert.equal(t.get('signatureRetry').hidden,false);
  t=await setup({directory:(path,page,json)=>json({pages:2,total:21,items:[file('第'+page+'页.txt',12)]})});
  await t.get('next').onclick();assert.match(t.state.gets.at(-1),/page=1/);assert.equal(t.get('pageState').textContent,'2 / 2');
  t.get('search').value='无匹配';t.get('search').oninput();assert.equal(t.get('entries').children.length,0);assert.match(t.get('listState').textContent,/本页/);
  let release;
  t=await setup({directory:(path,page,json)=>path==='slow'?new Promise(resolve=>release=()=>resolve(json({pages:1,total:1,items:[file('过时.txt')]}))):json({pages:1,total:1,items:[file('当前.txt')]})});
  const stale=t.context.navigate('slow');await t.context.navigate('fast');release();await stale;
  assert.equal(t.get('path').textContent,'/fast');assert.match(t.get('entries').textContent,/当前/);assert.doesNotMatch(t.get('entries').textContent,/过时/);
  t=await setup({directory:(path,page,json)=>json({pages:1,total:1,items:[file('<img src=x onerror=alert(1)>.txt',12)]})});
  assert.match(t.get('entries').textContent,/<img src=x/);assert.equal(t.get('entries').children[0].children[0].children[2].tagName,'div');
  t=await setup();t.get('selectAll').checked=true;t.get('selectAll').onchange();t.get('deleteSelected').onclick();await t.context.submitEditor();
  assert.deepEqual(t.state.mutations,[{action:'delete',path:'books'},{action:'delete',path:'readme.txt'}]);
  t=await setup({mutations:[{status:200},{status:500,body:{error:'删除失败'}},{status:200}]});
  t.get('selectAll').checked=true;t.get('selectAll').onchange();t.get('deleteSelected').onclick();await t.context.submitEditor();
  assert.equal(t.get('editor').open,true);assert.match(t.get('editorError').textContent,/readme.txt/);await t.context.submitEditor();
  assert.deepEqual(t.state.mutations.map(item=>item.path),['books','readme.txt','readme.txt']);assert.equal(t.get('editor').open,false);
  t=await setup();await t.context.sendFiles(Array.from({length:41},(_,i)=>file(i+'.txt',1)),'file','');assert.equal(t.run('records.length'),40);
  assert.equal(t.run('records[39].name'),'40.txt');assert.equal(t.run('records.every(record=>record.file===null)'),true);
  t=await setup({mode:'sta'});t.get('wallpaperFile').files=[file('photo.heic',3000000)];t.get('wallpaperFile').onchange();await flush();
  assert.equal(t.get('wallpaperSend').disabled,false);t.get('cropZoom').value='1.5';t.get('cropZoom').oninput();
  const before=t.run('[cropX,cropY]');t.get('cropPreview').onpointerdown({pointerId:1,clientX:100,clientY:100});
  t.get('cropPreview').onpointermove({pointerId:1,clientX:120,clientY:90});t.get('cropPreview').onpointerup();assert.notDeepEqual(t.run('[cropX,cropY]'),before);
  await t.get('wallpaperSend').onclick();assert.match(t.state.sent.at(-1).url,/^\/image-upload\?name=pico-wallpaper-.+&wallpaper=1$/);assert.match(t.get('wallpaperResult').textContent,/已设为壁纸锁屏/);
  t.run("location.hash='#signature';route()");assert.equal(t.get('files-view').hidden,true);assert.equal(t.get('signature-view').hidden,false);
  t.get('signatureText').value='<b>纯文本</b>';t.get('signatureText').oninput();assert.equal(t.get('signaturePreview').textContent,'<b>纯文本</b>');
  const symbols=new Set([...html.matchAll(/<symbol id="([^"]+)"/g)].map(match=>match[1]));
  for(const match of html.matchAll(/<use href="#([^"]+)"/g))assert.ok(symbols.has(match[1]),'Missing symbol '+match[1]);
  console.log('transfer web interaction tests passed');
})().catch(error=>{console.error(error);process.exitCode=1;});
