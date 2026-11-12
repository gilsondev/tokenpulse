#pragma once
// Páginas e arquivos comuns às placas com página web (SmallTV pelo WiFi de sempre, placas USB pelo WiFi que
// a pessoa liga nos Ajustes): CSS, logo, configuração do WiFi e atualização guiada. Cada firmware serve a
// sua página de ajustes à parte. Tudo que vem da placa ou do GitHub entra por textContent.
#include <pgmspace.h>

// A logo vem da placa, não do site: no WiFi de configuração o celular está sem internet.
static const char LOGO[] PROGMEM = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" role="img" aria-label="tokenpulse">
  <rect width="512" height="512" rx="112" fill="#1F1E1D"/>
  <circle cx="256" cy="256" r="176" fill="none" stroke="#3A3936" stroke-width="44"/>
  <path d="M256 80 A176 176 0 1 1 80 256" fill="none" stroke="#D97757" stroke-width="44" stroke-linecap="round"/>
  <polyline points="150,256 210,256 234,180 266,324 290,256 362,256" fill="none" stroke="#F5F4EE"
            stroke-width="26" stroke-linecap="round" stroke-linejoin="round"/>
</svg>)SVG";

static const char STYLE[] PROGMEM = R"CSS(*{box-sizing:border-box}:root{--bg:#FAF9F5;--fg:#1F1E1D;--mut:#6B6A66;--card:#FFF;--line:#E6E3DA;--acc:#C2603F}
@media(prefers-color-scheme:dark){:root{--bg:#1F1E1D;--fg:#F5F4EE;--mut:#A3A29C;--card:#2A2927;--line:#3A3935;--acc:#D97757}}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,sans-serif}
main{max-width:30em;margin:auto;padding:16px}
h1{font-size:20px;margin:8px 0 2px}.sub{color:var(--mut);margin:0 0 8px}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:12px 14px;margin:12px 0}
h2{font-size:12px;text-transform:uppercase;letter-spacing:.06em;color:var(--mut);margin:0 0 6px}
label{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:6px 0}
select,input[type=text],input[type=password]{font:inherit;padding:4px 6px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--fg);min-width:0}
input[type=range]{width:50%;accent-color:var(--acc)}input[type=checkbox]{accent-color:var(--acc);width:18px;height:18px}
dl{display:grid;grid-template-columns:auto 1fr;gap:4px 12px;margin:0}dt{color:var(--mut)}dd{margin:0;text-align:right;overflow-wrap:anywhere}
button,.btn{font:inherit;padding:6px 12px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--fg);cursor:pointer;text-decoration:none}
.row{display:flex;gap:8px;flex-wrap:wrap}#msg{color:var(--acc);min-height:1.4em;margin:0}[hidden]{display:none}
.steps{counter-reset:s;list-style:none;padding:0;margin:0}.steps>li{counter-increment:s;position:relative;padding:2px 0 14px 40px}
.steps>li::before{content:counter(s);position:absolute;left:0;top:0;width:26px;height:26px;border-radius:50%;display:grid;place-items:center;font-size:13px;font-weight:600;background:var(--line);color:var(--fg)}
.steps>li.ok::before{content:"✓";background:#3C8A4E;color:#fff}.steps>li.bad::before{content:"!";background:#B9472F;color:#fff}
.steps h3{font-size:15px;margin:2px 0 4px}.steps p{margin:4px 0;color:var(--mut)}
.drop{border:2px dashed var(--line);border-radius:10px;padding:18px;text-align:center;color:var(--mut);cursor:pointer}.drop.on{border-color:var(--acc);color:var(--fg)}
progress{width:100%;height:10px;accent-color:var(--acc)}code{font:13px ui-monospace,monospace;background:var(--bg);padding:1px 5px;border-radius:5px}
.good{color:#3C8A4E}.warn{color:#C28A1E}.err{color:#B9472F}a{color:var(--acc)}
.nets{display:grid;gap:6px;margin:8px 0}.net{display:flex;align-items:center;gap:10px;width:100%;text-align:left;padding:10px 12px;border-radius:10px;border:1px solid var(--line);background:var(--bg);color:var(--fg);font:inherit;cursor:pointer}
.net.on{border-color:var(--acc);box-shadow:0 0 0 1px var(--acc)}.net .n{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.bars{display:inline-flex;align-items:flex-end;gap:2px;height:14px}.bars i{width:4px;background:var(--line);border-radius:1px}.bars i.f{background:var(--fg)}
.spin{display:inline-block;width:14px;height:14px;border:2px solid var(--line);border-top-color:var(--acc);border-radius:50%;animation:r .8s linear infinite;vertical-align:-2px;margin-right:6px}@keyframes r{to{transform:rotate(1turn)}}
.brand{display:flex;align-items:center;gap:8px;font-weight:700;color:var(--fg);text-decoration:none;margin:4px 0 14px}.brand img{width:28px;height:28px}
.big{font-size:17px}.hint{font-size:14px;color:var(--mut)}input.full{width:100%;padding:10px 12px;font-size:16px}
)CSS";

static const char WIFI_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang=pt-BR><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>WiFi · tokenpulse</title><link rel=stylesheet href=/style.css><link rel=icon href=/logo.svg type=image/svg+xml>
<main>
<a class=brand href=/wifi><img src=/logo.svg alt="">tokenpulse</a>
<h1>Conectar a placa ao seu WiFi</h1><p class=sub id=sub></p>
<p>Escolha a <b>mesma rede do Mac</b> em que você usa o Claude Code ou o Codex: é por ela que o Mac manda os dados para a placa.</p>
<section><ol class=steps>
<li id=s1><h3>Escolha a rede</h3>
<div id=list class=nets><p class=hint><span class=spin></span>procurando redes…</p></div>
<div class=row><button id=again type=button>Procurar de novo</button><button id=other type=button>Minha rede não aparece</button></div>
<div id=manual hidden><p class=hint>Rede oculta, ou que não apareceu na lista? Digite o nome exatamente como é, com maiúsculas e minúsculas.</p>
<input class=full id=ssid placeholder="nome da rede" autocapitalize=off autocorrect=off spellcheck=false></div>
<p class=hint>A placa só enxerga redes de 2,4 GHz. Se o roteador tem duas redes (ex.: "Casa" e "Casa_5G"), escolha a que não termina em 5G.</p></li>
<li id=s2><h3>Digite a senha</h3>
<input class=full id=pass type=password placeholder="senha do WiFi" autocapitalize=off autocorrect=off spellcheck=false>
<label class=hint><span>Mostrar a senha</span><input type=checkbox id=show></label><p id=open class=hint hidden>Essa rede é aberta: não precisa de senha.</p></li>
<li id=s3><h3>Conecte</h3>
<div class=row><button id=go type=button disabled>Conectar</button></div>
<p id=msg class=big></p><p id=tip class=hint></p></li>
</ol></section>
<section id=done hidden><h2>Pronto</h2><p class=big>A placa está na rede <b id=dssid></b>.</p>
<dl><dt>Endereço</dt><dd><a id=dhost></a></dd><dt>IP</dt><dd id=dip></dd></dl>
<ol class=steps><li><h3>Volte o celular para a sua rede de sempre</h3><p>O WiFi <b class=apn></b> some em 1 minuto.</p></li>
<li><h3>Instale o tokenpulse no Mac</h3><p>Um comando no Terminal: o passo a passo está em <a href="https://gilsondev.github.io/tokenpulse/comecar/">gilsondev.github.io/tokenpulse/comecar</a>. A tela da placa mostra um QR code para essa página.</p></li></ol></section>
<p class=hint><a href=/config>Ajustes da placa</a></p>
</main>
<script>
const $=id=>document.getElementById(id),mark=(id,c)=>{$(id).className=c};
let pick=null,open=false;
const bars=r=>{const n=r>-60?4:r>-70?3:r>-80?2:1;let h='';for(let i=1;i<=4;i++)h+='<i class="'+(i<=n?'f':'')+'" style="height:'+(3*i+2)+'px"></i>';return'<span class=bars>'+h+'</span>'};
function ready(){$('go').disabled=!(pick&&(open||$('pass').value.length>=8||$('pass').value.length==0&&open))}
function choose(ssid,isOpen,btn){pick=ssid;open=isOpen;document.querySelectorAll('.net').forEach(b=>b.classList.remove('on'));if(btn)btn.classList.add('on');
  $('pass').disabled=open;$('open').hidden=!open;mark('s1','ok');ready();if(!open)$('pass').focus()}
async function scan(again){
  $('list').innerHTML='<p class=hint><span class=spin></span>procurando redes…</p>';
  try{for(let i=0;i<20;i++){const j=await(await fetch('/api/scan'+(again&&!i?'?again=1':''),{cache:'no-store'})).json();
    if(j.state==='done'){show(j.nets);return}await new Promise(r=>setTimeout(r,1500))}}catch(e){}
  $('list').innerHTML='<p class=hint>Não consegui procurar agora. Toque em "Procurar de novo" ou digite o nome da rede.</p>';
}
function show(nets){
  const l=$('list');l.textContent='';
  if(!nets.length){l.innerHTML='<p class=hint>Nenhuma rede de 2,4 GHz por perto. Aproxime a placa do roteador e procure de novo.</p>';return}
  for(const[ssid,rssi,isOpen]of nets){const b=document.createElement('button');b.type='button';b.className='net';
    const n=document.createElement('span');n.className='n';n.textContent=ssid;b.append(n);
    b.insertAdjacentHTML('beforeend',(isOpen?'':'<span title=senha>🔒</span>')+bars(rssi));
    b.onclick=()=>{$('manual').hidden=true;choose(ssid,isOpen,b)};l.append(b)}
}
$('again').onclick=()=>scan(true);
$('other').onclick=()=>{$('manual').hidden=false;$('ssid').focus();document.querySelectorAll('.net').forEach(b=>b.classList.remove('on'));pick=null;ready()};
$('ssid').oninput=e=>{pick=e.target.value.trim()||null;open=false;$('pass').disabled=false;$('open').hidden=true;if(pick)mark('s1','ok');ready()};
$('pass').oninput=()=>{mark('s2',$('pass').value.length>=8?'ok':'');ready()};
$('show').onchange=e=>$('pass').type=e.target.checked?'text':'password';
const WHY={senha:['A senha não confere.','Digite de novo com calma: maiúsculas e minúsculas contam.'],
  rede:['A placa não achou essa rede.','Ela pode ser de 5 GHz ou estar longe. Aproxime a placa do roteador ou escolha outra rede.'],
  tempo:['A rede não respondeu.','Confira a senha e tente de novo. Se continuar, reinicie a placa tirando da tomada.']};
$('go').onclick=async()=>{
  $('go').disabled=true;mark('s3','');$('tip').textContent='';
  $('msg').innerHTML='<span class=spin></span>conectando a '+pick.replace(/[<&]/g,'')+'…';
  try{await fetch('/wifi',{method:'POST',body:new URLSearchParams({ssid:pick,pass:open?'':$('pass').value})})}catch(e){}
  for(let i=0;i<40;i++){await new Promise(r=>setTimeout(r,1500));
    let j;try{j=await(await fetch('/api/wifi',{cache:'no-store'})).json()}
    catch(e){$('tip').textContent='O celular saiu do WiFi da placa durante a troca de canal. Se não voltar sozinho, entre de novo em '+(window.AP||'tokenpulse-…')+'.';continue}
    if(j.state==='connected'){mark('s3','ok');$('msg').textContent='Conectada.';$('done').hidden=false;$('dssid').textContent=j.ssid;
      $('dhost').textContent='http://'+j.host+'.local';$('dhost').href='http://'+j.host+'.local/';$('dip').textContent=j.ip;$('done').scrollIntoView({behavior:'smooth'});return}
    if(j.state==='failed'){mark('s3','bad');const w=WHY[j.why]||WHY.tempo;$('msg').textContent=w[0];$('tip').textContent=w[1];$('go').disabled=false;return}
  }
  mark('s3','bad');$('msg').textContent=WHY.tempo[0];$('tip').textContent=WHY.tempo[1];$('go').disabled=false;
};
fetch('/api/state').then(r=>r.json()).then(s=>{window.AP=s.apName;$('sub').textContent=s.board+' · '+s.apName;document.querySelectorAll('.apn').forEach(e=>e.textContent=s.apName)}).catch(()=>{});
scan(false);
</script>)HTML";

static const char UPDATE_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang=pt-BR><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Atualizar · tokenpulse</title><link rel=stylesheet href=/style.css><link rel=icon href=/logo.svg type=image/svg+xml>
<main>
<a class=brand href=/config><img src=/logo.svg alt="">tokenpulse</a>
<p><a href=/config>← ajustes da placa</a></p>
<h1>Atualizar o firmware</h1><p class=sub id=sub>lendo a placa…</p>
<section><h2>Versões</h2><dl id=ver></dl><p id=verMsg></p></section>
<section><h2>Passo a passo</h2><ol class=steps>
<li id=s1><h3>Baixe o firmware</h3>
<p>Baixe o <a id=bin href="https://github.com/gilsondev/tokenpulse/releases/latest">arquivo desta placa</a> do <a href="https://github.com/gilsondev/tokenpulse/releases/latest" target=_blank>último release</a>. Use só o arquivo dela: o das outras placas não roda aqui.</p>
<p>Compilou da fonte? Use <code id=built>firmware/.pio/build/&lt;placa&gt;/firmware.bin</code>.</p></li>
<li id=s2><h3>Escolha o arquivo</h3>
<label class=drop id=drop><input type=file id=file accept=".bin,.gz" hidden><span id=fileMsg>Toque para escolher ou arraste o .bin aqui</span></label></li>
<li id=s3><h3>Envie para a placa</h3>
<p>Leva uns 10 s. Não feche a página e não desligue a placa.</p>
<div class=row><button id=send disabled>Enviar firmware</button></div>
<p><progress id=bar max=100 value=0 hidden></progress></p><p id=sendMsg></p></li>
<li id=s4><h3>Confira se deu certo</h3>
<p id=doneMsg>A placa reinicia sozinha. Esta página espera ela voltar e confere a versão.</p></li>
</ol>
<p>Se algo falhar no meio, a placa continua na versão atual: ela só troca depois de gravar e conferir o arquivo inteiro.<span id=guard hidden> Se o firmware novo travar 3 vezes seguidas, ela volta só com WiFi e esta página, para você mandar outro.</span></p>
</section>
<section><h2>Pelo Mac</h2><p>Com o daemon rodando, dá para atualizar sem o navegador: <code>./update.sh</code> manda o último release, e <code id=flash>firmware/flash.sh &lt;placa&gt;</code> compila e manda.</p></section>
</main>
<script>
const $=id=>document.getElementById(id),mark=(id,c)=>{$(id).className=c};
const say=(id,t,c)=>{$(id).textContent=t;$(id).className=c||''};
let before=null,room=0,file=null,board='';
async function state(t){const c=new AbortController();setTimeout(()=>c.abort(),t||3000);
  return(await fetch('/api/state',{cache:'no-store',signal:c.signal})).json()}
function row(k,v){const t=document.createElement('dt'),d=document.createElement('dd');t.textContent=k;d.textContent=v;$('ver').append(t,d)}
state().then(s=>{before=s.fw;room=s.ota;board=s.board;
  $('bin').href='https://github.com/gilsondev/tokenpulse/releases/latest/download/tokenpulse-'+board+'-ota.bin';$('bin').textContent='tokenpulse-'+board+'-ota.bin';
  $('built').textContent='firmware/.pio/build/'+board+'/firmware.bin';$('flash').textContent='firmware/flash.sh '+board;$('guard').hidden=board!=='smalltv-ultra';$('sub').textContent=s.board+' em http://'+s.host+'.local';
  row('Na placa',s.fw);row('Espaço para o novo',Math.floor(s.ota/1024)+' KB');
  fetch('https://api.github.com/repos/gilsondev/tokenpulse/releases/latest').then(r=>r.json()).then(j=>{
    row('Último release',j.tag_name);
    if(s.fw===j.tag_name){say('verMsg','A placa já está no último release.','good');mark('s1','ok')}
    else if(/^dev|-g[0-9a-f]+|-dirty$/.test(s.fw))say('verMsg','A placa roda um firmware compilado localmente. O release pode ser mais antigo que ele.','warn');
    else say('verMsg','Há uma versão nova: siga os passos abaixo.','warn');
  }).catch(()=>say('verMsg','Não consegui consultar o GitHub agora. O link do passo 1 continua valendo.','warn'));
}).catch(()=>say('sub','Não consegui ler a placa. Recarregue a página.','err'));
function pick(f){
  file=null;$('send').disabled=true;mark('s2','');
  if(!f)return;
  if(!/\.bin(\.gz)?$/i.test(f.name))return say('fileMsg',f.name+': escolha um arquivo .bin','err'),mark('s2','bad');
  if(board&&/tokenpulse-/.test(f.name)&&f.name.indexOf(board)<0)return say('fileMsg',f.name+' é de outra placa. Use o tokenpulse-'+board+'-ota.bin.','err'),mark('s2','bad');
  if(room&&f.size>room)return say('fileMsg',f.name+' tem '+Math.round(f.size/1024)+' KB e não cabe ('+Math.floor(room/1024)+' KB livres).','err'),mark('s2','bad');
  file=f;say('fileMsg',f.name+' · '+Math.round(f.size/1024)+' KB','good');mark('s2','ok');mark('s1','ok');$('send').disabled=false;
}
$('file').onchange=e=>pick(e.target.files[0]);
const drop=$('drop');
drop.ondragover=e=>{e.preventDefault();drop.classList.add('on')};drop.ondragleave=()=>drop.classList.remove('on');
drop.ondrop=e=>{e.preventDefault();drop.classList.remove('on');pick(e.dataTransfer.files[0])};
$('send').onclick=()=>{
  if(!file)return;$('send').disabled=true;$('bar').hidden=false;say('sendMsg','Enviando…');
  const x=new XMLHttpRequest(),fd=new FormData();fd.append('firmware',file,file.name);
  x.upload.onprogress=e=>{if(e.lengthComputable)$('bar').value=Math.round(100*e.loaded/e.total)};
  x.onload=()=>{
    if(/Update Success/.test(x.responseText)){$('bar').value=100;say('sendMsg','Gravado e conferido. A placa está reiniciando…','good');mark('s3','ok');wait()}
    else{say('sendMsg','A placa recusou: '+x.responseText.replace(/<[^>]*>/g,'').trim()+'. Ela continua na versão atual.','err');mark('s3','bad');$('send').disabled=false}
  };
  x.onerror=()=>{say('sendMsg','A conexão caiu no meio do envio. A placa continua na versão atual; tente de novo.','err');mark('s3','bad');$('send').disabled=false};
  x.open('POST','/update');x.send(fd);
};
async function wait(){
  const t0=Date.now();await new Promise(r=>setTimeout(r,4000));
  while(Date.now()-t0<90000){
    try{const s=await state(2000);
      if(s.safe){say('doneMsg','A placa voltou em modo de recuperação: o firmware novo travou ao iniciar. Mande outro arquivo pelo passo 2.','err');mark('s4','bad');return}
      if(s.fw!==before){say('doneMsg','Pronto: a placa voltou com '+s.fw+'.','good');mark('s4','ok');return}
      say('doneMsg','A placa voltou, mas com a mesma versão ('+s.fw+'). O arquivo enviado pode ser o mesmo que já estava nela.','warn');mark('s4','ok');return
    }catch(e){say('doneMsg','Esperando a placa voltar… '+Math.round((Date.now()-t0)/1000)+' s')}
    await new Promise(r=>setTimeout(r,2000));
  }
  say('doneMsg','A placa não respondeu em 90 s. Confira se ela está ligada e na mesma rede, e recarregue esta página.','err');mark('s4','bad');
}
</script>)HTML";
